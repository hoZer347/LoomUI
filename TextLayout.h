#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>


namespace Loom::TextLayout
{
	enum class Style : uint8_t
	{
		Plain,
		Strong,
		Code,
		Fence,
		Quote,
		Bullet,
		Added,
		Removed,
		Image,
	};

	// A stretch of the source text in one style. Offsets, so a paragraph holds
	// no copy of it.
	struct Run
	{
		uint32_t begin = 0;
		uint32_t end = 0;
		Style style = Style::Plain;

		bool operator==(const Run&) const = default;
	};

	struct Paragraph
	{
		std::vector<Run> runs;
		int indent = 0;
		bool bullet = false;

		bool operator==(const Paragraph&) const = default;
	};

	// A run's words placed on one line.
	struct Piece
	{
		uint32_t begin = 0;
		uint32_t end = 0;
		float x = 0;
		float y = 0;
		Style style = Style::Plain;

		bool operator==(const Piece&) const = default;
	};

	// Where a paragraph starts in a layout.
	struct ParagraphStart
	{
		uint32_t piece = 0;
		float y = 0;
	};

	struct Layout
	{
		std::vector<Piece> pieces;
		std::vector<ParagraphStart> starts;
		float height = 0;
	};

	using Measure = std::function<float(std::string_view)>;

	using FigureHeight = std::function<float(std::string_view path, float width)>;

	// Headings, lists, quotes, fences, **bold**, `code` and ![figures](absolute path).
	// An unclosed marker stays as text, so a message reads the same while it streams in.
	std::vector<Paragraph> ParseMarkdown(std::string_view text);

	std::string FigureFile(std::string_view link);

	std::vector<std::string> FigurePaths(std::string_view text);
	bool IsFigure(const Paragraph& paragraph);

	// One paragraph a line. With diff, lines starting + or - take their colours.
	std::vector<Paragraph> ParseLines(std::string_view text, bool diff);

	Layout LayOut(
		std::string_view text,
		const std::vector<Paragraph>& paragraphs,
		float width,
		float lineHeight,
		float indentWidth,
		const Measure& measure,
		const FigureHeight& figureHeight = { });

	// Lays paragraphs out again from the one at index from, keeping what is
	// before it, so text growing at its end costs only its last paragraph.
	void Extend(
		Layout& layout,
		std::string_view text,
		const std::vector<Paragraph>& paragraphs,
		size_t from,
		float width,
		float lineHeight,
		float indentWidth,
		const Measure& measure,
		const FigureHeight& figureHeight = { });

	// The byte in text nearest a point in the layout.
	size_t OffsetAt(
		const Layout& layout,
		std::string_view text,
		float x,
		float y,
		float lineHeight,
		const Measure& measure);

	// Whether a point in the layout is on its text.
	bool OnText(
		const Layout& layout,
		std::string_view text,
		float x,
		float y,
		float lineHeight,
		const Measure& measure);

	// The bytes a code point starting there takes, for a lead byte.
	size_t CodePointLength(char lead);

	// The byte length of the first visual row of one non-empty line, at least 1.
	using Wrap = std::function<size_t(std::string_view line)>;

	// The rows a text box shows for text: an empty line is one row, as is the
	// one after a trailing newline, and any other takes as many rows as wrap
	// needs to consume it. Empty text is one row.
	int InputLines(std::string_view text, const Wrap& wrap);

	// A caret in a text box: the byte it sits before, and whether it shows at the
	// end of the row that wrapped there, not at the start of the next.
	struct InputCaret
	{
		size_t index = 0;
		bool trailing = false;

		bool operator==(const InputCaret&) const = default;
	};

	enum class RowStep : uint8_t
	{
		Up,
		Down,
	};

	constexpr float own_x = -1;

	// Where a caret goes one row up or down, rows being those InputLines counts:
	// the place on that row nearest goal, the earlier of two equally near. Up
	// from the top row is the start of the text, down from the bottom row its
	// end. A goal of own_x is the caret's own x.
	InputCaret MoveRow(
		std::string_view text,
		InputCaret caret,
		RowStep step,
		const Wrap& wrap,
		const Measure& measure,
		float goal = own_x);
};
