#include "Text.h"

#include <cctype>
#include <chrono>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>


namespace Loom::Text
{
	namespace
	{
		constexpr size_t millisecond_digits = 3;
		constexpr int64_t seconds_per_minute = 60;
		constexpr int64_t seconds_per_hour = 3600;
		constexpr int64_t ms_per_second = 1000;
	};

	std::string Trim(
		std::string_view text,
		std::string_view blanks)
	{
		const size_t first = text.find_first_not_of(blanks);

		if (first == std::string_view::npos)
			return { };

		return std::string(text.substr(first, text.find_last_not_of(blanks) - first + 1));
	};

	std::string Lower(std::string_view text)
	{
		std::string lower(text);

		for (char& c : lower)
			c = (char)std::tolower((unsigned char)c);

		return lower;
	};

	std::optional<int64_t> ParseTime(const std::string& text)
	{
		size_t at = 0;

		const auto number = [&text, &at](size_t digits) -> std::optional<int64_t>
		{
			if (at + digits > text.size())
				return std::nullopt;

			int64_t value = 0;

			for (size_t i = 0; i < digits; ++i)
			{
				if (!std::isdigit((unsigned char)text[at + i]))
					return std::nullopt;

				value = value * 10 + (text[at + i] - '0');
			};

			at += digits;

			return value;
		};

		const auto expect = [&text, &at](char c)
		{
			if (at >= text.size() || text[at] != c)
				return false;

			++at;

			return true;
		};

		constexpr size_t year_digits = 4;
		constexpr size_t pair_digits = 2;

		const std::optional<int64_t> year = number(year_digits);

		if (!year || !expect('-'))
			return std::nullopt;

		const std::optional<int64_t> month = number(pair_digits);

		if (!month || !expect('-'))
			return std::nullopt;

		const std::optional<int64_t> day = number(pair_digits);

		if (!day || !(expect('T') || expect(' ')))
			return std::nullopt;

		const std::optional<int64_t> hour = number(pair_digits);

		if (!hour || !expect(':'))
			return std::nullopt;

		const std::optional<int64_t> minute = number(pair_digits);

		if (!minute || !expect(':'))
			return std::nullopt;

		const std::optional<int64_t> second = number(pair_digits);

		if (!second)
			return std::nullopt;

		int64_t millis = 0;

		if (expect('.'))
		{
			size_t digits = 0;
			int64_t scale = ms_per_second / 10;

			while (at < text.size() && std::isdigit((unsigned char)text[at]))
			{
				if (digits < millisecond_digits)
				{
					millis += (text[at] - '0') * scale;
					scale /= 10;
				};

				++digits;
				++at;
			};
		};

		int64_t offset = 0;

		if (at < text.size() && (text[at] == '+' || text[at] == '-'))
		{
			const int64_t sign = text[at] == '+' ? 1 : -1;

			++at;

			const std::optional<int64_t> offset_hours = number(pair_digits);

			expect(':');

			const std::optional<int64_t> offset_minutes = number(pair_digits);

			if (!offset_hours || !offset_minutes)
				return std::nullopt;

			offset = sign * (*offset_hours * seconds_per_hour + *offset_minutes * seconds_per_minute);
		}
		else
			expect('Z');

		const std::chrono::year_month_day date{ std::chrono::year((int)*year), std::chrono::month((unsigned)*month), std::chrono::day((unsigned)*day) };

		if (!date.ok())
			return std::nullopt;

		const int64_t days = std::chrono::sys_days(date).time_since_epoch().count();
		const int64_t seconds = days * 24 * seconds_per_hour + *hour * seconds_per_hour + *minute * seconds_per_minute + *second - offset;

		return seconds * ms_per_second + millis;
	};


	std::string Narrow(std::wstring_view text)
	{
		const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0, nullptr, nullptr);
		std::string narrow((size_t)length, '\0');
		WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), narrow.data(), length, nullptr, nullptr);

		return narrow;
	};

	std::wstring Wide(std::string_view text)
	{
		const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0);
		std::wstring wide((size_t)length, L'\0');
		MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), wide.data(), length);

		return wide;
	};

	std::filesystem::path Utf8Path(std::string_view text)
	{
		return std::filesystem::path(std::u8string_view((const char8_t*)text.data(), text.size()));
	};

	std::string PathUtf8(const std::filesystem::path& path)
	{
		const std::u8string text = path.u8string();

		return std::string((const char*)text.data(), text.size());
	};
};
