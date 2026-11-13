#include "LayoutIni.h"

#include <algorithm>
#include <cstddef>
#include <utility>


namespace Loom::LayoutIni
{
	namespace
	{
		constexpr std::string_view windows_ending = "\r\n";
		constexpr std::string_view unix_ending = "\n";

		Lines SplitLines(const std::string& text)
		{
			Lines lines;
			size_t start = 0;

			while (start < text.size())
			{
				const size_t end = std::min(text.find('\n', start), text.size());
				std::string line = text.substr(start, end - start);

				if (line.ends_with('\r'))
					line.pop_back();

				lines.push_back(std::move(line));
				start = end + 1;
			};

			return lines;
		};

		void TrimBlank(Lines& lines)
		{
			while (!lines.empty() && lines.back().empty())
				lines.pop_back();
		};
	};

	std::string_view Section::Header() const
	{
		return !lines.empty() && lines.front().starts_with('[') ? std::string_view(lines.front()) : std::string_view();
	};

	std::vector<Section> Split(const std::string& text)
	{
		std::vector<Section> sections;

		for (const std::string& line : SplitLines(text))
		{
			if (sections.empty() || line.starts_with('['))
				sections.emplace_back();

			sections.back().lines.push_back(line);
		};

		return sections;
	};

	std::string_view EndingOf(const std::string& text)
	{
		return text.find(windows_ending) != std::string::npos ? windows_ending : unix_ending;
	};

	std::string Merge(
		const std::vector<Section>& sections,
		std::string_view ending)
	{
		std::string merged;

		for (const Section& section : sections)
			for (const std::string& line : section.lines)
			{
				merged += line;
				merged += ending;
			};

		return merged;
	};

	bool Named(
		const std::vector<Section>& sections,
		std::string_view header)
	{
		return std::ranges::any_of(sections, [header](const Section& section) { return section.Header() == header; });
	};

	Section Rehead(
		const Section& from,
		std::string_view header)
	{
		Section section = from;

		section.lines.front() = header;
		TrimBlank(section.lines);
		section.lines.emplace_back();

		return section;
	};
};
