#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>


namespace Loom::Images
{
	/**
	* Loom::Images
	* - Decodes, measures, scales and encodes images through Windows Imaging, from memory or a file
	* - Reads images off the clipboard, picks them with the file dialog and opens them in the shell
	* - Pixels are 8-bit RGBA, and a decode takes the longest side down to a limit when it is over
	*/
	constexpr std::string_view PngMedia = "image/png";
	constexpr std::string_view JpegMedia = "image/jpeg";

	struct Pixels
	{
		int width = 0;
		int height = 0;
		std::vector<uint8_t> rgba;
	};

	struct ClipboardImages
	{
		std::string bytes;
		std::vector<std::string> files;
	};

	enum class Format
	{
		Png,
		Jpeg,
	};

	bool IsImageFile(std::string_view path);
	float Scale(float width, float height, float spaceWidth, float spaceHeight);

	std::string Base64(std::string_view bytes);
	std::string FromBase64(std::string_view text);
	std::string HashName(std::string_view text);

	// The media type of the PNG, JPEG, GIF or WebP in the bytes, empty for anything else.
	std::string_view MediaOf(std::string_view bytes);

	// The file extension that goes with a media type, .png when it is none of the others.
	std::string_view ExtensionOf(std::string_view media);

	// The media type of a file extension with its dot, in any case: PNG, JPEG, GIF, WebP, MP4 and WebM. Empty for any other.
	std::string_view MediaOfExtension(std::string_view extension);

	bool ReadFile(const std::filesystem::path& file, std::string& bytes, std::string& error);

	bool Decode(const std::filesystem::path& file, int side, Pixels& pixels, std::string& error);
	bool DecodeBytes(const std::string& bytes, int side, Pixels& pixels, std::string& error);
	bool Measure(const std::filesystem::path& file, int& width, int& height, std::string& error);
	bool MeasureBytes(const std::string& bytes, int& width, int& height, std::string& error);

	// One imaging factory for a run of decodes, so each image skips making its own.
	class ImageDecoder final
	{
	public:
		ImageDecoder();
		~ImageDecoder();

		ImageDecoder(const ImageDecoder&) = delete;
		ImageDecoder& operator=(const ImageDecoder&) = delete;

		bool Decode(const std::string& bytes, int side, Pixels& pixels, std::string& error) const;

	private:
		struct State;

		std::unique_ptr<State> m_state;
	};

	// The pixels scaled to the size with Fant interpolation, which keeps the whole image.
	bool Resize(const Pixels& source, int width, int height, Pixels& pixels);

	// Each pixel repeated factor times across and down. A factor below 1 is 1.
	Pixels Enlarged(const Pixels& source, int factor);

	// Empty when the pixels could not be encoded.
	std::string Encode(const Pixels& pixels, Format format);

	ClipboardImages ReadClipboard();

	std::vector<std::string> Pick(void* owner);

	void Open(const std::string& path);
};
