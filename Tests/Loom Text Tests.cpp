#include "doctest.h"

#include "Text.h"

#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

using namespace Loom;

#pragma region Helpers

namespace
{
	constexpr const char* session_blanks = " \t\r\n";
	constexpr const char* utf8_text = "caf\xC3\xA9 \xE2\x82\xAC \xF0\x9F\x98\x80";
	constexpr const wchar_t* wide_text = L"caf\u00E9 \u20AC \U0001F600";
	constexpr int64_t known_ms = 1738367999123;
	constexpr int64_t known_whole_ms = 1738367999000;
	constexpr int64_t hour_ms = 3600000;
	constexpr int64_t half_hour_ms = 1800000;
};

#pragma endregion

TEST_SUITE("Loom Text")
{
	#pragma region Trim and Lower

	TEST_CASE("trim strips every kind of whitespace at both ends and keeps it inside")
	{
		CHECK(Text::Trim(" \t\n\v\f\rin side\t \r\n") == "in side");
		CHECK(Text::Trim("plain") == "plain");
	};

	TEST_CASE("trim of empty or blank text is empty")
	{
		CHECK(Text::Trim("").empty());
		CHECK(Text::Trim(" \t\r\n\v\f").empty());
	};

	TEST_CASE("trim with its own blanks strips only those")
	{
		CHECK(Text::Trim("\v\r\n text \t\f", session_blanks) == "\v\r\n text \t\f");
		CHECK(Text::Trim("\r\n text \t", session_blanks) == "text");
		CHECK(Text::Trim("--x--", "-") == "x");
	};

	TEST_CASE("lower changes ASCII letters only")
	{
		CHECK(Text::Lower("MiXeD Case 123_!") == "mixed case 123_!");
		CHECK(Text::Lower("\xC3\x89") == "\xC3\x89");
		CHECK(Text::Lower("").empty());
	};

	#pragma endregion

	#pragma region ParseTime

	TEST_CASE("parse time reads a UTC time with milliseconds")
	{
		CHECK(Text::ParseTime("1970-01-01T00:00:00Z") == 0);
		CHECK(Text::ParseTime("2025-01-31T23:59:59.123Z") == known_ms);
	};

	TEST_CASE("parse time takes a space before the time and no zone as UTC")
	{
		CHECK(Text::ParseTime("2025-01-31 23:59:59.123") == known_ms);
		CHECK(Text::ParseTime("2025-01-31T23:59:59") == known_whole_ms);
	};

	TEST_CASE("parse time reads a short fraction as tenths and drops digits past milliseconds")
	{
		CHECK(Text::ParseTime("2025-01-31T23:59:59.5Z") == known_whole_ms + 500);
		CHECK(Text::ParseTime("2025-01-31T23:59:59.123456789Z") == known_ms);
		CHECK(Text::ParseTime("2025-01-31T23:59:59.Z") == known_whole_ms);
	};

	TEST_CASE("parse time applies an offset with or without its colon")
	{
		CHECK(Text::ParseTime("2025-01-31T23:59:59.123+01:00") == known_ms - hour_ms);
		CHECK(Text::ParseTime("2025-01-31T23:59:59.123-0030") == known_ms + half_hour_ms);
	};

	TEST_CASE("parse time rejects an offset without minutes")
	{
		CHECK_FALSE(Text::ParseTime("2025-01-31T23:59:59+01").has_value());
	};

	TEST_CASE("parse time rejects a date that does not exist")
	{
		CHECK_FALSE(Text::ParseTime("2025-02-29T00:00:00Z").has_value());
		CHECK_FALSE(Text::ParseTime("2025-13-01T00:00:00Z").has_value());
		CHECK(Text::ParseTime("2024-02-29T00:00:00Z").has_value());
	};

	TEST_CASE("parse time rejects text that is not a time")
	{
		CHECK_FALSE(Text::ParseTime("").has_value());
		CHECK_FALSE(Text::ParseTime("yesterday").has_value());
		CHECK_FALSE(Text::ParseTime("2025-01-31").has_value());
		CHECK_FALSE(Text::ParseTime("2025-01-31T23:59").has_value());
		CHECK_FALSE(Text::ParseTime("2025/01/31T23:59:59Z").has_value());
		CHECK_FALSE(Text::ParseTime("2025-01-31X23:59:59Z").has_value());
		CHECK_FALSE(Text::ParseTime("2025-1-31T23:59:59Z").has_value());
	};

	#pragma endregion

	#pragma region Narrow and Wide

	TEST_CASE("narrow and wide convert UTF-8 and UTF-16 both ways")
	{
		CHECK(Text::Narrow(wide_text) == utf8_text);
		CHECK(Text::Wide(utf8_text) == wide_text);
		CHECK(Text::Narrow(Text::Wide(utf8_text)) == utf8_text);
	};

	TEST_CASE("narrow and wide of empty text are empty")
	{
		CHECK(Text::Narrow(L"").empty());
		CHECK(Text::Wide("").empty());
	};

	TEST_CASE("compare ignores the case of ASCII letters and puts a prefix first")
	{
		CHECK(std::is_eq(Text::CompareNoCase("Alpha", "aLPHA")));
		CHECK(std::is_lt(Text::CompareNoCase("apple", "Banana")));
		CHECK(std::is_gt(Text::CompareNoCase("b", "A")));
		CHECK(std::is_lt(Text::CompareNoCase("Ab", "abc")));
		CHECK(std::is_eq(Text::CompareNoCase("", "")));
	};

	TEST_CASE("starts with and contains ignore case and treat empty text as having nothing")
	{
		CHECK(Text::StartsWithNoCase("C:/Users/Me", "c:/users"));
		CHECK_FALSE(Text::StartsWithNoCase("C:/Us", "c:/users"));
		CHECK(Text::StartsWithNoCase("abc", ""));
		CHECK(Text::ContainsNoCase("Run SLOTS.py now", "slots.PY"));
		CHECK_FALSE(Text::ContainsNoCase("Run now", "slots"));
		CHECK_FALSE(Text::ContainsNoCase("", ""));
		CHECK(Text::ContainsNoCase("a", ""));
	};

	TEST_CASE("an absolute path starts at a root or at a drive's root")
	{
		CHECK(Text::IsAbsolutePath("C:\\Work\\a.png"));
		CHECK(Text::IsAbsolutePath("c:/Work/a.png"));
		CHECK(Text::IsAbsolutePath("/home/me/a.png"));
		CHECK(Text::IsAbsolutePath("\\\\server\\share\\a.png"));
		CHECK(Text::IsAbsolutePath("\\Work\\a.png"));
		CHECK_FALSE(Text::IsAbsolutePath("Work\\a.png"));
		CHECK_FALSE(Text::IsAbsolutePath("C:a.png"));
		CHECK_FALSE(Text::IsAbsolutePath("C:"));
		CHECK_FALSE(Text::IsAbsolutePath("1:\\a.png"));
		CHECK_FALSE(Text::IsAbsolutePath(""));
	};

	TEST_CASE("narrow and wide keep only the length they are given")
	{
		const std::wstring_view wide = L"abc";
		const std::string_view narrow = "abc";

		CHECK(Text::Narrow(wide.substr(0, 2)) == "ab");
		CHECK(Text::Wide(narrow.substr(1)) == L"bc");
	};

	#pragma endregion
}
