#include "Images.h"

#include "Text.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <objidl.h>
#include <shellapi.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iterator>
#include <system_error>


namespace Loom::Images
{
	using Microsoft::WRL::ComPtr;

	namespace
	{
		constexpr uintmax_t read_limit = 64 * 1024 * 1024;

		constexpr std::string_view gif_media = "image/gif";
		constexpr std::string_view webp_media = "image/webp";

		constexpr std::string_view image_extensions[] = { ".png", ".jpg", ".jpeg", ".gif", ".webp", ".bmp" };

		constexpr wchar_t pick_filter[] = L"Images\0*.png;*.jpg;*.jpeg;*.gif;*.webp;*.bmp\0";
		constexpr DWORD pick_buffer = 32 * 1024;

		constexpr std::string_view base64_digits = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
		constexpr size_t base64_group = 4;
		constexpr size_t byte_group = 3;
		constexpr int digit_bits = 6;
		constexpr int byte_bits = 8;
		constexpr uint32_t digit_mask = 63;
		constexpr uint32_t byte_mask = 255;

		#pragma region Windows Imaging

		struct ComScope final
		{
			explicit ComScope(DWORD model = COINIT_MULTITHREADED) :
				m_result(CoInitializeEx(nullptr, model))
			{
			};

			~ComScope()
			{
				if (SUCCEEDED(m_result))
					CoUninitialize();
			};

			ComScope(const ComScope&) = delete;
			ComScope& operator=(const ComScope&) = delete;

		private:
			HRESULT m_result;
		};

		ComPtr<IWICImagingFactory> Factory()
		{
			ComPtr<IWICImagingFactory> factory;
			CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));

			return factory;
		};

		ComPtr<IWICBitmapFrameDecode> FirstFrame(IWICImagingFactory& factory, const std::string& bytes)
		{
			ComPtr<IWICStream> stream;
			ComPtr<IWICBitmapDecoder> decoder;
			ComPtr<IWICBitmapFrameDecode> frame;

			if (FAILED(factory.CreateStream(&stream)) ||
				FAILED(stream->InitializeFromMemory((BYTE*)bytes.data(), (DWORD)bytes.size())) ||
				FAILED(factory.CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder)) ||
				FAILED(decoder->GetFrame(0, &frame)))
				return nullptr;

			return frame;
		};

		bool Convert(IWICImagingFactory& factory, IWICBitmapSource* frame, int side, Pixels& pixels)
		{
			constexpr UINT channels = 4;

			UINT width = 0;
			UINT height = 0;

			if (FAILED(frame->GetSize(&width, &height)) || width == 0 || height == 0)
				return false;

			ComPtr<IWICBitmapSource> source = frame;
			const UINT longest = std::max(width, height);

			if (longest > (UINT)side)
			{
				const double scale = (double)side / longest;
				width = std::max(1u, (UINT)(width * scale));
				height = std::max(1u, (UINT)(height * scale));

				ComPtr<IWICBitmapScaler> scaler;

				if (FAILED(factory.CreateBitmapScaler(&scaler)) ||
					FAILED(scaler->Initialize(source.Get(), width, height, WICBitmapInterpolationModeFant)))
					return false;

				source = scaler;
			};

			ComPtr<IWICFormatConverter> converter;

			if (FAILED(factory.CreateFormatConverter(&converter)) ||
				FAILED(converter->Initialize(source.Get(), GUID_WICPixelFormat32bppRGBA,
					WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)))
				return false;

			pixels.width = (int)width;
			pixels.height = (int)height;
			pixels.rgba.resize((size_t)width * height * channels);

			return SUCCEEDED(converter->CopyPixels(nullptr, width * channels, (UINT)pixels.rgba.size(), pixels.rgba.data()));
		};

		std::string EncodeAs(IWICImagingFactory& factory, const Pixels& pixels, const GUID& container)
		{
			constexpr UINT channels = 4;

			ComPtr<IWICBitmap> bitmap;
			ComPtr<IStream> stream;
			ComPtr<IWICBitmapEncoder> encoder;
			ComPtr<IWICBitmapFrameEncode> frame;

			if (FAILED(factory.CreateBitmapFromMemory((UINT)pixels.width, (UINT)pixels.height, GUID_WICPixelFormat32bppRGBA,
					(UINT)pixels.width * channels, (UINT)pixels.rgba.size(), (BYTE*)pixels.rgba.data(), &bitmap)) ||
				FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream)) ||
				FAILED(factory.CreateEncoder(container, nullptr, &encoder)) ||
				FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) ||
				FAILED(encoder->CreateNewFrame(&frame, nullptr)) ||
				FAILED(frame->Initialize(nullptr)) ||
				FAILED(frame->WriteSource(bitmap.Get(), nullptr)) ||
				FAILED(frame->Commit()) ||
				FAILED(encoder->Commit()))
				return { };

			HGLOBAL memory = nullptr;
			STATSTG stat{ };

			if (FAILED(GetHGlobalFromStream(stream.Get(), &memory)) || FAILED(stream->Stat(&stat, STATFLAG_NONAME)))
				return { };

			const void* const locked = GlobalLock(memory);
			std::string bytes((const char*)locked, (size_t)stat.cbSize.QuadPart);
			GlobalUnlock(memory);

			return bytes;
		};

		#pragma endregion

		#pragma region Clipboard

		std::string BitmapFile(const std::string& dib)
		{
			constexpr WORD bitmap_mark = 0x4D42;
			constexpr size_t bitfield_masks = 3;
			constexpr WORD palette_depth = 8;

			if (dib.size() < sizeof(BITMAPINFOHEADER))
				return { };

			BITMAPINFOHEADER info{ };
			std::memcpy(&info, dib.data(), sizeof(info));

			size_t colors = info.biClrUsed;

			if (colors == 0 && info.biBitCount <= palette_depth)
				colors = (size_t)1 << info.biBitCount;

			if (info.biCompression == BI_BITFIELDS && info.biSize == sizeof(BITMAPINFOHEADER))
				colors = bitfield_masks;

			BITMAPFILEHEADER header{ };
			header.bfType = bitmap_mark;
			header.bfSize = (DWORD)(sizeof(header) + dib.size());
			header.bfOffBits = (DWORD)(sizeof(header) + info.biSize + colors * sizeof(RGBQUAD));

			std::string bytes((const char*)&header, sizeof(header));
			bytes += dib;

			return bytes;
		};

		std::string ClipboardBytes(UINT format)
		{
			const HANDLE handle = GetClipboardData(format);
			const void* const locked = handle != nullptr ? GlobalLock(handle) : nullptr;

			if (locked == nullptr)
				return { };

			std::string bytes((const char*)locked, GlobalSize(handle));
			GlobalUnlock(handle);

			return bytes;
		};

		#pragma endregion
	};

	bool IsImageFile(std::string_view path)
	{
		std::string extension = Text::PathUtf8(Text::Utf8Path(path).extension());
		std::ranges::transform(extension, extension.begin(), [](unsigned char letter) { return (char)std::tolower(letter); });

		return std::ranges::find(image_extensions, std::string_view(extension)) != std::end(image_extensions);
	};

	float Scale(float width, float height, float spaceWidth, float spaceHeight)
	{
		return std::min(spaceWidth / width, spaceHeight / height);
	};

	#pragma region Encoding

	std::string Base64(std::string_view bytes)
	{
		std::string text;
		text.reserve((bytes.size() + byte_group - 1) / byte_group * base64_group);

		for (size_t index = 0; index < bytes.size(); index += byte_group)
		{
			const size_t left = bytes.size() - index;
			uint32_t group = (uint32_t)(uint8_t)bytes[index] << byte_bits * 2;

			if (left > 1)
				group |= (uint32_t)(uint8_t)bytes[index + 1] << byte_bits;

			if (left > 2)
				group |= (uint8_t)bytes[index + 2];

			text += base64_digits[group >> digit_bits * 3 & digit_mask];
			text += base64_digits[group >> digit_bits * 2 & digit_mask];
			text += left > 1 ? base64_digits[group >> digit_bits & digit_mask] : '=';
			text += left > 2 ? base64_digits[group & digit_mask] : '=';
		};

		return text;
	};

	std::string FromBase64(std::string_view text)
	{
		std::string bytes;
		bytes.reserve(text.size() / base64_group * byte_group);

		uint32_t group = 0;
		int bits = 0;

		for (const char digit : text)
		{
			const size_t value = base64_digits.find(digit);

			if (value == std::string_view::npos)
				continue;

			group = group << digit_bits | (uint32_t)value;
			bits += digit_bits;

			if (bits >= byte_bits)
			{
				bits -= byte_bits;
				bytes += (char)(group >> bits & byte_mask);
			};
		};

		return bytes;
	};

	std::string HashName(std::string_view text)
	{
		constexpr uint64_t offset = 14695981039346656037ull;
		constexpr uint64_t prime = 1099511628211ull;
		constexpr size_t digits = 16;
		constexpr int digit_width = 4;
		constexpr uint64_t last_digit = 15;

		uint64_t hash = offset;

		for (const char byte : text)
			hash = (hash ^ (uint8_t)byte) * prime;

		std::string name(digits, '0');

		for (size_t index = digits; index-- > 0; hash >>= digit_width)
			name[index] = "0123456789abcdef"[hash & last_digit];

		return name;
	};

	std::string_view MediaOf(std::string_view bytes)
	{
		constexpr std::string_view png_mark = "\x89PNG";
		constexpr std::string_view jpeg_mark = "\xFF\xD8\xFF";
		constexpr std::string_view gif_mark = "GIF8";
		constexpr std::string_view riff_mark = "RIFF";
		constexpr std::string_view webp_mark = "WEBP";
		constexpr size_t webp_offset = 8;

		if (bytes.starts_with(png_mark))
			return PngMedia;

		if (bytes.starts_with(jpeg_mark))
			return JpegMedia;

		if (bytes.starts_with(gif_mark))
			return gif_media;

		if (bytes.starts_with(riff_mark) && bytes.substr(std::min(bytes.size(), webp_offset)).starts_with(webp_mark))
			return webp_media;

		return { };
	};

	std::string_view ExtensionOf(std::string_view media)
	{
		return media == JpegMedia ? ".jpg" : media == gif_media ? ".gif" : media == webp_media ? ".webp" : ".png";
	};

	#pragma endregion

	#pragma region Files

	bool ReadFile(const std::filesystem::path& file, std::string& bytes, std::string& error)
	{
		std::error_code code;
		const uintmax_t size = std::filesystem::file_size(file, code);

		if (code)
		{
			error = "Image not found: " + Text::PathUtf8(file.filename());
			return false;
		};

		if (size > read_limit)
		{
			error = "Image too large to read: " + Text::PathUtf8(file.filename());
			return false;
		};

		std::ifstream stream(file, std::ios::binary);
		bytes.assign(std::istreambuf_iterator<char>(stream), { });

		if (bytes.size() != size)
		{
			error = "Image could not be read: " + Text::PathUtf8(file.filename());
			return false;
		};

		return true;
	};

	bool Decode(const std::filesystem::path& file, int side, Pixels& pixels, std::string& error)
	{
		std::string bytes;

		if (!ReadFile(file, bytes, error))
			return false;

		const ComScope com;
		const ComPtr<IWICImagingFactory> factory = Factory();
		const ComPtr<IWICBitmapFrameDecode> frame = factory ? FirstFrame(*factory.Get(), bytes) : nullptr;

		if (frame && Convert(*factory.Get(), frame.Get(), side, pixels))
			return true;

		error = "Not an image: " + Text::PathUtf8(file.filename());

		return false;
	};

	bool DecodeBytes(const std::string& bytes, int side, Pixels& pixels, std::string& error)
	{
		return ImageDecoder().Decode(bytes, side, pixels, error);
	};

	struct ImageDecoder::State final
	{
		const ComScope com;
		const ComPtr<IWICImagingFactory> factory = Factory();
	};

	ImageDecoder::ImageDecoder() :
		m_state(std::make_unique<State>())
	{
	};

	ImageDecoder::~ImageDecoder() = default;

	bool ImageDecoder::Decode(const std::string& bytes, int side, Pixels& pixels, std::string& error) const
	{
		IWICImagingFactory* const factory = m_state->factory.Get();
		const ComPtr<IWICBitmapFrameDecode> frame = factory != nullptr ? FirstFrame(*factory, bytes) : nullptr;

		if (frame && Convert(*factory, frame.Get(), side, pixels))
			return true;

		error = "Not an image.";

		return false;
	};

	bool MeasureBytes(const std::string& bytes, int& width, int& height, std::string& error)
	{
		const ComScope com;
		const ComPtr<IWICImagingFactory> factory = Factory();
		const ComPtr<IWICBitmapFrameDecode> frame = factory ? FirstFrame(*factory.Get(), bytes) : nullptr;

		UINT wide = 0;
		UINT high = 0;

		if (!frame || FAILED(frame->GetSize(&wide, &high)))
		{
			error = "Not an image.";
			return false;
		};

		width = (int)wide;
		height = (int)high;

		return true;
	};

	bool Measure(const std::filesystem::path& file, int& width, int& height, std::string& error)
	{
		std::error_code code;

		if (!std::filesystem::exists(file, code))
		{
			error = "Image not found: " + Text::PathUtf8(file.filename());
			return false;
		};

		const ComScope com;
		const ComPtr<IWICImagingFactory> factory = Factory();
		ComPtr<IWICBitmapDecoder> decoder;
		ComPtr<IWICBitmapFrameDecode> frame;

		UINT wide = 0;
		UINT high = 0;

		if (!factory ||
			FAILED(factory->CreateDecoderFromFilename(file.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder)) ||
			FAILED(decoder->GetFrame(0, &frame)) ||
			FAILED(frame->GetSize(&wide, &high)))
		{
			error = "Not an image: " + Text::PathUtf8(file.filename());
			return false;
		};

		width = (int)wide;
		height = (int)high;

		return true;
	};

	bool Resize(const Pixels& source, int width, int height, Pixels& pixels)
	{
		constexpr UINT channels = 4;

		const ComScope com;
		const ComPtr<IWICImagingFactory> factory = Factory();
		ComPtr<IWICBitmap> bitmap;
		ComPtr<IWICBitmapScaler> scaler;
		ComPtr<IWICFormatConverter> converter;

		if (!factory
			|| FAILED(factory->CreateBitmapFromMemory((UINT)source.width, (UINT)source.height, GUID_WICPixelFormat32bppRGBA,
				(UINT)source.width * channels, (UINT)source.rgba.size(), (BYTE*)source.rgba.data(), &bitmap))
			|| FAILED(factory->CreateBitmapScaler(&scaler))
			|| FAILED(scaler->Initialize(bitmap.Get(), (UINT)width, (UINT)height, WICBitmapInterpolationModeFant))
			|| FAILED(factory->CreateFormatConverter(&converter))
			|| FAILED(converter->Initialize(scaler.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)))
			return false;

		pixels.width = width;
		pixels.height = height;
		pixels.rgba.resize((size_t)width * height * channels);

		return SUCCEEDED(converter->CopyPixels(nullptr, (UINT)width * channels, (UINT)pixels.rgba.size(), pixels.rgba.data()));
	};

	Pixels Enlarged(const Pixels& source, int factor)
	{
		constexpr size_t channels = 4;

		const size_t times = (size_t)std::max(1, factor);
		const size_t width = (size_t)source.width * times;
		const size_t height = (size_t)source.height * times;

		Pixels enlarged;
		enlarged.width = (int)width;
		enlarged.height = (int)height;
		enlarged.rgba.resize(width * height * channels);

		for (size_t y = 0; y < height; ++y)
			for (size_t x = 0; x < width; ++x)
				std::copy_n(
					source.rgba.begin() + ((y / times) * (size_t)source.width + x / times) * channels,
					channels,
					enlarged.rgba.begin() + (y * width + x) * channels);

		return enlarged;
	};

	std::string Encode(const Pixels& pixels, Format format)
	{
		const ComScope com;
		const ComPtr<IWICImagingFactory> factory = Factory();

		if (!factory)
			return { };

		return EncodeAs(*factory.Get(), pixels, format == Format::Png ? GUID_ContainerFormatPng : GUID_ContainerFormatJpeg);
	};

	#pragma endregion

	ClipboardImages ReadClipboard()
	{
		ClipboardImages images;

		if (!OpenClipboard(nullptr))
			return images;

		const UINT png = RegisterClipboardFormatW(L"PNG");

		if (IsClipboardFormatAvailable(png))
			images.bytes = ClipboardBytes(png);
		else if (IsClipboardFormatAvailable(CF_DIB))
			images.bytes = BitmapFile(ClipboardBytes(CF_DIB));
		else if (const HDROP files = (HDROP)GetClipboardData(CF_HDROP); files != nullptr)
		{
			const UINT count = DragQueryFileW(files, UINT_MAX, nullptr, 0);

			for (UINT index = 0; index < count; index++)
			{
				std::wstring path(DragQueryFileW(files, index, nullptr, 0), L'\0');
				DragQueryFileW(files, index, path.data(), (UINT)path.size() + 1);
				images.files.push_back(Text::PathUtf8(path));
			};
		};

		CloseClipboard();

		return images;
	};

	std::vector<std::string> Pick(void* owner)
	{
		const ComScope com(COINIT_APARTMENTTHREADED);

		std::wstring buffer(pick_buffer, L'\0');

		OPENFILENAMEW dialog{ };
		dialog.lStructSize = sizeof(dialog);
		dialog.hwndOwner = (HWND)owner;
		dialog.lpstrFilter = pick_filter;
		dialog.lpstrFile = buffer.data();
		dialog.nMaxFile = pick_buffer;
		dialog.Flags = OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

		if (!GetOpenFileNameW(&dialog))
			return { };

		// The folder then each name, or the whole path when one was picked.
		const std::filesystem::path first(buffer.c_str());
		size_t at = wcslen(buffer.c_str()) + 1;

		if (buffer[at] == L'\0')
			return { Text::PathUtf8(first) };

		std::vector<std::string> picked;

		for (; buffer[at] != L'\0'; at += wcslen(buffer.c_str() + at) + 1)
			picked.push_back(Text::PathUtf8(first / std::filesystem::path(buffer.c_str() + at)));

		return picked;
	};

	void Open(const std::string& path)
	{
		ShellExecuteW(nullptr, L"open", Text::Utf8Path(path).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
	};
};
