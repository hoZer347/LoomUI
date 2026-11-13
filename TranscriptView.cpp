#include "TranscriptView.h"

#include "ChatInput.h"
#include "Images.h"

#include "imgui_internal.h"

#include <algorithm>
#include <iterator>
#include <ranges>
#include <string>


namespace Loom
{
	using namespace TextLayout;

	namespace
	{
		constexpr float block_gap = 10;
		constexpr float box_padding = 6;
		constexpr float indent_width = 14;
		constexpr float bullet_radius = 2;
		constexpr float stick_slack = 4;
		constexpr float edge_scroll_speed = 600;

		constexpr float figure_max_height = 360;
		constexpr float figure_gap = 6;

		constexpr size_t working_dots = 3;

		constexpr ImU32 user_box = IM_COL32(255, 255, 255, 18);
		constexpr ImU32 code_back = IM_COL32(255, 255, 255, 14);
		constexpr ImU32 selection = IM_COL32(80, 130, 220, 110);
		constexpr ImU32 strong_text = IM_COL32(255, 255, 255, 255);
		constexpr ImU32 code_text = IM_COL32(190, 215, 240, 255);
		constexpr ImU32 added_text = IM_COL32(125, 205, 135, 255);
		constexpr ImU32 removed_text = IM_COL32(235, 125, 125, 255);
		constexpr ImU32 error_text = IM_COL32(235, 110, 100, 255);
		constexpr float link_line_drop = 1;

		using Row = TranscriptView::Row;

		ImU32 ColorOf(Style style, ImU32 plain)
		{
			switch (style)
			{
			case Style::Strong: return strong_text;
			case Style::Code:
			case Style::Fence: return code_text;
			case Style::Added: return added_text;
			case Style::Removed: return removed_text;
			default: return plain;
			};
		};

		bool IsFolding(const Row& row)
		{
			return row.kind == Row::Kind::Folding;
		};

		float BodyInset(const Row& row)
		{
			return row.boxed ? box_padding : 0;
		};

		ImVec2 DetailOrigin()
		{
			return { indent_width, ImGui::GetTextLineHeightWithSpacing() };
		};

		const std::string& ShownPath(const Row& row, const std::string& path)
		{
			if (row.kept == nullptr)
				return path;

			const auto kept = row.kept->find(path);

			return kept == row.kept->end() ? path : kept->second;
		};

		const std::vector<std::string>& ImagesOf(const Row& row)
		{
			static const std::vector<std::string> none;

			return row.images == nullptr ? none : *row.images;
		};
	};

	ImVec2 TranscriptView::Fit(const ImageCache::Entry& entry, ImVec2 space)
	{
		const float scale = Images::Scale((float)entry.width, (float)entry.height, space.x, space.y);

		return { entry.width * scale, entry.height * scale };
	};

	void TranscriptView::Reset()
	{
		m_laid.clear();
		m_anchor = m_focus = { };
		m_stick = true;
	};

	void TranscriptView::Relay(float width)
	{
		const float line = ImGui::GetTextLineHeightWithSpacing();
		const Measure measure = ChatInput::FontMeasure();

		m_laid.resize(m_source->Count());

		float top = 0;

		for (size_t index = 0; index < m_laid.size(); index++)
		{
			const Row row = m_source->At(index);
			Laid& laid = m_laid[index];

			if (laid.width != width || laid.revision != row.revision || laid.open != row.open)
			{
				const bool sameWidth = laid.width == width;

				laid.width = width;
				laid.revision = row.revision;
				laid.open = row.open;

				const float inset = BodyInset(row);

				if (IsFolding(row))
				{
					laid.body = { };
					if (row.open && laid.linesRevision != row.revision)
					{
						laid.lines = ParseLines(row.detail, row.diff);
						laid.linesRevision = row.revision;
					};

					laid.detail = row.open
						? LayOut(row.detail, laid.lines, width - indent_width, line, indent_width, measure)
						: Layout{ };
					laid.height = line + laid.detail.height +
						(row.open ? ImagesHeight(ImagesOf(row), width - indent_width) : 0);
				}
				else
				{
					if (row.kind == Row::Kind::Markdown)
					{
						const bool parsed = laid.parsedRevision == row.revision;
						std::vector<Paragraph> paragraphs = parsed ? std::vector<Paragraph>{ } : ParseMarkdown(row.text);
						const size_t same = sameWidth && !parsed
							? (size_t)(std::ranges::mismatch(paragraphs, laid.paragraphs).in1 - paragraphs.begin())
							: 0;

						const FigureHeight figureHeight = [this, &row](std::string_view path, float space)
						{
							return FigureSpace(ShownPath(row, FigureFile(path)), space);
						};

						if (!parsed)
						{
							laid.paragraphs = std::move(paragraphs);
							laid.parsedRevision = row.revision;
						};

						Extend(laid.body, row.text, laid.paragraphs, same, width, line, indent_width, measure, figureHeight);
					}
					else
					{
						if (laid.linesRevision != row.revision)
						{
							laid.lines = ParseLines(row.text, false);
							laid.linesRevision = row.revision;
						};

						laid.body = LayOut(row.text, laid.lines, width - inset * 2, line, indent_width, measure);
					};

					laid.height = laid.body.height + ImagesHeight(ImagesOf(row), width - inset * 2) + inset * 2;
				};
			};

			laid.top = top;
			top += laid.height + block_gap;
		};
	};

	void TranscriptView::ImagesChanged(const Source& source)
	{
		for (size_t index = 0; index < std::min(source.Count(), m_laid.size()); index++)
			if (!ImagesOf(source.At(index)).empty() || std::ranges::any_of(m_laid[index].paragraphs, IsFigure))
				m_laid[index].width = -1;
	};

	std::pair<size_t, size_t> TranscriptView::SelectedIn(size_t index, Part part, size_t size) const
	{
		const Caret low = std::min(m_anchor, m_focus);
		const Caret high = std::max(m_anchor, m_focus);
		const Caret start{ index, part, 0 };
		const Caret end{ index, part, size };

		if (low == high || high <= start || low >= end)
			return { 0, 0 };

		return { low < start ? 0 : low.offset, high > end ? size : high.offset };
	};

	std::string TranscriptView::SelectedText() const
	{
		const Caret low = std::min(m_anchor, m_focus);
		const Caret high = std::max(m_anchor, m_focus);
		std::string copied;

		const auto append = [&copied](std::string_view text, std::pair<size_t, size_t> range)
		{
			if (range.first == range.second)
				return;

			if (!copied.empty())
				copied += "\n\n";

			copied.append(text, range.first, range.second - range.first);
		};

		for (size_t index = low.block; index <= high.block && index < m_source->Count(); index++)
		{
			const Row row = m_source->At(index);

			if (!IsFolding(row))
				append(row.text, SelectedIn(index, Part::Body, row.text.size()));
			else if (row.open)
				append(row.detail, SelectedIn(index, Part::Detail, row.detail.size()));
		};

		return copied;
	};

	void TranscriptView::CopyHovered()
	{
		const std::string selected = SelectedText();

		if (!selected.empty())
			ImGui::SetClipboardText(selected.c_str());
		else if (m_hovered < m_source->Count())
		{
			const Row row = m_source->At(m_hovered);
			const std::string_view copied = IsFolding(row) && row.open ? row.detail : row.text;

			ImGui::SetClipboardText(std::string(copied).c_str());
		};
	};

	size_t TranscriptView::BlockAt(float y) const
	{
		const auto after = std::upper_bound(m_laid.begin(), m_laid.end(), y,
			[](float top, const Laid& laid) { return top < laid.top; });

		return after == m_laid.begin() ? 0 : (size_t)(after - m_laid.begin()) - 1;
	};

	TranscriptView::Caret TranscriptView::CaretAt(ImVec2 point) const
	{
		if (m_laid.empty())
			return { };

		const size_t index = BlockAt(point.y);
		const Row row = m_source->At(index);
		const Laid& laid = m_laid[index];
		const float line = ImGui::GetTextLineHeightWithSpacing();

		if (IsFolding(row))
		{
			if (!row.open)
				return { index, Part::Detail, 0 };

			const ImVec2 origin = DetailOrigin();

			return { index, Part::Detail, OffsetAt(laid.detail, row.detail,
				point.x - origin.x, point.y - laid.top - origin.y, line, ChatInput::FontMeasure()) };
		};

		const float inset = BodyInset(row);

		return { index, Part::Body, OffsetAt(laid.body, row.text,
			point.x - inset, point.y - laid.top - inset, line, ChatInput::FontMeasure()) };
	};

	bool TranscriptView::LinkAt(ImVec2 point, size_t& index) const
	{
		if (m_laid.empty())
			return false;

		index = BlockAt(point.y);

		const Row row = m_source->At(index);

		if (!row.link)
			return false;

		const float inset = BodyInset(row);

		return OnText(m_laid[index].body, row.text,
			point.x - inset, point.y - m_laid[index].top - inset, ImGui::GetTextLineHeightWithSpacing(), ChatInput::FontMeasure());
	};

	void TranscriptView::HandleMouse(ImVec2 origin)
	{
		const ImVec2 mouse = ImGui::GetMousePos();
		const ImVec2 point = { mouse.x - origin.x, mouse.y - origin.y };

		if (ImGui::IsWindowHovered())
		{
			m_hovered = BlockAt(point.y);

			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			{
				m_anchor = m_focus = CaretAt(point);
				m_selecting = true;
			};
		};

		if (m_selecting)
		{
			m_focus = CaretAt(point);

			const float top = ImGui::GetWindowPos().y;
			const float bottom = top + ImGui::GetWindowHeight();
			const float step = edge_scroll_speed * ImGui::GetIO().DeltaTime;

			if (mouse.y < top)
				ImGui::SetScrollY(ImGui::GetScrollY() - step);
			else if (mouse.y > bottom)
				ImGui::SetScrollY(ImGui::GetScrollY() + step);

			if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
			{
				m_selecting = false;

				const bool clicked = m_anchor == m_focus && m_hovered < m_source->Count();

				if (clicked)
					for (const ShownFigure& figure : m_figures)
						if (ImGui::IsMouseHoveringRect(figure.min, figure.max))
							Images::Open(figure.path);

				size_t linked = 0;

				if (clicked && LinkAt(point, linked))
					m_source->Activate(linked);

				if (clicked)
				{
					const Row row = m_source->At(m_hovered);

					if (IsFolding(row) && point.y - m_laid[m_hovered].top < ImGui::GetTextLineHeightWithSpacing())
						m_source->SetOpen(m_hovered, !row.open);
				};
			};
		};

		if (ImGui::IsWindowFocused() && ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_C))
			CopyHovered();

		if (ImGui::BeginPopupContextWindow("Copy", ImGuiPopupFlags_MouseButtonRight))
		{
			if (ImGui::MenuItem("Copy"))
				CopyHovered();

			ImGui::EndPopup();
		};
	};

	void TranscriptView::DrawLayout(
		const Layout& layout,
		std::string_view text,
		size_t index,
		Part part,
		ImVec2 origin,
		float width)
	{
		ImDrawList* const draw = ImGui::GetWindowDrawList();
		ImFont* const font = ImGui::GetFont();
		const float size = ImGui::GetFontSize();
		const float line = ImGui::GetTextLineHeightWithSpacing();
		const float clipTop = draw->GetClipRectMin().y - line;
		const float clipBottom = draw->GetClipRectMax().y;
		const Row row = m_source->At(index);
		const ImU32 plain = row.colour;
		const bool link = row.link;
		const Measure measure = ChatInput::FontMeasure();
		const std::pair<size_t, size_t> selected = SelectedIn(index, part, text.size());
		const char* const source = text.data();

		auto visible = std::ranges::lower_bound(layout.pieces, clipTop - origin.y, { }, &Piece::y);

		if (visible != layout.pieces.begin() && std::prev(visible)->style == Style::Image)
			visible--;

		for (const Piece& piece : std::ranges::subrange(visible, layout.pieces.end()))
		{
			const ImVec2 at = { origin.x + piece.x, origin.y + piece.y };

			if (at.y > clipBottom)
				break;

			if (piece.style == Style::Bullet)
			{
				draw->AddCircleFilled({ at.x + bullet_radius, at.y + size / 2 }, bullet_radius, plain);
				continue;
			};

			const std::string_view words(source + piece.begin, piece.end - piece.begin);

			if (piece.style == Style::Image)
			{
				DrawFigure(ShownPath(row, FigureFile(words)), at, width);
				continue;
			};

			if (piece.style == Style::Fence)
				draw->AddRectFilled({ origin.x, at.y }, { origin.x + width, at.y + line }, code_back);
			else if (piece.style == Style::Code)
				draw->AddRectFilled(at, { at.x + measure(words), at.y + line }, code_back);

			const size_t from = std::clamp<size_t>(selected.first, piece.begin, piece.end);
			const size_t to = std::clamp<size_t>(selected.second, piece.begin, piece.end);

			if (from < to)
			{
				const float left = at.x + measure(std::string_view(source + piece.begin, from - piece.begin));
				const float right = at.x + measure(std::string_view(source + piece.begin, to - piece.begin));
				draw->AddRectFilled({ left, at.y }, { right, at.y + line }, selection);
			};

			draw->AddText(font, size, at, ColorOf(piece.style, plain), source + piece.begin, source + piece.end);

			if (link)
				draw->AddLine({ at.x, at.y + size + link_line_drop }, { at.x + measure(words), at.y + size + link_line_drop }, plain);
		};
	};

	float TranscriptView::FigureSpace(const std::string& path, float width)
	{
		const ImageCache::Entry& entry = m_images->Find(path);
		const float line = ImGui::GetTextLineHeightWithSpacing();

		return (entry.state == ImageCache::State::Ready ? Fit(entry, { width, figure_max_height }).y : line) + figure_gap;
	};

	float TranscriptView::ImagesHeight(const std::vector<std::string>& images, float width)
	{
		float height = 0;

		for (const std::string& path : images)
			height += FigureSpace(path, width);

		return height;
	};

	void TranscriptView::DrawFigure(const std::string& path, ImVec2 at, float width)
	{
		ImDrawList* const draw = ImGui::GetWindowDrawList();
		const ImageCache::Entry& known = m_images->Find(path);

		if (known.state == ImageCache::State::Failed)
		{
			draw->PushClipRect(at, { at.x + width, at.y + ImGui::GetTextLineHeightWithSpacing() }, true);
			draw->AddText(at, error_text, known.error.c_str());
			draw->PopClipRect();
			return;
		};

		if (known.state != ImageCache::State::Ready)
			return;

		const ImVec2 size = Fit(known, { width, figure_max_height });
		const ImVec2 end = { at.x + size.x, at.y + size.y };

		if (end.y < draw->GetClipRectMin().y || at.y > draw->GetClipRectMax().y)
			return;

		const ImageCache::Entry& entry = m_images->Use(path);

		if (entry.texture != nullptr)
			draw->AddImage(entry.texture->GetTexRef(), at, end);
		else draw->AddRectFilled(at, end, code_back);

		m_figures.push_back({ at, end, path });
	};

	void TranscriptView::DrawImages(const std::vector<std::string>& images, ImVec2 at, float width)
	{
		for (const std::string& path : images)
		{
			DrawFigure(path, at, width);
			at.y += FigureSpace(path, width);
		};
	};

	void TranscriptView::DrawBlock(size_t index, ImVec2 origin, float width)
	{
		const Row row = m_source->At(index);
		const Laid& laid = m_laid[index];
		const ImVec2 at = { origin.x, origin.y + laid.top };
		ImDrawList* const draw = ImGui::GetWindowDrawList();

		if (IsFolding(row))
		{
			const float line = ImGui::GetTextLineHeightWithSpacing();
			const std::string name = (row.open ? "v " : "> ") + m_source->Header(index);

			draw->PushClipRect(at, { at.x + width, at.y + line }, true);
			draw->AddText(at, row.headerColour, name.c_str());

			const float nameWidth = ImGui::CalcTextSize(name.c_str()).x + indent_width;
			draw->AddText({ at.x + nameWidth, at.y }, ImGui::GetColorU32(ImGuiCol_TextDisabled), row.text.data(), row.text.data() + row.text.size());
			draw->PopClipRect();

			if (row.open)
			{
				const ImVec2 detail = DetailOrigin();
				DrawLayout(laid.detail, row.detail, index, Part::Detail, { at.x + detail.x, at.y + detail.y }, width - detail.x);
				DrawImages(ImagesOf(row), { at.x + detail.x, at.y + detail.y + laid.detail.height }, width - detail.x);
			};

			return;
		};

		const float inset = BodyInset(row);

		if (row.boxed)
			draw->AddRectFilled(at, { at.x + width, at.y + laid.height }, user_box, box_padding);

		DrawLayout(laid.body, row.text, index, Part::Body, { at.x + inset, at.y + inset }, width - inset * 2);
		DrawImages(ImagesOf(row), { at.x + inset, at.y + inset + laid.body.height }, width - inset * 2);
	};

	void TranscriptView::Draw(Source& source, ImageCache& images, float footer, bool working)
	{
		m_source = &source;
		m_images = &images;

		if (!ImGui::BeginChild("Transcript", { 0, -footer }))
		{
			ImGui::EndChild();
			return;
		};

		if (ImGui::GetScrollY() < ImGui::GetScrollMaxY() - stick_slack)
			m_stick = false;
		else if (ImGui::GetScrollMaxY() > 0)
			m_stick = true;

		const float width = ImGui::GetContentRegionAvail().x;
		const ImVec2 origin = ImGui::GetCursorScreenPos();

		Relay(width);

		const float scroll = ImGui::GetScrollY();
		const float bottom = scroll + ImGui::GetWindowHeight();

		m_figures.clear();

		const auto first = std::upper_bound(m_laid.begin(), m_laid.end(), scroll,
			[](float y, const Laid& laid) { return y < laid.top; });

		for (size_t index = first == m_laid.begin() ? 0 : (size_t)(first - m_laid.begin()) - 1;
			index < m_laid.size() && m_laid[index].top <= bottom; index++)
			DrawBlock(index, origin, width);

		const float line = ImGui::GetTextLineHeightWithSpacing();
		const float end = m_laid.empty() ? 0 : m_laid.back().top + m_laid.back().height + block_gap;

		if (working)
		{
			const std::string label = std::string("Working") + std::string((size_t)ImGui::GetTime() % (working_dots + 1), '.');
			ImGui::GetWindowDrawList()->AddText({ origin.x, origin.y + end },
				ImGui::GetColorU32(ImGuiCol_TextDisabled), label.c_str());
		};

		ImGui::Dummy({ width, end + line });

		HandleMouse(origin);

		size_t linked = 0;

		if (ImGui::IsWindowHovered() && LinkAt({ ImGui::GetMousePos().x - origin.x, ImGui::GetMousePos().y - origin.y }, linked))
			ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

		if (ImGui::IsWindowHovered())
			for (const ShownFigure& figure : m_figures)
				if (ImGui::IsMouseHoveringRect(figure.min, figure.max))
					ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

		if (m_stick)
			ImGui::SetScrollHereY(1.0f);

		ImGui::EndChild();
	};
};
