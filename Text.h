#pragma once

#include <compare>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>


namespace Loom::Text
{
	/**
	* Loom::Text
	* - Small string helpers the editor and its extensions keep needing
	*/

	constexpr std::string_view whitespace = " \t\n\v\f\r";

	// The text without the blank characters at either end.
	std::string Trim(
		std::string_view text,
		std::string_view blanks = whitespace);

	// ASCII letters in lower case.
	std::string Lower(std::string_view text);

	// Orders by ASCII letters in lower case, a shorter text first when it is the start of the other.
	std::strong_ordering CompareNoCase(
		std::string_view a,
		std::string_view b);

	bool StartsWithNoCase(
		std::string_view text,
		std::string_view prefix);

	// False for empty text, even when the needle is empty too.
	bool ContainsNoCase(
		std::string_view text,
		std::string_view needle);

	// A path that starts with a slash or backslash, or with a drive letter, a colon and a slash or backslash.
	bool IsAbsolutePath(std::string_view path);

	// Milliseconds since the Unix epoch of an ISO 8601 time as "2025-01-31T23:59:59.123Z", with a space or a "T" before the time,
	// optional fraction and a "Z" or "+hh:mm" offset. Nothing when it reads otherwise or is no date.
	std::optional<int64_t> ParseTime(const std::string& text);

	// UTF-16 to UTF-8.
	std::string Narrow(std::wstring_view text);

	// UTF-8 to UTF-16.
	std::wstring Wide(std::string_view text);

	// A path read from or written as UTF-8, whatever the code page.
	std::filesystem::path Utf8Path(std::string_view text);
	std::string PathUtf8(const std::filesystem::path& path);
};
