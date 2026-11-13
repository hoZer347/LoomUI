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
	constexpr size_t row_bytes = 5;

	// Every code point the same width, so positions read as character counts.
	float Width(std::string_view text)
	{
		float width = 0;

		for (size_t at = 0; at < text.size(); at += CodePointLength(text[at]))
			width += letter;

		return width;
	};

	Wrap Rows(size_t bytes)
	{
		return [bytes](std::string_view text) { return std::min(bytes, text.size()); };
	};

	std::string Slice(std::string_view text, const Run& run)
	{
		return std::string(text.substr(run.begin, run.end - run.begin));
	};

	std::string Slice(std::string_view text, const Piece& piece)
	{
		return std::string(text.substr(piece.begin, piece.end - piece.begin));
	};
};

#pragma endregion

TEST_SUITE("ParseMarkdown")
{
	TEST_CASE("bold and code become their own runs without their markers")
	{
		const std::string text = "a **b** `c` d";
		const std::vector<Paragraph> paragraphs = ParseMarkdown(text);

		REQUIRE(paragraphs.size() == 1);
		REQUIRE(paragraphs[0].runs.size() == 5);
		CHECK(Slice(text, paragraphs[0].runs[1]) == "b");
		CHECK(paragraphs[0].runs[1].style == Style::Strong);
		CHECK(Slice(text, paragraphs[0].runs[3]) == "c");
		CHECK(paragraphs[0].runs[3].style == Style::Code);
	};

	TEST_CASE("emphasis loses its markers")
	{
		const std::string text = "a *b* c";
		const std::vector<Paragraph> paragraphs = ParseMarkdown(text);

		REQUIRE(paragraphs[0].runs.size() == 3);
		CHECK(Slice(text, paragraphs[0].runs[1]) == "b");
		CHECK(paragraphs[0].runs[1].style == Style::Plain);
	};

	TEST_CASE("a spaced asterisk is multiplication")
	{
		const std::string text = "2 * 3 * 4 *5 * 6";
		const std::vector<Paragraph> paragraphs = ParseMarkdown(text);

		REQUIRE(paragraphs[0].runs.size() == 1);
		CHECK(Slice(text, paragraphs[0].runs[0]) == text);
	};

	TEST_CASE("an unclosed marker stays as text")
	{
		const std::string text = "a **b and `c";
		const std::vector<Paragraph> paragraphs = ParseMarkdown(text);

		REQUIRE(paragraphs.size() == 1);
		REQUIRE(paragraphs[0].runs.size() == 1);
		CHECK(Slice(text, paragraphs[0].runs[0]) == text);
	};

	TEST_CASE("a marker does not close on the next line")
	{
		const std::string text = "a **b\nc** d";
		const std::vector<Paragraph> paragraphs = ParseMarkdown(text);

		REQUIRE(paragraphs.size() == 2);
		CHECK(paragraphs[0].runs.size() == 1);
		CHECK(paragraphs[0].runs[0].style == Style::Plain);
	};

	TEST_CASE("a heading is strong without its hashes")
	{
		const std::string text = "## Title";
		const std::vector<Paragraph> paragraphs = ParseMarkdown(text);

		REQUIRE(paragraphs[0].runs.size() == 1);
		CHECK(Slice(text, paragraphs[0].runs[0]) == "Title");
		CHECK(paragraphs[0].runs[0].style == Style::Strong);
	};

	TEST_CASE("bullets nest by their indent")
	{
		const std::string text = "- one\n  - two";
		const std::vector<Paragraph> paragraphs = ParseMarkdown(text);

		REQUIRE(paragraphs.size() == 2);
		CHECK(paragraphs[0].bullet);
		CHECK(paragraphs[0].indent == 0);
		CHECK(paragraphs[1].bullet);
		CHECK(paragraphs[1].indent == 1);
		CHECK(Slice(text, paragraphs[1].runs[0]) == "two");
	};

	TEST_CASE("a fence keeps its lines as they are, markers included")
	{
		const std::string text = "```cpp\n**a**\n\nb\n```\nc";
		const std::vector<Paragraph> paragraphs = ParseMarkdown(text);

		REQUIRE(paragraphs.size() == 4);
		CHECK(Slice(text, paragraphs[0].runs[0]) == "**a**");
		CHECK(paragraphs[0].runs[0].style == Style::Fence);
		CHECK(paragraphs[1].runs[0].style == Style::Fence);
		CHECK(paragraphs[3].runs[0].style == Style::Plain);
	};

	TEST_CASE("blank lines collapse and do not trail")
	{
		const std::vector<Paragraph> paragraphs = ParseMarkdown("a\n\n\n\nb\n\n");

		REQUIRE(paragraphs.size() == 3);
		CHECK(paragraphs[1].runs.empty());
	};

	TEST_CASE("crlf line ends are not part of the text")
	{
		const std::string text = "a\r\nb";
		const std::vector<Paragraph> paragraphs = ParseMarkdown(text);

		REQUIRE(paragraphs.size() == 2);
		CHECK(Slice(text, paragraphs[0].runs[0]) == "a");
	};
};

TEST_SUITE("ParseLines")
{
	TEST_CASE("a diff colours its lines")
	{
		const std::vector<Paragraph> paragraphs = ParseLines("-a\n+b\nc", true);

		REQUIRE(paragraphs.size() == 3);
		CHECK(paragraphs[0].runs[0].style == Style::Removed);
		CHECK(paragraphs[1].runs[0].style == Style::Added);
		CHECK(paragraphs[2].runs[0].style == Style::Plain);
	};

	TEST_CASE("outside a diff a dash is text")
	{
		CHECK(ParseLines("-a", false)[0].runs[0].style == Style::Plain);
	};
};

TEST_SUITE("LayOut")
{
	TEST_CASE("extending from the first change matches a fresh layout")
	{
		const std::string before = "First paragraph here\n\nSecond **bold** words\n- item";
		const std::string after = before + " grown onto the end\n\nThird one";

		const std::vector<Paragraph> old = ParseMarkdown(before);
		const std::vector<Paragraph> grown = ParseMarkdown(after);

		Layout extended = LayOut(before, old, 12 * letter, line, indent, Width);

		const size_t same = (size_t)(std::ranges::mismatch(grown, old).in1 - grown.begin());

		CHECK(same > 0);
		CHECK(same < grown.size());

		Extend(extended, after, grown, same, 12 * letter, line, indent, Width);

		const Layout fresh = LayOut(after, grown, 12 * letter, line, indent, Width);

		CHECK(extended.pieces == fresh.pieces);
		CHECK(extended.height == fresh.height);
		CHECK(extended.starts.size() == fresh.starts.size());
	};

	TEST_CASE("extending from zero starts over")
	{
		const std::string text = "Words to wrap across lines";
		const std::vector<Paragraph> paragraphs = ParseMarkdown(text);

		Layout layout = LayOut(text, paragraphs, 100 * letter, line, indent, Width);

		Extend(layout, text, paragraphs, 0, 8 * letter, line, indent, Width);

		CHECK(layout.pieces == LayOut(text, paragraphs, 8 * letter, line, indent, Width).pieces);
	};

	TEST_CASE("words wrap at the width")
	{
		const std::string text = "aaa bbb ccc";
		const Layout layout = LayOut(text, ParseMarkdown(text), letter * 8, line, indent, Width);

		REQUIRE(layout.pieces.size() == 2);
		CHECK(Slice(text, layout.pieces[0]) == "aaa bbb ");
		CHECK(Slice(text, layout.pieces[1]) == "ccc");
		CHECK(layout.pieces[1].x == 0);
		CHECK(layout.pieces[1].y == line);
		CHECK(layout.height == line * 2);
	};

	TEST_CASE("a word wider than the line breaks")
	{
		const std::string text = "abcdefghij";
		const Layout layout = LayOut(text, ParseMarkdown(text), letter * 4, line, indent, Width);

		REQUIRE(layout.pieces.size() == 3);
		CHECK(Slice(text, layout.pieces[0]) == "abcd");
		CHECK(Slice(text, layout.pieces[2]) == "ij");
	};

	TEST_CASE("a break keeps code points whole")
	{
		const std::string text = "\xC3\xA9\xC3\xA9\xC3\xA9";
		const Layout layout = LayOut(text, ParseMarkdown(text), letter * 2, line, indent, Width);

		REQUIRE(layout.pieces.size() == 2);
		CHECK(Slice(text, layout.pieces[0]).size() == 4);
	};

	TEST_CASE("a width narrower than one letter still ends")
	{
		const std::string text = "ab";
		const Layout layout = LayOut(text, ParseMarkdown(text), 1, line, indent, Width);

		CHECK(layout.pieces.size() == 2);
	};

	TEST_CASE("a bullet's text hangs past its dot")
	{
		const std::string text = "- aaa bbb";
		const Layout layout = LayOut(text, ParseMarkdown(text), indent + letter * 4, line, indent, Width);

		REQUIRE(layout.pieces.size() == 3);
		CHECK(layout.pieces[0].style == Style::Bullet);
		CHECK(layout.pieces[1].x == indent);
		CHECK(layout.pieces[2].x == indent);
		CHECK(layout.pieces[2].y == line);
	};

	TEST_CASE("styled runs on a line advance past each other")
	{
		const std::string text = "ab **cd** ef";
		const Layout layout = LayOut(text, ParseMarkdown(text), letter * 100, line, indent, Width);

		REQUIRE(layout.pieces.size() == 3);
		CHECK(layout.pieces[1].x == letter * 3);
		CHECK(layout.pieces[2].x == letter * 5);
	};

	TEST_CASE("empty text lays out as nothing")
	{
		const Layout layout = LayOut("", ParseMarkdown(""), letter, line, indent, Width);

		CHECK(layout.pieces.empty());
		CHECK(layout.height == 0);
	};
};

TEST_SUITE("OffsetAt")
{
	const std::string text = "aaa bbb ccc";

	TEST_CASE("a point lands on the nearest letter of its line")
	{
		const Layout layout = LayOut(text, ParseMarkdown(text), letter * 8, line, indent, Width);

		CHECK(OffsetAt(layout, text, letter * 1.2f, line / 2, line, Width) == 1);
		CHECK(OffsetAt(layout, text, letter * 1.8f, line / 2, line, Width) == 2);
		CHECK(OffsetAt(layout, text, letter * 1.2f, line * 1.5f, line, Width) == 9);
	};

	TEST_CASE("past a line's end lands on its end")
	{
		const Layout layout = LayOut(text, ParseMarkdown(text), letter * 8, line, indent, Width);

		CHECK(OffsetAt(layout, text, letter * 50, line * 1.5f, line, Width) == text.size());
	};

	TEST_CASE("above and below land on the ends")
	{
		const Layout layout = LayOut(text, ParseMarkdown(text), letter * 8, line, indent, Width);

		CHECK(OffsetAt(layout, text, 0, -line, line, Width) == 0);
		CHECK(OffsetAt(layout, text, 0, line * 9, line, Width) == 8);
	};

	TEST_CASE("nothing laid out is offset zero")
	{
		CHECK(OffsetAt(Layout{ }, "", 0, 0, line, Width) == 0);
	};
};

TEST_SUITE("OnText")
{
	const std::string text = "aaa bbb ccc";
	const std::string bullet = "- aaa bbb";
	constexpr float first_line = line / 2;
	constexpr float second_line = line * 1.5f;
	constexpr float first_text = letter * 8;
	constexpr float short_end = letter * 3;
	constexpr float before_bullet_text = indent - 1;
	constexpr float on_bullet = 1;

	TEST_CASE("a point on a word is on text")
	{
		const Layout layout = LayOut(text, ParseMarkdown(text), letter * 8, line, indent, Width);

		CHECK(OnText(layout, text, letter * 1.5f, first_line, line, Width));
		CHECK(OnText(layout, text, letter * 1.5f, second_line, line, Width));
	};

	TEST_CASE("a point past the end of a short line is not on text")
	{
		const Layout layout = LayOut(text, ParseMarkdown(text), letter * 8, line, indent, Width);

		CHECK_FALSE(OnText(layout, text, first_text + letter, first_line, line, Width));
		CHECK_FALSE(OnText(layout, text, short_end + letter, second_line, line, Width));
	};

	TEST_CASE("a point below the last line or above the first is not on text")
	{
		const Layout layout = LayOut(text, ParseMarkdown(text), letter * 8, line, indent, Width);

		CHECK_FALSE(OnText(layout, text, letter, line * 2 + 1, line, Width));
		CHECK_FALSE(OnText(layout, text, letter, -1, line, Width));
	};

	TEST_CASE("a point left of a bullet's text is not on text")
	{
		const Layout layout = LayOut(bullet, ParseMarkdown(bullet), indent + letter * 4, line, indent, Width);

		CHECK_FALSE(OnText(layout, bullet, before_bullet_text, first_line, line, Width));
		CHECK(OnText(layout, bullet, indent + letter, first_line, line, Width));
	};

	TEST_CASE("a point on the bullet itself is not on text")
	{
		const Layout layout = LayOut(bullet, ParseMarkdown(bullet), indent + letter * 4, line, indent, Width);

		CHECK_FALSE(OnText(layout, bullet, on_bullet, first_line, line, Width));
	};

	TEST_CASE("nothing laid out has no text")
	{
		CHECK_FALSE(OnText(Layout{ }, "", 0, 0, line, Width));
	};
};

TEST_SUITE("Figures")
{
	TEST_CASE("a figure stands in its own paragraph between the words around it")
	{
		const std::string text = "before ![a](C:/b.png) after";
		const std::vector<Paragraph> paragraphs = ParseMarkdown(text);

		REQUIRE(paragraphs.size() == 3);
		REQUIRE(paragraphs[1].runs.size() == 1);
		CHECK(paragraphs[1].runs[0].style == Style::Image);
		CHECK(Slice(text, paragraphs[1].runs[0]) == "C:/b.png");
	};

	TEST_CASE("the ways of writing a path all find it")
	{
		using Paths = std::vector<std::string>;

		CHECK(FigurePaths("![a](C:\\b\\c.png)") == Paths{ "C:\\b\\c.png" });
		CHECK(FigurePaths("![a](<C:/my shots/c.png>)") == Paths{ "C:/my shots/c.png" });
		CHECK(FigurePaths("![a](C:/my shots/c.png)") == Paths{ "C:/my shots/c.png" });
		CHECK(FigurePaths("![a](C:/b (1).png)") == Paths{ "C:/b (1).png" });
		CHECK(FigurePaths("![](file:///C:/b.png)") == Paths{ "C:/b.png" });
		CHECK(FigurePaths("![a](file:///C:/my%20shots/c.png)") == Paths{ "C:/my shots/c.png" });
	};

	TEST_CASE("what cannot be shown stays text")
	{
		CHECK(FigurePaths("![a](https://example.com/b.png)").empty());
		CHECK(FigurePaths("![a](relative/b.png)").empty());
		CHECK(FigurePaths("![a](C:/b.txt)").empty());
		CHECK(FigurePaths("`![a](C:/b.png)`").empty());
		CHECK(FigurePaths("```\n![a](C:/b.png)\n```").empty());
		CHECK(FigurePaths("![a](C:/b.png").empty());
	};

	TEST_CASE("a figure takes the height it asks for, and offsets pass over it")
	{
		constexpr float width = 1000;
		constexpr float figure = 100;

		const std::string text = "one\n\n![a](C:/b.png)\n\ntwo";
		const std::vector<Paragraph> paragraphs = ParseMarkdown(text);
		const FigureHeight height = [](std::string_view, float) { return figure; };

		const Layout layout = LayOut(text, paragraphs, width, line, indent, Width, height);

		const auto shown = std::ranges::find(layout.pieces, Style::Image, &Piece::style);

		REQUIRE(shown != layout.pieces.end());

		const float below = layout.pieces.back().y;

		CHECK(below >= shown->y + figure);
		CHECK(layout.height >= below + line);
		CHECK(OffsetAt(layout, text, 0, below, line, Width) == text.find("two"));
	};
};

TEST_SUITE("InputLines")
{
	TEST_CASE("empty text is one row")
	{
		CHECK(InputLines("", Rows(row_bytes)) == 1);
	};

	TEST_CASE("a short line is one row")
	{
		CHECK(InputLines("abc", Rows(row_bytes)) == 1);
	};

	TEST_CASE("a trailing newline starts a new row")
	{
		CHECK(InputLines("abc\n", Rows(row_bytes)) == 2);
		CHECK(InputLines("\n", Rows(row_bytes)) == 2);
	};

	TEST_CASE("consecutive newlines each add a row")
	{
		CHECK(InputLines("a\n\nb", Rows(row_bytes)) == 3);
		CHECK(InputLines("\n\n\n", Rows(row_bytes)) == 4);
		CHECK(InputLines("a\n\n", Rows(row_bytes)) == 3);
	};

	TEST_CASE("a line exactly the row width is one row, one byte over is two")
	{
		CHECK(InputLines("abcde", Rows(row_bytes)) == 1);
		CHECK(InputLines("abcdef", Rows(row_bytes)) == 2);
	};

	TEST_CASE("a long line spans as many rows as it needs")
	{
		CHECK(InputLines("abcdefghijklmno", Rows(row_bytes)) == 3);
		CHECK(InputLines("abcdefghijklmnop", Rows(row_bytes)) == 4);
	};

	TEST_CASE("newlines and wrapping add up")
	{
		CHECK(InputLines("abcdefg\nhi\n\nabcdefghijk\n", Rows(row_bytes)) == 2 + 1 + 1 + 3 + 1);
	};

	TEST_CASE("each line is wrapped on its own")
	{
		std::vector<std::string> seen;

		const Wrap record = [&seen](std::string_view text)
		{
			seen.emplace_back(text);

			return text.size();
		};

		CHECK(InputLines("ab\n\ncd", record) == 3);
		CHECK(seen == std::vector<std::string>{ "ab", "cd" });
	};
};

TEST_SUITE("MoveRow")
{
	constexpr float wide = 30;
	constexpr float narrow = 5;
	constexpr size_t past_the_text = 100;

	const Wrap wrap = Rows(row_bytes);

	// 'w' is wide and 'h' narrow, so x falls between the positions of a row.
	float Uneven(std::string_view text)
	{
		float width = 0;

		for (const char character : text)
			width += character == 'w' ? wide : character == 'h' ? narrow : letter;

		return width;
	};

	InputCaret Move(
		std::string_view text,
		InputCaret caret,
		RowStep step,
		float goal = own_x,
		const Measure& measure = Width)
	{
		return MoveRow(text, caret, step, wrap, measure, goal);
	};

	TEST_CASE("up on the top row goes to the start")
	{
		const InputCaret start{ 0, false };

		CHECK(Move("abcd\nefgh", { 2, false }, RowStep::Up) == start);
		CHECK(Move("abcd\nefgh", { 4, false }, RowStep::Up) == start);
		CHECK(Move("abcd\nefgh", { 0, false }, RowStep::Up) == start);
		CHECK(Move("abc", { 2, false }, RowStep::Up) == start);
		CHECK(Move("abc", { 3, false }, RowStep::Up) == start);
		CHECK(Move("", { 0, false }, RowStep::Up) == start);
	};

	TEST_CASE("up from a lower row lands at the same x on the row above")
	{
		CHECK(Move("abcd\nefgh", { 7, false }, RowStep::Up) == InputCaret{ 2, false });
		CHECK(Move("abcd\nefgh\nijkl", { 12, false }, RowStep::Up) == InputCaret{ 7, false });
		CHECK(Move("abcd\nefgh", { 5, false }, RowStep::Up) == InputCaret{ 0, false });
	};

	TEST_CASE("up onto a shorter row lands at its end, before the newline")
	{
		CHECK(Move("ab\ncdefg", { 7, false }, RowStep::Up) == InputCaret{ 2, false });
		CHECK(Move("abc\ndefgh", { 8, false }, RowStep::Up) == InputCaret{ 3, false });
	};

	TEST_CASE("up across wrapped rows")
	{
		CHECK(Move("abcdefghij", { 7, false }, RowStep::Up) == InputCaret{ 2, false });
		CHECK(Move("abcdefghijklm", { 12, false }, RowStep::Up) == InputCaret{ 7, false });
	};

	TEST_CASE("up onto an empty row")
	{
		CHECK(Move("ab\n\ncd", { 5, false }, RowStep::Up) == InputCaret{ 3, false });
		CHECK(Move("ab\n\ncd", { 3, false }, RowStep::Up) == InputCaret{ 0, false });
	};

	TEST_CASE("up lands on the nearer position, not the one before the caret's x")
	{
		CHECK(Move("wa\nbb", { 5, false }, RowStep::Up, own_x, Uneven) == InputCaret{ 1, false });
	};

	TEST_CASE("up between two equally near positions lands on the earlier")
	{
		CHECK(Move("aw\nhhhhh", { 8, false }, RowStep::Up, own_x, Uneven) == InputCaret{ 1, false });
	};

	TEST_CASE("down between two equally near positions lands on the earlier")
	{
		CHECK(Move("hhhhh\naw", { 5, false }, RowStep::Down, own_x, Uneven) == InputCaret{ 7, false });
	};

	TEST_CASE("a caret at a wrap boundary is at the start of the next row unless trailing")
	{
		const std::string text = "abcdefghij";

		CHECK(Move(text, { 5, false }, RowStep::Up) == InputCaret{ 0, false });
		CHECK(Move(text, { 5, true }, RowStep::Up) == InputCaret{ 0, false });
		CHECK(Move(text, { 5, false }, RowStep::Down) == InputCaret{ 10, false });
		CHECK(Move(text, { 5, true }, RowStep::Down) == InputCaret{ 10, false });
	};

	TEST_CASE("a trailing flag away from a wrap boundary changes nothing")
	{
		CHECK(Move("abcdefghij", { 7, true }, RowStep::Up) == InputCaret{ 2, false });
		CHECK(Move("abcdefghij", { 2, true }, RowStep::Down) == InputCaret{ 7, false });
	};

	TEST_CASE("landing at the end of a wrapped row is trailing")
	{
		CHECK(Move("abcdefghij", { 10, false }, RowStep::Up) == InputCaret{ 5, true });
		CHECK(Move("abcdefghij", { 10, true }, RowStep::Up) == InputCaret{ 5, true });
		CHECK(Move("abcdefghijklm", { 5, true }, RowStep::Down) == InputCaret{ 10, true });
	};

	TEST_CASE("landing at the end of a row that ends in a newline or the text is not trailing")
	{
		CHECK_FALSE(Move("abc\ndefgh", { 8, false }, RowStep::Up).trailing);
		CHECK_FALSE(Move("abcd\nef", { 3, false }, RowStep::Down).trailing);
		CHECK_FALSE(Move("abcdefghijklm", { 9, false }, RowStep::Down).trailing);
	};

	TEST_CASE("a goal overrides the caret's own x")
	{
		constexpr float three_letters = letter * 3;
		constexpr float far_right = letter * 100;

		CHECK(Move("abcd\nefgh", { 5, false }, RowStep::Up, three_letters) == InputCaret{ 3, false });
		CHECK(Move("abcd\nefgh", { 8, false }, RowStep::Up, 0) == InputCaret{ 0, false });
		CHECK(Move("abcd\nefgh", { 5, false }, RowStep::Up, far_right) == InputCaret{ 4, false });
		CHECK(Move("abcd\nefgh", { 8, false }, RowStep::Down, three_letters) == InputCaret{ 9, false });
		CHECK(Move("abcd\nefgh\nij", { 0, false }, RowStep::Down, three_letters) == InputCaret{ 8, false });
	};

	TEST_CASE("code points are never split")
	{
		const std::string text = "\xC3\xA9\xC3\xA9\nab";

		CHECK(Move(text, { 7, false }, RowStep::Up) == InputCaret{ 4, false });
		CHECK(Move(text, { 6, false }, RowStep::Up) == InputCaret{ 2, false });
		CHECK(Move(text, { 6, false }, RowStep::Up, letter * 1.5f) == InputCaret{ 2, false });
		CHECK(Move(text, { 2, false }, RowStep::Down) == InputCaret{ 6, false });
		CHECK(Move(text, { 4, false }, RowStep::Down) == InputCaret{ 7, false });
		CHECK(Move("ab\n\xC3\xA9\xC3\xA9", { 1, false }, RowStep::Down) == InputCaret{ 5, false });
	};

	TEST_CASE("down from the bottom row goes to the end of the text")
	{
		CHECK(Move("abc\ndef", { 5, false }, RowStep::Down) == InputCaret{ 7, false });
		CHECK(Move("abc", { 1, false }, RowStep::Down) == InputCaret{ 3, false });
		CHECK(Move("abc", { 3, false }, RowStep::Down) == InputCaret{ 3, false });
		CHECK(Move("", { 0, false }, RowStep::Down) == InputCaret{ 0, false });
		CHECK(Move("ab\n", { 3, false }, RowStep::Down) == InputCaret{ 3, false });
		CHECK(Move("abcdefghij", { 7, false }, RowStep::Down) == InputCaret{ 10, false });
	};

	TEST_CASE("down from an upper row lands at the same x on the row below")
	{
		CHECK(Move("abcd\nefgh", { 2, false }, RowStep::Down) == InputCaret{ 7, false });
		CHECK(Move("abcd\nefgh\nijkl", { 2, false }, RowStep::Down) == InputCaret{ 7, false });
		CHECK(Move("abcd\nefgh", { 0, false }, RowStep::Down) == InputCaret{ 5, false });
	};

	TEST_CASE("down onto a shorter row lands at its end")
	{
		CHECK(Move("abcd\nef", { 3, false }, RowStep::Down) == InputCaret{ 7, false });
		CHECK(Move("abcd\nef\ngh", { 4, false }, RowStep::Down) == InputCaret{ 7, false });
	};

	TEST_CASE("down across wrapped rows")
	{
		CHECK(Move("abcdefghij", { 2, false }, RowStep::Down) == InputCaret{ 7, false });
		CHECK(Move("abcdefghijklm", { 3, false }, RowStep::Down) == InputCaret{ 8, false });
		CHECK(Move("abcdefghijklm", { 8, false }, RowStep::Down) == InputCaret{ 13, false });
	};

	TEST_CASE("down onto the empty row after a trailing newline")
	{
		CHECK(Move("ab\n", { 1, false }, RowStep::Down) == InputCaret{ 3, false });
		CHECK(Move("ab\n\ncd", { 1, false }, RowStep::Down) == InputCaret{ 3, false });
	};

	TEST_CASE("a caret past the text is clamped")
	{
		CHECK(Move("abcd\nefgh", { past_the_text, false }, RowStep::Up) == InputCaret{ 4, false });
		CHECK(Move("abcd\nefgh", { past_the_text, false }, RowStep::Down) == InputCaret{ 9, false });
		CHECK(Move("abc", { past_the_text, false }, RowStep::Up) == InputCaret{ 0, false });
		CHECK(Move("", { past_the_text, false }, RowStep::Down) == InputCaret{ 0, false });
	};
};
