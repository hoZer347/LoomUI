#include "doctest.h"

#include "TextLayout.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

using namespace Loom::TextLayout;

#pragma region Helpers

namespace
{
	constexpr float letter = 10;
	constexpr float line = 20;
	constexpr float indent = 15;
	constexpr float wide = 1000;
	constexpr size_t row_bytes = 5;

	float Width(std::string_view text)
	{
		float width = 0;

		for (size_t at = 0; at < text.size(); at += CodePointLength(text[at]))
			width += letter;

		return width;
	};

	size_t Rows(std::string_view text)
	{
		return std::min(row_bytes, text.size());
	};

	std::string Slice(
		std::string_view text,
		const Run& run)
	{
		return std::string(text.substr(run.begin, run.end - run.begin));
	};

	std::string Slice(
		std::string_view text,
		const Piece& piece)
	{
		return std::string(text.substr(piece.begin, piece.end - piece.begin));
	};
};

#pragma endregion

TEST_SUITE("Loom TextLayout")
{
	#pragma region Parse

	TEST_CASE("a code point's length is read from its lead byte")
	{
		CHECK(CodePointLength('a') == 1);
		CHECK(CodePointLength((char)0xC3) == 2);
		CHECK(CodePointLength((char)0xE2) == 3);
		CHECK(CodePointLength((char)0xF0) == 4);
		CHECK(CodePointLength((char)0x80) == 1);
	};

	TEST_CASE("a quote loses its marker and takes the quote style")
	{
		const std::string text = "> quoted";
		const std::vector<Paragraph> paragraphs = ParseMarkdown(text);

		REQUIRE(paragraphs.size() == 1);
		REQUIRE(paragraphs[0].runs.size() == 1);
		CHECK(paragraphs[0].runs[0].style == Style::Quote);
		CHECK(Slice(text, paragraphs[0].runs[0]) == "quoted");
	};

	TEST_CASE("a heading needs one to six hashes and a space")
	{
		const std::string six = "###### six";
		const std::string seven = "####### seven";
		const std::string tag = "#tag";

		const std::vector<Paragraph> heading = ParseMarkdown(six);
		const std::vector<Paragraph> tooDeep = ParseMarkdown(seven);
		const std::vector<Paragraph> unspaced = ParseMarkdown(tag);

		REQUIRE(heading.size() == 1);
		REQUIRE(heading[0].runs.size() == 1);
		CHECK(heading[0].runs[0].style == Style::Strong);
		CHECK(Slice(six, heading[0].runs[0]) == "six");

		REQUIRE(tooDeep.size() == 1);
		REQUIRE(tooDeep[0].runs.size() == 1);
		CHECK(tooDeep[0].runs[0].style == Style::Plain);
		CHECK(Slice(seven, tooDeep[0].runs[0]) == seven);

		REQUIRE(unspaced.size() == 1);
		REQUIRE(unspaced[0].runs.size() == 1);
		CHECK(unspaced[0].runs[0].style == Style::Plain);
		CHECK(Slice(tag, unspaced[0].runs[0]) == tag);
	};

	TEST_CASE("code in a heading keeps the code style")
	{
		const std::string text = "# a `b`";
		const std::vector<Paragraph> paragraphs = ParseMarkdown(text);

		REQUIRE(paragraphs.size() == 1);
		REQUIRE(paragraphs[0].runs.size() == 2);
		CHECK(paragraphs[0].runs[0].style == Style::Strong);
		CHECK(Slice(text, paragraphs[0].runs[0]) == "a ");
		CHECK(paragraphs[0].runs[1].style == Style::Code);
		CHECK(Slice(text, paragraphs[0].runs[1]) == "b");
	};

	TEST_CASE("a rule ends a paragraph and leaves a gap")
	{
		const std::string text = "one\n---\ntwo";
		const std::vector<Paragraph> paragraphs = ParseMarkdown(text);

		REQUIRE(paragraphs.size() == 3);
		CHECK(Slice(text, paragraphs[0].runs.at(0)) == "one");
		CHECK(paragraphs[1].runs.empty());
		CHECK(Slice(text, paragraphs[2].runs.at(0)) == "two");
	};

	TEST_CASE("star and plus bullets are bullets too")
	{
		const std::string text = "* a\n+ b";
		const std::vector<Paragraph> paragraphs = ParseMarkdown(text);

		REQUIRE(paragraphs.size() == 2);
		CHECK(paragraphs[0].bullet);
		CHECK(Slice(text, paragraphs[0].runs.at(0)) == "a");
		CHECK(paragraphs[1].bullet);
		CHECK(Slice(text, paragraphs[1].runs.at(0)) == "b");
	};

	TEST_CASE("leading spaces indent a paragraph a level for every two")
	{
		const std::string text = "    deep";
		const std::vector<Paragraph> paragraphs = ParseMarkdown(text);

		REQUIRE(paragraphs.size() == 1);
		CHECK(paragraphs[0].indent == 2);
		CHECK(paragraphs[0].bullet == false);
		CHECK(Slice(text, paragraphs[0].runs.at(0)) == "deep");
	};

	TEST_CASE("a paragraph is a figure only when it is one image run")
	{
		const std::vector<Paragraph> figure = ParseMarkdown("![a](C:/b.png)");
		const std::vector<Paragraph> words = ParseMarkdown("words");
		const Paragraph mixed = { { { 0, 1, Style::Image }, { 1, 2, Style::Plain } } };

		REQUIRE(figure.size() == 1);
		CHECK(IsFigure(figure[0]));
		REQUIRE(words.size() == 1);
		CHECK(IsFigure(words[0]) == false);
		CHECK(IsFigure(mixed) == false);
		CHECK(IsFigure(Paragraph{ }) == false);
	};

	TEST_CASE("a file link decodes its escapes, and a path or a broken escape stays as it is")
	{
		CHECK(FigureFile("C:/b.png") == "C:/b.png");
		CHECK(FigureFile("file:///C:/a%41b.png") == "C:/aAb.png");
		CHECK(FigureFile("file:///C:/a%zzb.png") == "C:/a%zzb.png");
		CHECK(FigureFile("file:///C:/b%4") == "C:/b%4");
	};

	TEST_CASE("plain lines drop their carriage returns, and a final line end leaves an empty line")
	{
		const std::string text = "+a\r\n-b\r\n";
		const std::vector<Paragraph> paragraphs = ParseLines(text, true);

		REQUIRE(paragraphs.size() == 3);
		CHECK(paragraphs[0].runs.at(0).style == Style::Added);
		CHECK(Slice(text, paragraphs[0].runs.at(0)) == "+a");
		CHECK(paragraphs[1].runs.at(0).style == Style::Removed);
		CHECK(Slice(text, paragraphs[1].runs.at(0)) == "-b");
		CHECK(paragraphs[2].runs.empty());
	};

	#pragma endregion

	#pragma region Layout

	TEST_CASE("words of one style on one line make one piece")
	{
		const std::string text = "one two three";
		const Layout layout = LayOut(text, ParseMarkdown(text), wide, line, indent, Width);

		REQUIRE(layout.pieces.size() == 1);
		CHECK(Slice(text, layout.pieces[0]) == text);
	};

	TEST_CASE("an indented paragraph starts an indent width in for each level")
	{
		const std::string text = "    deep";
		const Layout layout = LayOut(text, ParseMarkdown(text), wide, line, indent, Width);

		REQUIRE(layout.pieces.size() == 1);
		CHECK(layout.pieces[0].x == indent * 2);
	};

	TEST_CASE("each paragraph records where it starts, and a gap is half a line")
	{
		constexpr float gap = line / 2;

		const std::string text = "one\n\ntwo";
		const Layout layout = LayOut(text, ParseMarkdown(text), wide, line, indent, Width);

		REQUIRE(layout.starts.size() == 3);
		CHECK(layout.starts[0].piece == 0);
		CHECK(layout.starts[0].y == 0);
		CHECK(layout.starts[1].piece == 1);
		CHECK(layout.starts[1].y == line);
		CHECK(layout.starts[2].piece == 1);
		CHECK(layout.starts[2].y == line + gap);
		REQUIRE(layout.pieces.size() == 2);
		CHECK(layout.pieces[1].y == line + gap);
		CHECK(layout.height == line * 2 + gap);
	};

	TEST_CASE("a figure with no height given takes a line")
	{
		const std::string text = "![a](C:/b.png)\ntwo";
		const Layout layout = LayOut(text, ParseMarkdown(text), wide, line, indent, Width);

		REQUIRE(layout.pieces.size() == 2);
		CHECK(layout.pieces[0].style == Style::Image);
		CHECK(layout.pieces[1].y == line);
		CHECK(layout.height == line * 2);
	};

	TEST_CASE("a figure's height is asked with its path and the width")
	{
		constexpr float figure = 100;

		const std::string text = "![a](C:/b.png)";
		std::string askedPath;
		float askedWidth = 0;

		const FigureHeight height = [&askedPath, &askedWidth](
			std::string_view path,
			float width)
		{
			askedPath = path;
			askedWidth = width;

			return figure;
		};

		const Layout layout = LayOut(text, ParseMarkdown(text), wide, line, indent, Width, height);

		CHECK(askedPath == "C:/b.png");
		CHECK(askedWidth == wide);
		CHECK(layout.height == figure);
	};

	TEST_CASE("extending from the last paragraph or past the end leaves the layout as it was")
	{
		const std::string text = "one\n\ntwo three";
		const std::vector<Paragraph> paragraphs = ParseMarkdown(text);
		const Layout layout = LayOut(text, paragraphs, wide, line, indent, Width);

		Layout extended = layout;
		Extend(extended, text, paragraphs, paragraphs.size() - 1, wide, line, indent, Width);

		CHECK(extended.pieces == layout.pieces);
		CHECK(extended.starts.size() == layout.starts.size());
		CHECK(extended.height == layout.height);

		Extend(extended, text, paragraphs, paragraphs.size(), wide, line, indent, Width);

		CHECK(extended.pieces == layout.pieces);
		CHECK(extended.starts.size() == layout.starts.size());
		CHECK(extended.height == layout.height);
	};

	#pragma endregion

	#pragma region Input

	TEST_CASE("up and down in empty text stay at its start")
	{
		const InputCaret start{ };

		CHECK(MoveRow("", start, RowStep::Up, Rows, Width) == start);
		CHECK(MoveRow("", start, RowStep::Down, Rows, Width) == start);
	};

	#pragma endregion
};
