#pragma once

#include <string>
#include <string_view>
#include <vector>


namespace Loom::LayoutIni
{
	using Lines = std::vector<std::string>;

	// A bracketed header line and the lines after it. The lines before the first header are a section with no header.
	struct Section final
	{
		Lines lines;

		std::string_view Header() const;
	};

	// The imgui.ini text split into sections, its line ends dropped.
	std::vector<Section> Split(const std::string& text);

	// The line end the text uses: CRLF when it holds one, else LF.
	std::string_view EndingOf(const std::string& text);

	// The sections joined again, each line ending in ending.
	std::string Merge(
		const std::vector<Section>& sections,
		std::string_view ending);

	bool Named(
		const std::vector<Section>& sections,
		std::string_view header);

	// A copy of a section under another header, its trailing blank lines dropped and one blank line after it.
	Section Rehead(
		const Section& from,
		std::string_view header);
};
