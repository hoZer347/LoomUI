#include "TextLayout.h"

#include "Images.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <string>
#include <string_view>


namespace Loom::TextLayout
{
	namespace
	{
		constexpr size_t tab_columns = 2;
		constexpr size_t max_heading = 6;
		constexpr std::string_view fence = "```";
		constexpr std::string_view strong = "**";
		constexpr std::string_view emphasis = "*";
		constexpr std::string_view code = "`";
		constexpr std::string_view figure_open = "![";
		constexpr std::string_view figure_link = "](";
		constexpr std::string_view file_scheme = "file:///";

		constexpr unsigned char two_byte_lead = 0xC0;
		constexpr unsigned char three_byte_lead = 0xE0;
		constexpr unsigned char four_byte_lead = 0xF0;


		constexpr float blank_line = 0.5f;

		#pragma region Parsing

		void Add(std::vector<Run>& runs, size_t begin, size_t end, Style style)
		{
			if (begin < end)
				runs.push_back({ (uint32_t)begin, (uint32_t)end, style });
		};

		std::string_view MarkerAt(std::string_view text, size_t at, size_t end)
		{
			if (text[at] == code[0])
				return code;

			if (text.substr(at, strong.size()) == strong)
				return strong;

			if (text[at] == emphasis[0] && at + 1 < end && text[at + 1] != ' ')
				return emphasis;

			return { };
		};

		void Inline(std::string_view text, size_t begin, size_t end, Style base, std::vector<Run>& runs)
		{
			size_t plain = begin;
			size_t at = begin;

			while (at < end)
			{
				const std::string_view marker = MarkerAt(text, at, end);

				if (marker.empty())
				{
					at++;
					continue;
				};

				const size_t open = at + marker.size();
				const size_t close = text.find(marker, open);

				if (close == std::string_view::npos || close + marker.size() > end || close == open ||
					(marker == emphasis && text[close - 1] == ' '))
				{
					at = open;
					continue;
				};

				Add(runs, plain, at, base);
				Add(runs, open, close, marker == strong ? Style::Strong : marker == emphasis ? base : Style::Code);
				at = plain = close + marker.size();
			};

			Add(runs, plain, end, base);
		};

		template <typename Each>
		void ForEachLine(std::string_view text, Each each)
		{
			size_t begin = 0;

			while (begin <= text.size())
			{
				const size_t newline = text.find('\n', begin);
				size_t end = newline == std::string_view::npos ? text.size() : newline;
				const size_t next = end + 1;

				if (end > begin && text[end - 1] == '\r')
					end--;

				each(begin, end);
				begin = next;
			};
		};

		bool IsRule(std::string_view line)
		{
			return line.size() >= fence.size() &&
				line.find_first_not_of('-') == std::string_view::npos;
		};

		size_t HeadingMarks(std::string_view line)
		{
			const size_t marks = std::min(line.find_first_not_of('#'), line.size());

			return marks >= 1 && marks <= max_heading && marks < line.size() && line[marks] == ' ' ? marks : 0;
		};

		bool IsBullet(std::string_view line)
		{
			return line.size() > 1 &&
				(line[0] == '-' || line[0] == '*' || line[0] == '+') &&
				line[1] == ' ';
		};

		struct Figure
		{
			size_t begin = 0;
			size_t pathBegin = 0;
			size_t pathEnd = 0;
			size_t end = 0;
		};

		bool IsAbsolute(std::string_view path)
		{
			constexpr size_t drive_length = 3;

			return path.starts_with("\\\\") || path.starts_with('/') ||
				(path.size() >= drive_length && std::isalpha((unsigned char)path[0]) && path[1] == ':' &&
					(path[2] == '\\' || path[2] == '/'));
		};

		// The closing bracket of a link whose destination starts at begin. Paths
		// hold spaces, and parentheses as long as they pair up.
		size_t LinkEnd(std::string_view text, size_t begin, size_t end)
		{
			int depth = 0;

			for (size_t at = begin; at < end; at++)
			{
				if (text[at] == '(')
					depth++;
				else if (text[at] == ')' && depth-- == 0)
					return at;
			};

			return std::string_view::npos;
		};

		bool NextFigure(std::string_view text, size_t begin, size_t end, Figure& figure)
		{
			for (size_t at = text.find(figure_open, begin); at != std::string_view::npos && at < end;
				at = text.find(figure_open, at + 1))
			{
				const size_t link = text.find(figure_link, at);

				if (link == std::string_view::npos || link >= end || (at > 0 && text[at - 1] == code[0]))
					continue;

				size_t pathBegin = link + figure_link.size();
				const size_t close = LinkEnd(text, pathBegin, end);

				if (close == std::string_view::npos)
					continue;

				size_t pathEnd = close;

				if (pathBegin < pathEnd && text[pathBegin] == '<' && text[pathEnd - 1] == '>')
				{
					pathBegin++;
					pathEnd--;
				};

				const std::string path = FigureFile(text.substr(pathBegin, pathEnd - pathBegin));

				if (IsAbsolute(path) && Images::IsImageFile(path))
				{
					figure = { at, pathBegin, pathEnd, close + 1 };
					return true;
				};
			};

			return false;
		};

		bool HasWords(std::string_view text)
		{
			return std::ranges::any_of(text,
				[](char letter)
				{
					return std::isalnum((unsigned char)letter) || (unsigned char)letter >= two_byte_lead;
				});
		};

		void ParseLine(std::string_view text, size_t begin, size_t end, std::vector<Paragraph>& paragraphs)
		{
			const std::string_view line = text.substr(begin, end - begin);
			const size_t spaces = std::min(line.find_first_not_of(' '), line.size());
			const std::string_view trimmed = line.substr(spaces);

			Paragraph paragraph;

			if (trimmed.empty() || IsRule(trimmed))
			{
				if (!paragraphs.empty() && !paragraphs.back().runs.empty())
					paragraphs.push_back(std::move(paragraph));

				return;
			};

			const size_t start = begin + spaces;

			if (const size_t marks = HeadingMarks(trimmed))
				Inline(text, start + marks + 1, end, Style::Strong, paragraph.runs);
			else if (IsBullet(trimmed))
			{
				paragraph.bullet = true;
				paragraph.indent = (int)(spaces / tab_columns);
				Inline(text, start + tab_columns, end, Style::Plain, paragraph.runs);
			}
			else if (trimmed[0] == '>')
				Inline(text, std::min(start + tab_columns, end), end, Style::Quote, paragraph.runs);
			else
			{
				paragraph.indent = (int)(spaces / tab_columns);
				Inline(text, start, end, Style::Plain, paragraph.runs);
			};

			paragraphs.push_back(std::move(paragraph));
		};

		#pragma endregion

		#pragma region Placing

		// Places text on lines, joining what follows on from the last piece.
		struct Placer
		{
			std::vector<Piece>& pieces;
			float width;
			float lineHeight;
			float left = 0;
			float x = 0;
			float y = 0;
			size_t firstOfParagraph = 0;

			void NewLine()
			{
				x = left;
				y += lineHeight;
			};

			void Place(size_t begin, size_t end, float advance, Style style)
			{
				const bool joins = pieces.size() > firstOfParagraph &&
					pieces.back().style == style &&
					pieces.back().y == y &&
					pieces.back().end == begin;

				if (joins)
					pieces.back().end = (uint32_t)end;
				else pieces.push_back({ (uint32_t)begin, (uint32_t)end, x, y, style });

				x += advance;
			};
		};

		// How many bytes of token fit in space, ending on a code point.
		size_t Fit(std::string_view token, float space, const Measure& measure, float& fitted)
		{
			size_t at = 0;
			fitted = 0;

			while (at < token.size())
			{
				const size_t length = std::min(CodePointLength(token[at]), token.size() - at);
				const float advance = measure(token.substr(at, length));

				if (fitted + advance > space)
					break;

				fitted += advance;
				at += length;
			};

			return at;
		};

		void PlaceWord(
			Placer& placer,
			std::string_view text,
			size_t begin,
			size_t end,
			Style style,
			const Measure& measure)
		{
			const bool spaced = text[end - 1] == ' ';
			const size_t wordEnd = spaced ? end - 1 : end;
			const float word = measure(text.substr(begin, wordEnd - begin));
			const float space = spaced ? measure(" ") : 0;

			if (placer.x > placer.left && placer.x + word > placer.width)
				placer.NewLine();

			if (word <= placer.width - placer.left)
			{
				placer.Place(begin, end, word + space, style);
				return;
			};

			size_t at = begin;

			while (at < wordEnd)
			{
				float fitted = 0;
				size_t fit = Fit(text.substr(at, wordEnd - at), placer.width - placer.x, measure, fitted);

				if (fit == 0)
				{
					fit = CodePointLength(text[at]);
					fitted = measure(text.substr(at, fit));
				};

				placer.Place(at, at + fit, fitted, style);
				at += fit;

				if (at < wordEnd)
					placer.NewLine();
			};
		};

		#pragma endregion

		#pragma region Input

		struct InputRow
		{
			size_t begin = 0;
			size_t end = 0;
			bool wrapped = false;
		};

		template <typename Each>
		void EachRow(std::string_view text, const Wrap& wrap, const Each& each)
		{
			size_t begin = 0;

			for (;;)
			{
				const size_t newline = text.find('\n', begin);
				const size_t end = newline == std::string_view::npos ? text.size() : newline;

				if (begin == end)
					each(InputRow{ begin, end, false });

				while (begin < end)
				{
					const size_t taken = std::clamp<size_t>(wrap(text.substr(begin, end - begin)), 1, end - begin);

					each(InputRow{ begin, begin + taken, begin + taken < end });
					begin += taken;
				};

				if (newline == std::string_view::npos)
					return;

				begin = newline + 1;
			};
		};

		#pragma endregion
	};

	#pragma region Parse

	size_t CodePointLength(char lead)
	{
		const unsigned char byte = (unsigned char)lead;

		if (byte < two_byte_lead)
			return 1;

		if (byte < three_byte_lead)
			return 2;

		return byte < four_byte_lead ? 3 : 4;
	};

	int InputLines(std::string_view text, const Wrap& wrap)
	{
		int rows = 0;

		EachRow(text, wrap, [&rows](const InputRow&) { ++rows; });

		return rows;
	};

	InputCaret MoveRow(
		std::string_view text,
		InputCaret caret,
		RowStep step,
		const Wrap& wrap,
		const Measure& measure,
		float goal)
	{
		std::vector<InputRow> rows;

		EachRow(text, wrap, [&rows](const InputRow& row) { rows.push_back(row); });

		const size_t index = std::min(caret.index, text.size());
		size_t at = rows.size() - 1;

		while (rows[at].begin > index)
			--at;

		if (caret.trailing
			&& at > 0
			&& rows[at].begin == index
			&& rows[at - 1].wrapped)
			--at;

		if (step == RowStep::Up && at == 0)
			return { };

		if (step == RowStep::Down && at == rows.size() - 1)
			return { text.size(), false };

		const float aim = goal < 0 ? measure(text.substr(rows[at].begin, index - rows[at].begin)) : goal;
		const InputRow& row = rows[step == RowStep::Up ? at - 1 : at + 1];

		size_t nearest = row.begin;
		float x = 0;
		float gap = aim;

		for (size_t next = row.begin; next < row.end && x < aim;)
		{
			const size_t length = std::min(CodePointLength(text[next]), row.end - next);

			x += measure(text.substr(next, length));
			next += length;

			if (const float away = std::abs(x - aim); away < gap)
			{
				nearest = next;
				gap = away;
			};
		};

		return { nearest, row.wrapped && nearest == row.end };
	};

	std::vector<Paragraph> ParseMarkdown(std::string_view text)
	{
		std::vector<Paragraph> paragraphs;
		bool fenced = false;

		ForEachLine(
			text,
			[text, &paragraphs, &fenced](size_t begin, size_t end)
		{
			const std::string_view line = text.substr(begin, end - begin);
			const std::string_view trimmed = line.substr(std::min(line.find_first_not_of(' '), line.size()));

			if (trimmed.starts_with(fence))
			{
				fenced = !fenced;
				return;
			};

			if (fenced)
			{
				Paragraph paragraph;
				paragraph.runs.push_back({ (uint32_t)begin, (uint32_t)end, Style::Fence });
				paragraphs.push_back(std::move(paragraph));
				return;
			};

			size_t from = begin;
			Figure figure;

			while (NextFigure(text, from, end, figure))
			{
				if (HasWords(text.substr(from, figure.begin - from)))
					ParseLine(text, from, figure.begin, paragraphs);

				Paragraph paragraph;
				paragraph.runs.push_back({ (uint32_t)figure.pathBegin, (uint32_t)figure.pathEnd, Style::Image });
				paragraphs.push_back(std::move(paragraph));
				from = figure.end;
			};

			if (from == begin || HasWords(text.substr(from, end - from)))
				ParseLine(text, from, end, paragraphs);
		});

		while (!paragraphs.empty() && paragraphs.back().runs.empty())
			paragraphs.pop_back();

		return paragraphs;
	};

	bool IsFigure(const Paragraph& paragraph)
	{
		return paragraph.runs.size() == 1 && paragraph.runs[0].style == Style::Image;
	};

	std::string FigureFile(std::string_view link)
	{
		if (!link.starts_with(file_scheme))
			return std::string(link);

		constexpr size_t escape_length = 3;
		constexpr int hex = 16;

		const std::string_view path = link.substr(file_scheme.size());
		std::string file;

		for (size_t at = 0; at < path.size(); at++)
		{
			unsigned char letter = 0;

			if (path[at] == '%' && at + escape_length <= path.size() &&
				std::from_chars(path.data() + at + 1, path.data() + at + escape_length, letter, hex).ptr == path.data() + at + escape_length)
			{
				file += (char)letter;
				at += escape_length - 1;
			}
			else file += path[at];
		};

		return file;
	};

	std::vector<std::string> FigurePaths(std::string_view text)
	{
		std::vector<std::string> paths;

		if (text.find(figure_open) == std::string_view::npos)
			return paths;

		for (const Paragraph& paragraph : ParseMarkdown(text))
			if (IsFigure(paragraph))
				paths.push_back(FigureFile(text.substr(paragraph.runs[0].begin, paragraph.runs[0].end - paragraph.runs[0].begin)));

		return paths;
	};

	std::vector<Paragraph> ParseLines(std::string_view text, bool diff)
	{
		std::vector<Paragraph> paragraphs;

		ForEachLine(
			text,
			[text, diff, &paragraphs](size_t begin, size_t end)
		{
			Style style = Style::Plain;

			if (diff && begin != end && text[begin] == '+')
				style = Style::Added;
			else if (diff && begin != end && text[begin] == '-')
				style = Style::Removed;

			Paragraph paragraph;
			Add(paragraph.runs, begin, end, style);
			paragraphs.push_back(std::move(paragraph));
		});

		return paragraphs;
	};

	#pragma endregion

	#pragma region Layout

	Layout LayOut(
		std::string_view text,
		const std::vector<Paragraph>& paragraphs,
		float width,
		float lineHeight,
		float indentWidth,
		const Measure& measure,
		const FigureHeight& figureHeight)
	{
		Layout layout;
		Extend(layout, text, paragraphs, 0, width, lineHeight, indentWidth, measure, figureHeight);

		return layout;
	};

	void Extend(
		Layout& layout,
		std::string_view text,
		const std::vector<Paragraph>& paragraphs,
		size_t from,
		float width,
		float lineHeight,
		float indentWidth,
		const Measure& measure,
		const FigureHeight& figureHeight)
	{
		Placer placer{ layout.pieces, width, lineHeight };
		placer.y = layout.height;

		if (from < layout.starts.size())
		{
			layout.pieces.resize(layout.starts[from].piece);
			placer.y = layout.starts[from].y;
			layout.starts.resize(from);
		};

		for (size_t index = from; index < paragraphs.size(); index++)
		{
			const Paragraph& paragraph = paragraphs[index];

			layout.starts.push_back({ (uint32_t)layout.pieces.size(), placer.y });

			if (paragraph.runs.empty())
			{
				placer.y += lineHeight * blank_line;
				continue;
			};

			if (IsFigure(paragraph))
			{
				const Run& path = paragraph.runs[0];

				layout.pieces.push_back({ path.begin, path.end, 0, placer.y, Style::Image });
				placer.y += figureHeight ? figureHeight(text.substr(path.begin, path.end - path.begin), width) : lineHeight;
				continue;
			};

			placer.left = indentWidth * paragraph.indent;

			if (paragraph.bullet)
			{
				layout.pieces.push_back({ 0, 0, placer.left, placer.y, Style::Bullet });
				placer.left += indentWidth;
			};

			placer.x = placer.left;
			placer.firstOfParagraph = layout.pieces.size();

			for (const Run& run : paragraph.runs)
			{
				const std::string_view runText = text.substr(0, run.end);
				size_t at = run.begin;

				while (at < run.end)
				{
					const size_t space = runText.find(' ', at);
					const size_t end = space == std::string_view::npos ? run.end : space + 1;

					PlaceWord(placer, text, at, end, run.style, measure);
					at = end;
				};
			};

			placer.y += lineHeight;
		};

		layout.height = placer.y;
	};

	#pragma endregion

	#pragma region Hit testing

	size_t OffsetAt(
		const Layout& layout,
		std::string_view text,
		float x,
		float y,
		float lineHeight,
		const Measure& measure)
	{
		const auto isText = [](const Piece& piece) { return piece.style != Style::Bullet && piece.style != Style::Image; };

		const Piece* last = nullptr;

		for (const Piece& piece : layout.pieces)
			if (isText(piece))
			{
				if (last != nullptr && last->y != piece.y && last->y + lineHeight > y)
					break;

				if (last == nullptr || last->y != piece.y || x >= piece.x)
					last = &piece;

				if (last == &piece && x < piece.x)
					return piece.begin;
			};

		if (last == nullptr)
			return 0;

		float at = last->x;
		size_t offset = last->begin;

		while (offset < last->end)
		{
			const size_t length = CodePointLength(text[offset]);
			const float advance = measure(text.substr(offset, length));

			if (at + advance / 2 > x)
				break;

			at += advance;
			offset += length;
		};

		return std::min<size_t>(offset, last->end);
	};

	bool OnText(
		const Layout& layout,
		std::string_view text,
		float x,
		float y,
		float lineHeight,
		const Measure& measure)
	{
		return std::ranges::any_of(layout.pieces, [&](const Piece& piece)
		{
			return piece.style != Style::Bullet &&
				piece.style != Style::Image &&
				y >= piece.y &&
				y < piece.y + lineHeight &&
				x >= piece.x &&
				x < piece.x + measure(text.substr(piece.begin, piece.end - piece.begin));
		});
	};

	#pragma endregion
};
