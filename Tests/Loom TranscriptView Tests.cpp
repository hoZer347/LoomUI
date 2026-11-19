#include "doctest.h"

#include "ImageCache.h"
#include "TranscriptView.h"

#include "Testing/Headless.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace Loom;
using Kind = TranscriptView::Row::Kind;

#pragma region Harness

namespace
{
	constexpr const char* window_name = "View";
	constexpr const char* transcript_name = "Transcript";
	constexpr const char* untouched = "untouched";
	constexpr ImVec2 display = { 800, 600 };
	constexpr float footer_height = 120;
	constexpr int settle_frames = 3;
	constexpr int edge_frames = 10;
	constexpr float edge = 2;
	constexpr int long_rows = 200;
	constexpr float wheel_up = 3;
	constexpr float wheel_to_bottom = -100000;
	constexpr float stick_slack = 4;
	constexpr float lead_width = 40;
	constexpr float pad_height = 10;

	struct Item final
	{
		Kind kind = Kind::Plain;
		std::string text;
		std::string detail;
		std::string header;
		bool open = false;
		bool boxed = false;
		bool link = false;
		bool hidden = false;
		float lead = 0;
		float pad = 0;
		uint32_t revision = 0;
	};

	struct Lead final
	{
		size_t index = 0;
		ImVec2 at{ };
	};

	// A test adapter: rows held as strings, recording what the view asks of them.
	struct Rows final : TranscriptView::Source
	{
		size_t Count() const override { return lines.size(); };

		TranscriptView::Row At(size_t index) const override
		{
			const Item& line = lines[index];
			TranscriptView::Row row;
			row.kind = line.kind;
			row.revision = line.revision;
			row.open = line.open;
			row.boxed = line.boxed;
			row.link = line.link;
			row.hidden = line.hidden;
			row.lead = line.lead;
			row.pad = line.pad;
			row.text = line.text;
			row.detail = line.detail;
			row.colour = IM_COL32_WHITE;
			row.headerColour = IM_COL32_WHITE;

			return row;
		};

		std::string Header(size_t index) const override { return lines[index].header; };

		void SetOpen(
			size_t index,
			bool open) override
		{
			toggled.push_back(index);
			lines[index].open = open;
			lines[index].revision++;
		};

		void Activate(size_t index) override { activated.push_back(index); };

		void DrawLead(
			size_t index,
			ImVec2 at) const override
		{
			leads.push_back({ index, at });
		};

		Item& Add(
			Kind kind,
			std::string text,
			std::string detail = { },
			bool open = false)
		{
			Item line;
			line.kind = kind;
			line.text = std::move(text);
			line.detail = std::move(detail);
			line.header = "Read";
			line.open = open;
			lines.push_back(std::move(line));

			return lines.back();
		};

		std::vector<Item> lines;
		std::vector<size_t> toggled;
		std::vector<size_t> activated;
		mutable std::vector<Lead> leads;
	};

	struct View final
	{
		View()
		{
			m_screen.Clipboard() = untouched;
			Frames(settle_frames);
		};

		View(const View&) = delete;
		View& operator=(const View&) = delete;

		void Frames(int count)
		{
			for (int frame = 0; frame < count; ++frame)
				Frame();
		};

		ImGuiWindow* Transcript() const
		{
			for (ImGuiWindow* const window : m_screen.Context()->Windows)
				if (std::string_view(window->Name).find(transcript_name) != std::string_view::npos)
					return window;

			return nullptr;
		};

		// A point in the transcript, from the top left of its content.
		ImVec2 At(ImVec2 offset) const
		{
			const ImVec2 origin = Transcript()->DC.CursorStartPos;

			return { origin.x + offset.x, origin.y + offset.y };
		};

		// The bottom right corner of the transcript, past every short row.
		ImVec2 Corner() const
		{
			const ImRect inner = Transcript()->InnerRect;

			return { inner.Max.x - edge, inner.Max.y - edge };
		};

		ImVec2 Middle() const
		{
			const ImRect inner = Transcript()->InnerRect;

			return { (inner.Min.x + inner.Max.x) / 2, (inner.Min.y + inner.Max.y) / 2 };
		};

		// A point in the footer, below the transcript.
		ImVec2 Footer() const
		{
			const ImRect inner = Transcript()->InnerRect;

			return { inner.Min.x + edge, inner.Max.y + footer_height / 2 };
		};

		void Move(ImVec2 point)
		{
			m_screen.Context()->IO.AddMousePosEvent(point.x, point.y);
			Frame();
		};

		void Press(ImVec2 point)
		{
			Move(point);
			m_screen.Context()->IO.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
			Frame();
		};

		void Release()
		{
			m_screen.Context()->IO.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
			Frame();
		};

		void ClickAt(ImVec2 point)
		{
			Press(point);
			Release();
		};

		void DragFrom(
			ImVec2 from,
			ImVec2 to)
		{
			Press(from);
			Move(to);
			Release();
		};

		void Copy()
		{
			ImGuiIO& io = m_screen.Context()->IO;
			io.AddKeyEvent(ImGuiMod_Ctrl, true);
			io.AddKeyEvent(ImGuiKey_C, true);
			Frame();
			io.AddKeyEvent(ImGuiKey_C, false);
			io.AddKeyEvent(ImGuiMod_Ctrl, false);
			Frame();
		};

		void Wheel(float rows)
		{
			m_screen.Context()->IO.AddMouseWheelEvent(0, rows);
			Frames(settle_frames);
		};

		void AddRows(int count)
		{
			for (int row = 0; row < count; ++row)
				rows.Add(Kind::Plain, "row " + std::to_string(row));

			Frames(settle_frames);
		};

		// What the source was asked to draw in front of rows in one frame.
		std::vector<Lead> Leads()
		{
			rows.leads.clear();
			Frame();

			return rows.leads;
		};

		bool Following() const { return Scroll() >= ScrollMax() - stick_slack; };
		float Height() const { return Transcript()->ContentSize.y; };
		float Scroll() const { return Transcript()->Scroll.y; };
		float ScrollMax() const { return Transcript()->ScrollMax.y; };
		std::string& Clipboard() const { return m_screen.Clipboard(); };
		float Line() const { return m_line; };
		ImGuiMouseCursor Cursor() const { return m_screen.Context()->MouseCursor; };

		Rows rows;
		TranscriptView view;
		bool working = false;

	private:
		void Frame()
		{
			m_screen.Window(window_name, [this]()
			{
				m_line = ImGui::GetTextLineHeightWithSpacing();
				view.Draw(rows, m_images, footer_height, working);
			});
		};

		const Loom::Testing::Headless m_screen{ Loom::Testing::Headless::Options{ display } };
		ImageCache m_images{ };
		float m_line = 0;
	};
};

#pragma endregion

TEST_SUITE("Loom TranscriptView")
{
	#pragma region Empty

	TEST_CASE("an empty view leaves the footer below it and copies nothing")
	{
		View view;

		REQUIRE(view.Transcript() != nullptr);
		CHECK(view.Transcript()->Size.y == doctest::Approx(display.y - ImGui::GetStyle().WindowPadding.y * 2 - footer_height));

		view.ClickAt(view.At({ 1, 1 }));
		view.Copy();
		view.DragFrom(view.At({ 1, 1 }), view.Corner());
		view.Copy();

		CHECK(view.Clipboard() == untouched);
		CHECK(view.rows.toggled.empty());
		CHECK(view.rows.activated.empty());
	};

	TEST_CASE("working draws a label after the last row, even with none")
	{
		View view;
		const int idle = view.Transcript()->DrawList->VtxBuffer.Size;

		view.working = true;
		view.Frames(settle_frames);

		CHECK(view.Transcript()->DrawList->VtxBuffer.Size > idle);
	};

	#pragma endregion

	#pragma region Selection

	TEST_CASE("a drag across rows copies each row's text with a blank line between, skipping a closed folding row")
	{
		View view;
		view.rows.Add(Kind::Plain, "hello").boxed = true;
		view.rows.Add(Kind::Markdown, "alpha");
		view.rows.Add(Kind::Folding, "file.txt", "secret");
		view.rows.Add(Kind::Plain, "beta");
		view.Frames(settle_frames);

		view.DragFrom(view.At({ 1, 1 }), view.Corner());
		view.Copy();

		CHECK(view.Clipboard() == "hello\n\nalpha\n\nbeta");
	};

	TEST_CASE("a drag across an open folding row copies its detail, not its header or text")
	{
		View view;
		view.rows.Add(Kind::Markdown, "alpha");
		view.rows.Add(Kind::Folding, "file.txt", "secret", true);
		view.rows.Add(Kind::Markdown, "beta");
		view.Frames(settle_frames);

		view.DragFrom(view.At({ 1, 1 }), view.Corner());
		view.Copy();

		CHECK(view.Clipboard() == "alpha\n\nsecret\n\nbeta");
	};

	TEST_CASE("a drag upwards copies the same as one downwards")
	{
		View view;
		view.rows.Add(Kind::Markdown, "alpha");
		view.rows.Add(Kind::Plain, "beta");
		view.Frames(settle_frames);

		view.DragFrom(view.Corner(), view.At({ 1, 1 }));
		view.Copy();

		CHECK(view.Clipboard() == "alpha\n\nbeta");
	};

	TEST_CASE("a click without a drag copies the whole row under the mouse")
	{
		View view;
		view.rows.Add(Kind::Markdown, "alpha");
		view.rows.Add(Kind::Plain, "beta");
		view.Frames(settle_frames);

		view.ClickAt(view.At({ 1, 1 }));
		view.Copy();

		CHECK(view.Clipboard() == "alpha");

		view.ClickAt(view.Corner());
		view.Copy();

		CHECK(view.Clipboard() == "beta");
	};

	TEST_CASE("a click copies a closed folding row's text and an open one's detail")
	{
		View view;
		view.rows.Add(Kind::Folding, "file.txt", "secret");
		view.Frames(settle_frames);

		view.ClickAt(view.Corner());
		view.Copy();

		CHECK(view.Clipboard() == "file.txt");

		view.rows.lines[0].open = true;
		view.rows.lines[0].revision++;
		view.Frames(settle_frames);
		view.ClickAt(view.Corner());
		view.Copy();

		CHECK(view.Clipboard() == "secret");
	};

	TEST_CASE("a row's new text is laid out again when its revision moves on")
	{
		View view;
		view.rows.Add(Kind::Markdown, "alpha");
		view.Frames(settle_frames);

		view.rows.lines[0].text = "alpha and more";
		view.rows.lines[0].revision++;
		view.Frames(settle_frames);
		view.DragFrom(view.At({ 1, 1 }), view.Corner());
		view.Copy();

		CHECK(view.Clipboard() == "alpha and more");
	};

	TEST_CASE("rows removed under a selection copy what is left")
	{
		View view;
		view.rows.Add(Kind::Markdown, "alpha");
		view.rows.Add(Kind::Plain, "beta");
		view.rows.Add(Kind::Plain, "gamma");
		view.Frames(settle_frames);

		view.DragFrom(view.At({ 1, 1 }), view.Corner());
		view.Move(view.Footer());
		view.rows.lines.resize(1);
		view.Frames(settle_frames);
		view.Copy();

		CHECK(view.Clipboard() == "alpha");
	};

	TEST_CASE("a hovered row that was removed copies nothing")
	{
		View view;
		view.rows.Add(Kind::Markdown, "alpha");
		view.rows.Add(Kind::Plain, "beta");
		view.Frames(settle_frames);

		view.ClickAt(view.Corner());
		view.Move(view.Footer());
		view.rows.lines.clear();
		view.Frames(settle_frames);
		view.Copy();

		CHECK(view.Clipboard() == untouched);
	};

	TEST_CASE("reset forgets the selection")
	{
		View view;
		view.rows.Add(Kind::Markdown, "alpha");
		view.rows.Add(Kind::Plain, "beta");
		view.Frames(settle_frames);

		view.DragFrom(view.At({ 1, 1 }), view.Corner());
		view.view.Reset();
		view.Frames(settle_frames);
		view.Copy();

		CHECK(view.Clipboard() == "beta");
	};

	#pragma endregion

	#pragma region Folding and links

	TEST_CASE("clicking a folding row's header asks to open it and clicking again to close it")
	{
		View view;
		view.rows.Add(Kind::Folding, "file.txt", "secret");
		view.rows.Add(Kind::Plain, "beta");
		view.Frames(settle_frames);

		view.ClickAt(view.At({ 1, view.Line() / 2 }));

		REQUIRE(view.rows.toggled == std::vector<size_t>{ 0 });
		CHECK(view.rows.lines[0].open);

		view.ClickAt(view.At({ 1, view.Line() / 2 }));

		CHECK(view.rows.toggled == std::vector<size_t>{ 0, 0 });
		CHECK_FALSE(view.rows.lines[0].open);
	};

	TEST_CASE("a click below a folding row's header, or on a row that does not fold, opens nothing")
	{
		View view;
		view.rows.Add(Kind::Folding, "file.txt", "secret", true);
		view.Frames(settle_frames);

		view.ClickAt(view.At({ 1, view.Line() * 1.5f }));

		CHECK(view.rows.toggled.empty());

		view.rows.lines[0].kind = Kind::Plain;
		view.rows.lines[0].revision++;
		view.Frames(settle_frames);
		view.ClickAt(view.At({ 1, view.Line() / 2 }));

		CHECK(view.rows.toggled.empty());
	};

	TEST_CASE("a drag starting on a folding row's header does not open it")
	{
		View view;
		view.rows.Add(Kind::Folding, "file.txt", "secret");
		view.rows.Add(Kind::Plain, "beta");
		view.Frames(settle_frames);

		view.DragFrom(view.At({ 1, view.Line() / 2 }), view.Corner());

		CHECK(view.rows.toggled.empty());
	};

	TEST_CASE("opening a folding row pushes the rows after it down")
	{
		View view;
		view.rows.Add(Kind::Folding, "file.txt", "one\ntwo\nthree\nfour");
		view.rows.Add(Kind::Plain, "beta");
		view.Frames(settle_frames);

		const ImVec2 below = view.At({ 1, view.Line() * 3 });
		view.ClickAt(below);
		view.Copy();

		CHECK(view.Clipboard() == "beta");

		view.rows.lines[0].open = true;
		view.rows.lines[0].revision++;
		view.Frames(settle_frames);
		view.ClickAt(below);
		view.Copy();

		CHECK(view.Clipboard() == "one\ntwo\nthree\nfour");
	};

	TEST_CASE("a click on a link row's text activates it, and a click off the text does not")
	{
		View view;
		view.rows.Add(Kind::Plain, "Task finished").link = true;
		view.Frames(settle_frames);

		view.ClickAt(view.Corner());

		CHECK(view.rows.activated.empty());

		view.ClickAt(view.At({ 1, view.Line() / 2 }));

		CHECK(view.rows.activated == std::vector<size_t>{ 0 });
	};

	TEST_CASE("hovering a link row's text shows the hand")
	{
		View view;
		view.rows.Add(Kind::Plain, "Task finished").link = true;
		view.Frames(settle_frames);

		view.Move(view.At({ 1, view.Line() / 2 }));

		CHECK(view.Cursor() == ImGuiMouseCursor_Hand);

		view.Move(view.Corner());

		CHECK(view.Cursor() != ImGuiMouseCursor_Hand);
	};

	TEST_CASE("a row that is not a link never activates")
	{
		View view;
		view.rows.Add(Kind::Plain, "Plain note");
		view.Frames(settle_frames);

		view.ClickAt(view.At({ 1, view.Line() / 2 }));

		CHECK(view.rows.activated.empty());
	};

	#pragma endregion

	#pragma region Hidden rows and leads

	TEST_CASE("a hidden row takes no room, draws nothing in front and is left out of what is copied")
	{
		View view;
		view.rows.Add(Kind::Markdown, "alpha");
		view.rows.Add(Kind::Plain, "beta");
		view.Frames(settle_frames);
		const float shown = view.Height();

		Item& ghost = view.rows.Add(Kind::Plain, "ghost");
		view.Frames(settle_frames);

		CHECK(view.Height() > shown);

		ghost.hidden = true;
		view.Frames(settle_frames);

		CHECK(view.Height() == doctest::Approx(shown));

		for (const Lead& lead : view.Leads())
			CHECK(lead.index != 2);

		view.DragFrom(view.At({ 1, 1 }), view.Corner());
		view.Copy();

		CHECK(view.Clipboard() == "alpha\n\nbeta");
	};

	TEST_CASE("a hidden folding row takes no room")
	{
		View view;
		view.rows.Add(Kind::Plain, "beta");
		view.Frames(settle_frames);
		const float shown = view.Height();

		view.rows.Add(Kind::Folding, "file.txt", "secret", true).hidden = true;
		view.Frames(settle_frames);

		CHECK(view.Height() == doctest::Approx(shown));
	};

	TEST_CASE("a row's lead moves its text right and its pad sets it down")
	{
		View view;
		Item& note = view.rows.Add(Kind::Plain, "Task finished");
		note.link = true;
		view.Frames(settle_frames);
		const float flat = view.Height();

		note.lead = lead_width;
		note.pad = pad_height;
		view.Frames(settle_frames);

		CHECK(view.Height() == doctest::Approx(flat + pad_height * 2));

		view.ClickAt(view.At({ 1, pad_height + view.Line() / 2 }));

		CHECK(view.rows.activated.empty());

		view.ClickAt(view.At({ lead_width + 1, pad_height + view.Line() / 2 }));

		CHECK(view.rows.activated == std::vector<size_t>{ 0 });
	};

	TEST_CASE("a boxed row ignores its lead and pad")
	{
		View view;
		Item& said = view.rows.Add(Kind::Plain, "hello");
		said.boxed = true;
		view.Frames(settle_frames);
		const float boxed = view.Height();

		said.lead = lead_width;
		said.pad = pad_height;
		view.Frames(settle_frames);

		CHECK(view.Height() == doctest::Approx(boxed));
	};

	TEST_CASE("the source draws in front of each shown row that does not fold, at its top left")
	{
		View view;
		view.rows.Add(Kind::Plain, "alpha");
		view.rows.Add(Kind::Folding, "file.txt", "secret");
		view.rows.Add(Kind::Plain, "ghost").hidden = true;
		view.rows.Add(Kind::Markdown, "beta");
		view.Frames(settle_frames);

		const std::vector<Lead> leads = view.Leads();
		const ImVec2 origin = view.At({ 0, 0 });

		REQUIRE(leads.size() == 2);
		CHECK(leads[0].index == 0);
		CHECK(leads[0].at.x == doctest::Approx(origin.x));
		CHECK(leads[0].at.y == doctest::Approx(origin.y));
		CHECK(leads[1].index == 3);
		CHECK(leads[1].at.x == doctest::Approx(origin.x));
		CHECK(leads[1].at.y > origin.y + view.Line());
	};

	#pragma endregion

	#pragma region Scrolling

	TEST_CASE("the view follows the bottom as rows arrive")
	{
		View view;
		view.AddRows(long_rows);

		REQUIRE(view.ScrollMax() > 0);
		CHECK(view.Following());

		const float before = view.ScrollMax();
		view.AddRows(long_rows);

		CHECK(view.ScrollMax() > before);
		CHECK(view.Following());
	};

	TEST_CASE("scrolling up stops following, and scrolling back to the bottom follows again")
	{
		View view;
		view.AddRows(long_rows);
		view.Move(view.Middle());
		view.Wheel(wheel_up);

		const float held = view.Scroll();
		REQUIRE_FALSE(view.Following());

		view.AddRows(long_rows);

		CHECK(view.Scroll() == held);

		view.Wheel(wheel_to_bottom);
		view.AddRows(long_rows);

		CHECK(view.Following());
	};

	TEST_CASE("dragging above a scrolled-up view scrolls it further up, and the drag ends with the button")
	{
		View view;
		view.AddRows(long_rows);
		view.Move(view.Middle());
		view.Wheel(wheel_up);

		const float before = view.Scroll();
		const ImRect inner = view.Transcript()->InnerRect;

		view.Press(view.Middle());
		view.Move({ inner.Min.x + edge, inner.Min.y - edge });
		view.Frames(edge_frames);

		CHECK(view.Scroll() < before);

		view.Release();
		view.Frames(settle_frames);

		const float released = view.Scroll();
		view.Frames(edge_frames);

		CHECK(view.Scroll() == released);
	};

	TEST_CASE("dragging below a scrolled-up view scrolls it down")
	{
		View view;
		view.AddRows(long_rows);
		view.Move(view.Middle());
		view.Wheel(wheel_up);
		view.Wheel(wheel_up);

		const float before = view.Scroll();
		const ImRect inner = view.Transcript()->InnerRect;

		view.Press(view.Middle());
		view.Move({ inner.Min.x + edge, inner.Max.y + edge });
		view.Frames(edge_frames);

		CHECK(view.Scroll() > before);

		view.Release();
	};

	TEST_CASE("a drag scrolled out of view still copies every row it crossed")
	{
		View view;
		view.AddRows(long_rows);
		view.Move(view.Middle());
		view.Wheel(wheel_up);

		const ImRect inner = view.Transcript()->InnerRect;

		view.Press(view.Middle());
		view.Move({ inner.Min.x + edge, inner.Min.y - edge });
		view.Frames(edge_frames);
		view.Release();
		view.Copy();

		CHECK(view.Clipboard().find("\n\n") != std::string::npos);
		CHECK(view.Clipboard().find("row " + std::to_string(long_rows - 1)) == std::string::npos);
	};

	#pragma endregion
};
