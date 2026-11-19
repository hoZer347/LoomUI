#include "doctest.h"

#include "Images.h"
#include "Text.h"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

using namespace Loom;

#pragma region Helpers

namespace
{
	constexpr size_t channels = 4;
	constexpr uint8_t opaque = 255;
	constexpr int small_side = 2;
	constexpr int pattern_width = 7;
	constexpr int pattern_height = 5;
	constexpr int wide_width = 300;
	constexpr int wide_height = 100;
	constexpr int halved_side = 150;
	constexpr int large_side = 4096;
	constexpr int line_length = 1000;
	constexpr int line_side = 10;
	constexpr int jpeg_tolerance = 8;
	constexpr uint8_t red = 200;
	constexpr uint8_t green = 120;
	constexpr uint8_t blue = 40;
	constexpr int pattern_step = 30;
	constexpr size_t webp_length = 16;
	constexpr size_t binary_length = 12;
	constexpr WORD bitmap_mark = 0x4D42;
	constexpr WORD bitmap_depth = 32;

	Images::Pixels Solid(
		int width,
		int height)
	{
		Images::Pixels pixels;
		pixels.width = width;
		pixels.height = height;

		for (int index = 0; index < width * height; index++)
			pixels.rgba.insert(pixels.rgba.end(), { red, green, blue, opaque });

		return pixels;
	};

	Images::Pixels Pattern()
	{
		Images::Pixels pixels;
		pixels.width = pattern_width;
		pixels.height = pattern_height;

		for (int y = 0; y < pattern_height; y++)
			for (int x = 0; x < pattern_width; x++)
				pixels.rgba.insert(pixels.rgba.end(), { (uint8_t)(x * pattern_step), (uint8_t)(y * pattern_step), (uint8_t)((x + y) * pattern_step), opaque });

		return pixels;
	};

	std::string Png(
		int width,
		int height)
	{
		const std::string bytes = Images::Encode(Solid(width, height), Images::Format::Png);

		REQUIRE(!bytes.empty());

		return bytes;
	};

	void Write(
		const std::filesystem::path& file,
		const std::string& bytes)
	{
		std::ofstream stream(file, std::ios::binary);
		stream.write(bytes.data(), (std::streamsize)bytes.size());
	};

	struct Scratch final
	{
		Scratch() :
			path(std::filesystem::temp_directory_path() / "Loom Images")
		{
			std::filesystem::create_directories(path);
		};

		~Scratch()
		{
			std::error_code ignored;
			std::filesystem::remove_all(path, ignored);
		};

		Scratch(const Scratch&) = delete;
		Scratch& operator=(const Scratch&) = delete;

		const std::filesystem::path path;
	};

	void PutClipboard(
		UINT format,
		const std::string& bytes)
	{
		const HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes.size());
		std::memcpy(GlobalLock(memory), bytes.data(), bytes.size());
		GlobalUnlock(memory);
		SetClipboardData(format, memory);
	};

	template <typename Check>
	void OnPrivateClipboard(Check check)
	{
		std::thread worker([&check]()
		{
			HWINSTA const original = GetProcessWindowStation();
			HWINSTA const station = CreateWindowStationW(nullptr, 0, WINSTA_ALL_ACCESS, nullptr);

			REQUIRE(station != nullptr);
			REQUIRE(SetProcessWindowStation(station));

			HDESK const desktop = CreateDesktopW(L"Loom Images", nullptr, nullptr, 0, GENERIC_ALL, nullptr);
			REQUIRE(desktop != nullptr);
			REQUIRE(SetThreadDesktop(desktop));

			check();

			SetProcessWindowStation(original);
			CloseDesktop(desktop);
			CloseWindowStation(station);
		});

		worker.join();
	};
};

#pragma endregion

TEST_SUITE("Loom Images")
{
	TEST_CASE("base64 matches the standard vectors and reads back what it wrote")
	{
		CHECK(Images::Base64("") == "");
		CHECK(Images::Base64("f") == "Zg==");
		CHECK(Images::Base64("fo") == "Zm8=");
		CHECK(Images::Base64("foo") == "Zm9v");
		CHECK(Images::Base64("foobar") == "Zm9vYmFy");

		const std::string bytes("\x00\xFF\x10\x80 binary\n", binary_length);

		CHECK(Images::FromBase64(Images::Base64(bytes)) == bytes);
	};

	TEST_CASE("base64 decoding skips what is not a digit, such as line breaks")
	{
		CHECK(Images::FromBase64("Zm9v\r\nYmFy") == "foobar");
		CHECK(Images::FromBase64("") == "");
	};

	TEST_CASE("a hash name is sixteen hex digits of FNV-1a, the same for the same text")
	{
		CHECK(Images::HashName("") == "cbf29ce484222325");
		CHECK(Images::HashName("a") == "af63dc4c8601ec8c");
		CHECK(Images::HashName("same") == Images::HashName("same"));
		CHECK(Images::HashName("same") != Images::HashName("other"));
	};

	TEST_CASE("the media type comes from the bytes' mark, and its extension from the type")
	{
		CHECK(Images::MediaOf(Png(small_side, small_side)) == Images::PngMedia);
		CHECK(Images::MediaOf(Images::Encode(Solid(small_side, small_side), Images::Format::Jpeg)) == Images::JpegMedia);
		CHECK(Images::MediaOf("GIF89a") == "image/gif");
		CHECK(Images::MediaOf(std::string_view("RIFF\0\0\0\0WEBPVP8 ", webp_length)) == "image/webp");
		CHECK(Images::MediaOf("RIFF").empty());
		CHECK(Images::MediaOf("just some text").empty());
		CHECK(Images::MediaOf("").empty());

		CHECK(Images::ExtensionOf(Images::PngMedia) == ".png");
		CHECK(Images::ExtensionOf(Images::JpegMedia) == ".jpg");
		CHECK(Images::ExtensionOf("image/gif") == ".gif");
		CHECK(Images::ExtensionOf("image/webp") == ".webp");
		CHECK(Images::ExtensionOf("") == ".png");
	};

	TEST_CASE("an image's extension says so, in any case")
	{
		CHECK(Images::IsImageFile("a.png"));
		CHECK(Images::IsImageFile("C:/b c/d.JPEG"));
		CHECK(Images::IsImageFile("e.Webp"));
		CHECK(Images::IsImageFile("f.bmp"));
		CHECK_FALSE(Images::IsImageFile("g.txt"));
		CHECK_FALSE(Images::IsImageFile("png"));
		CHECK_FALSE(Images::IsImageFile(""));
	};

	TEST_CASE("an image grows or shrinks to the side of its space that binds first")
	{
		constexpr float low_width = 40;
		constexpr float low_height = 20;
		constexpr float space_width = 400;
		constexpr float space_height = 100;
		constexpr float grown = 5;
		constexpr float shrunk = 0.5f;

		CHECK(Images::Scale(low_width, low_height, space_width, space_height) == grown);
		CHECK(Images::Scale(space_width * 2, low_height, space_width, space_height) == shrunk);
	};

	TEST_CASE("a png encodes and decodes to the very same pixels")
	{
		const Images::Pixels pattern = Pattern();
		const std::string bytes = Images::Encode(pattern, Images::Format::Png);

		REQUIRE(!bytes.empty());

		Images::Pixels pixels;
		std::string error;
		int width = 0;
		int height = 0;

		REQUIRE(Images::DecodeBytes(bytes, large_side, pixels, error));
		CHECK(error.empty());
		CHECK(pixels.width == pattern_width);
		CHECK(pixels.height == pattern_height);
		CHECK(pixels.rgba == pattern.rgba);

		REQUIRE(Images::MeasureBytes(bytes, width, height, error));
		CHECK(width == pattern_width);
		CHECK(height == pattern_height);
	};

	TEST_CASE("a jpeg encodes and decodes to the same size and near the same colour")
	{
		const std::string bytes = Images::Encode(Solid(wide_width, wide_height), Images::Format::Jpeg);

		REQUIRE(!bytes.empty());

		Images::Pixels pixels;
		std::string error;

		REQUIRE(Images::DecodeBytes(bytes, large_side, pixels, error));
		REQUIRE(pixels.width == wide_width);
		REQUIRE(pixels.height == wide_height);
		REQUIRE(pixels.rgba.size() == (size_t)wide_width * wide_height * channels);

		const size_t middle = ((size_t)wide_height / 2 * wide_width + wide_width / 2) * channels;

		CHECK(std::abs(pixels.rgba[middle] - red) <= jpeg_tolerance);
		CHECK(std::abs(pixels.rgba[middle + 1] - green) <= jpeg_tolerance);
		CHECK(std::abs(pixels.rgba[middle + 2] - blue) <= jpeg_tolerance);
		CHECK(pixels.rgba[middle + 3] == opaque);
	};

	TEST_CASE("decoding takes the longest side down to the limit, keeping the shape, and never up")
	{
		const std::string wide = Png(wide_width, wide_height);
		const std::string line = Png(line_length, 1);
		Images::Pixels pixels;
		std::string error;

		REQUIRE(Images::DecodeBytes(wide, halved_side, pixels, error));
		CHECK(pixels.width == halved_side);
		CHECK(pixels.height == wide_height * halved_side / wide_width);
		CHECK(pixels.rgba.size() == (size_t)pixels.width * pixels.height * channels);

		REQUIRE(Images::DecodeBytes(wide, large_side, pixels, error));
		CHECK(pixels.width == wide_width);
		CHECK(pixels.height == wide_height);

		REQUIRE(Images::DecodeBytes(line, line_side, pixels, error));
		CHECK(pixels.width == line_side);
		CHECK(pixels.height == 1);
	};

	TEST_CASE("one decoder decodes image after image as a lone decode does, and refuses what is not an image")
	{
		const Images::ImageDecoder decoder;
		const std::string wide = Png(wide_width, wide_height);
		const std::string pattern = Images::Encode(Pattern(), Images::Format::Png);
		Images::Pixels pixels;
		Images::Pixels alone;
		std::string error;

		REQUIRE(decoder.Decode(wide, halved_side, pixels, error));
		REQUIRE(Images::DecodeBytes(wide, halved_side, alone, error));
		CHECK(pixels.width == alone.width);
		CHECK(pixels.height == alone.height);
		CHECK(pixels.rgba == alone.rgba);

		REQUIRE(decoder.Decode(pattern, large_side, pixels, error));
		CHECK(pixels.width == Pattern().width);
		CHECK(pixels.rgba == Pattern().rgba);

		CHECK_FALSE(decoder.Decode("not an image", large_side, pixels, error));
		CHECK_FALSE(error.empty());

		REQUIRE(decoder.Decode(wide, large_side, pixels, error));
		CHECK(pixels.width == wide_width);
	};

	TEST_CASE("resizing keeps a solid colour and gives the size asked for")
	{
		const Images::Pixels solid = Solid(wide_width, wide_height);
		Images::Pixels resized;

		REQUIRE(Images::Resize(solid, halved_side, wide_height / 2, resized));
		CHECK(resized.width == halved_side);
		CHECK(resized.height == wide_height / 2);
		REQUIRE(resized.rgba.size() == (size_t)halved_side * (wide_height / 2) * channels);
		CHECK(resized.rgba[0] == red);
		CHECK(resized.rgba[1] == green);
		CHECK(resized.rgba[2] == blue);
	};

	TEST_CASE("enlarging repeats each pixel across and down, and a factor below one changes nothing")
	{
		const Images::Pixels pattern = Pattern();
		const Images::Pixels doubled = Images::Enlarged(pattern, small_side);

		REQUIRE(doubled.width == pattern.width * small_side);
		REQUIRE(doubled.height == pattern.height * small_side);

		for (int y = 0; y < doubled.height; ++y)
			for (int x = 0; x < doubled.width; ++x)
				for (size_t channel = 0; channel < channels; ++channel)
					CHECK(doubled.rgba[((size_t)y * doubled.width + x) * channels + channel]
						== pattern.rgba[((size_t)(y / small_side) * pattern.width + x / small_side) * channels + channel]);

		CHECK(Images::Enlarged(pattern, 0).rgba == pattern.rgba);
		CHECK(Images::Enlarged(pattern, -small_side).width == pattern.width);
	};

	TEST_CASE("measuring reads the full size, whatever a decode would scale to")
	{
		int width = 0;
		int height = 0;
		std::string error;

		REQUIRE(Images::MeasureBytes(Png(wide_width, wide_height), width, height, error));
		CHECK(width == wide_width);
		CHECK(height == wide_height);
	};

	TEST_CASE("a file outside ascii reads, decodes and measures by its utf-8 path")
	{
		const Scratch scratch;
		const std::string name = "caf\xC3\xA9 shot.png";
		const std::filesystem::path file = scratch.path / Text::Utf8Path(name);
		const std::string png = Png(wide_width, wide_height);

		Write(file, png);

		CHECK(Text::PathUtf8(file.filename()) == name);
		CHECK(Text::Utf8Path(Text::PathUtf8(file)) == file);

		std::string bytes;
		std::string error;
		Images::Pixels pixels;
		int width = 0;
		int height = 0;

		REQUIRE(Images::ReadFile(file, bytes, error));
		CHECK(bytes == png);

		REQUIRE(Images::Decode(file, halved_side, pixels, error));
		CHECK(pixels.width == halved_side);

		REQUIRE(Images::Measure(file, width, height, error));
		CHECK(width == wide_width);
		CHECK(height == wide_height);
		CHECK(error.empty());
	};

	TEST_CASE("a missing file is refused by name")
	{
		const Scratch scratch;
		const std::filesystem::path file = scratch.path / "gone.png";
		std::string bytes;
		std::string error;
		Images::Pixels pixels;
		int width = 0;
		int height = 0;

		CHECK_FALSE(Images::ReadFile(file, bytes, error));
		CHECK(error == "Image not found: gone.png");

		error.clear();

		CHECK_FALSE(Images::Decode(file, large_side, pixels, error));
		CHECK(error == "Image not found: gone.png");

		error.clear();

		CHECK_FALSE(Images::Measure(file, width, height, error));
		CHECK(error == "Image not found: gone.png");
	};

	TEST_CASE("what is not an image is refused and says so")
	{
		const Scratch scratch;
		const std::filesystem::path file = scratch.path / "false.png";
		Images::Pixels pixels;
		std::string error;
		int width = 0;
		int height = 0;

		Write(file, "just some text");

		CHECK_FALSE(Images::DecodeBytes("just some text", large_side, pixels, error));
		CHECK(error == "Not an image.");

		CHECK_FALSE(Images::MeasureBytes("", width, height, error));
		CHECK(error == "Not an image.");

		CHECK_FALSE(Images::Decode(file, large_side, pixels, error));
		CHECK(error == "Not an image: false.png");

		CHECK_FALSE(Images::Measure(file, width, height, error));
		CHECK(error == "Not an image: false.png");
	};

	TEST_CASE("pixels with no size encode to nothing")
	{
		CHECK(Images::Encode(Images::Pixels{ }, Images::Format::Png).empty());
		CHECK(Images::Encode(Images::Pixels{ }, Images::Format::Jpeg).empty());
	};

	TEST_CASE("an empty clipboard holds no image")
	{
		OnPrivateClipboard([]()
		{
			REQUIRE(OpenClipboard(nullptr));
			EmptyClipboard();
			CloseClipboard();

			const Images::ClipboardImages pasted = Images::ReadClipboard();

			CHECK(pasted.bytes.empty());
			CHECK(pasted.files.empty());
		});
	};

	TEST_CASE("a pasted png comes as it is, with or without text beside it")
	{
		const std::string png = Png(small_side, small_side);

		OnPrivateClipboard([&png]()
		{
			constexpr char text[] = "words";

			REQUIRE(OpenClipboard(nullptr));
			EmptyClipboard();
			PutClipboard(RegisterClipboardFormatW(L"PNG"), png);
			CloseClipboard();

			CHECK(std::string_view(Images::ReadClipboard().bytes).starts_with(png));

			REQUIRE(OpenClipboard(nullptr));
			PutClipboard(CF_TEXT, std::string(text, sizeof(text)));
			CloseClipboard();

			CHECK(std::string_view(Images::ReadClipboard().bytes).starts_with(png));
		});
	};

	TEST_CASE("a pasted bitmap reads as a bitmap file that decodes to its size")
	{

		BITMAPINFOHEADER info{ };
		info.biSize = sizeof(info);
		info.biWidth = pattern_width;
		info.biHeight = pattern_height;
		info.biPlanes = 1;
		info.biBitCount = bitmap_depth;

		std::string dib((const char*)&info, sizeof(info));
		dib.resize(sizeof(info) + (size_t)pattern_width * pattern_height * channels, '\x40');

		OnPrivateClipboard([&dib]()
		{
			REQUIRE(OpenClipboard(nullptr));
			EmptyClipboard();
			PutClipboard(CF_DIB, dib);
			CloseClipboard();

			const std::string pasted = Images::ReadClipboard().bytes;

			REQUIRE(pasted.size() >= sizeof(BITMAPFILEHEADER));

			BITMAPFILEHEADER header{ };
			std::memcpy(&header, pasted.data(), sizeof(header));

			CHECK(header.bfType == bitmap_mark);
			CHECK(header.bfOffBits == sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER));

			int width = 0;
			int height = 0;
			std::string error;

			REQUIRE(Images::MeasureBytes(pasted, width, height, error));
			CHECK(width == pattern_width);
			CHECK(height == pattern_height);
		});
	};

	TEST_CASE("files on the clipboard come by their paths")
	{
		OnPrivateClipboard([]()
		{
			constexpr wchar_t list[] = L"C:\\a b.png\0C:\\caf\u00E9.txt\0";

			DROPFILES header{ };
			header.pFiles = sizeof(header);
			header.fWide = TRUE;

			std::string bytes((const char*)&header, sizeof(header));
			bytes.append((const char*)list, sizeof(list));

			REQUIRE(OpenClipboard(nullptr));
			EmptyClipboard();
			PutClipboard(CF_HDROP, bytes);
			CloseClipboard();

			CHECK(Images::ReadClipboard().files == std::vector<std::string>{ "C:\\a b.png", "C:\\caf\xC3\xA9.txt" });
		});
	};
};
