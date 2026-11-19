#include "doctest.h"

#include "Widgets.h"

#include "Testing/Headless.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <chrono>
#include <functional>
#include <set>
#include <string>
#include <vector>

using namespace Loom;

#pragma region Harness

namespace
{
	constexpr const char* window_name = "Widgets";
	constexpr const char* table_name = "Table";
	constexpr const char* popup_id = "##Confirm";
	constexpr const char* question = "Stop this task?";
	constexpr const char* confirm_label = "Stop";
	constexpr const char* cancel_label = "Cancel";
	constexpr const char* cross_id = "##Cross";
	constexpr ImVec2 display = { 800, 600 };
	constexpr ImVec2 parked = { 100, 100 };
	constexpr ImVec2 away = { 790, 590 };
	constexpr int settle_frames = 4;
	constexpr float sweep_step = 2;
	constexpr int vertices_per_line = 4;
	constexpr int vertices_per_rect = 4;
	constexpr float line_offset = 0.5f;
	constexpr float half = 0.5f;
	constexpr float tolerance = 0.0001f;
	constexpr float cross_inset = 0.3f;
	constexpr float cross_thickness = 1.5f;
	constexpr ImVec4 hover_tone = { 0.92f, 0.43f, 0.39f, 1 };
	constexpr ImVec4 ok_tone = { 0.49f, 0.8f, 0.53f, 1 };
	constexpr ImVec4 warn_tone = { 0.9f, 0.8f, 0.4f, 1 };
	constexpr ImVec4 bad_tone = { 0.92f, 0.43f, 0.39f, 1 };

	bool Near(
		const ImVec4& left,
		const ImVec4& right)
	{
		return left.x == doctest::Approx(right.x).epsilon(tolerance)
			&& left.y == doctest::Approx(right.y).epsilon(tolerance)
			&& left.z == doctest::Approx(right.z).epsilon(tolerance)
			&& left.w == doctest::Approx(right.w).epsilon(tolerance);
	};

	ImVec2 Centre(
		const ImDrawVert* const vertices,
		const int count)
	{
		ImVec2 sum{ };

		for (int index = 0; index < count; ++index)
		{
			sum.x += vertices[index].pos.x;
			sum.y += vertices[index].pos.y;
		};

		return { sum.x / (float)count, sum.y / (float)count };
	};

	struct Harness final
	{
		Harness()
		{
			ImGui::GetIO().AddMousePosEvent(parked.x, parked.y);
		};

		void Frame(const std::function<void()>& draw)
		{
			m_screen.Window(window_name, draw);
		};

		void Settle(const std::function<void()>& draw)
		{
			for (int frame = 0; frame < settle_frames; ++frame)
				Frame(draw);
		};

		ImGuiWindow* Popup() const
		{
			ImGuiContext* const context = m_screen.Context();

			return context->OpenPopupStack.empty() ? nullptr : context->OpenPopupStack.back().Window;
		};

		bool Hover(
			const ImRect& area,
			const ImGuiID id,
			const std::function<void()>& draw)
		{
			return m_screen.Hover(area, id, sweep_step, Framed(draw));
		};

		void Click(const std::function<void()>& draw)
		{
			m_screen.Press(ImGuiMouseButton_Left, Framed(draw));
		};

		bool ClickInPopup(
			const char* label,
			const std::function<void()>& draw)
		{
			ImGuiWindow* const popup = Popup();

			if (popup == nullptr || !Hover(popup->Rect(), popup->GetID(label), draw))
				return false;

			Click(draw);
			return true;
		};

	private:
		Loom::Testing::Headless::Draw Framed(const std::function<void()>& draw)
		{
			return [this, &draw]() { Frame(draw); };
		};

		const Loom::Testing::Headless m_screen{ Loom::Testing::Headless::Options{ display } };
	};
};

#pragma endregion

#pragma region Text

TEST_SUITE("Loom Widgets")
{
	TEST_CASE("WholePercent rounds a share to a whole percentage")
	{
		CHECK(Widgets::WholePercent(0) == "0%");
		CHECK(Widgets::WholePercent(0.456) == "46%");
		CHECK(Widgets::WholePercent(0.454) == "45%");
		CHECK(Widgets::WholePercent(1) == "100%");
		CHECK(Widgets::WholePercent(1.25) == "125%");
	};

	TEST_CASE("Age is how long before now a time was, and empty for no time")
	{
		using namespace std::chrono_literals;

		const std::chrono::system_clock::time_point now = std::chrono::system_clock::now();

		CHECK(Widgets::Age(now, { }).empty());
		CHECK(Widgets::Age(now, now) == "0s");
		CHECK(Widgets::Age(now, now - 45s) == "45s");
		CHECK(Widgets::Age(now, now - 90min) == "2h");
		CHECK(Widgets::Age(now, now + 10s) == "0s");
	};

	TEST_CASE("Flip puts an id in a set when absent and takes it out when present")
	{
		std::set<std::string> ids = { "a" };

		Widgets::Flip(ids, "b");
		CHECK(ids == std::set<std::string>{ "a", "b" });

		Widgets::Flip(ids, "a");
		CHECK(ids == std::set<std::string>{ "b" });
	};

	TEST_CASE("FirstLine stops at the first line break of either kind")
	{
		CHECK(Widgets::FirstLine("one") == "one");
		CHECK(Widgets::FirstLine("one\ntwo") == "one");
		CHECK(Widgets::FirstLine("one\r\ntwo") == "one");
		CHECK(Widgets::FirstLine("\nafter").empty());
		CHECK(Widgets::FirstLine("").empty());
	};
}

#pragma endregion

#pragma region Tones

TEST_SUITE("Loom Widgets")
{
	TEST_CASE("ThresholdTone is ok below low, warn from low and bad from critical")
	{
		constexpr double low = 0.7;
		constexpr double critical = 0.9;

		CHECK(Near(Widgets::ThresholdTone(0, low, critical, ok_tone, warn_tone, bad_tone), ok_tone));
		CHECK(Near(Widgets::ThresholdTone(0.69, low, critical, ok_tone, warn_tone, bad_tone), ok_tone));
		CHECK(Near(Widgets::ThresholdTone(low, low, critical, ok_tone, warn_tone, bad_tone), warn_tone));
		CHECK(Near(Widgets::ThresholdTone(critical, low, critical, ok_tone, warn_tone, bad_tone), bad_tone));
		CHECK(Near(Widgets::ThresholdTone(1.5, low, critical, ok_tone, warn_tone, bad_tone), bad_tone));
	};
}

#pragma endregion

#pragma region Drawing

namespace
{
	constexpr float label_width = 80;

	struct DrawnBar final
	{
		ImRect item;
		ImRect bar;
		ImU32 left = 0;
		ImU32 right = 0;
		ImU32 from = 0;
		ImU32 to = 0;
		float start = 0;
		float frame = 0;
		float spacing = 0;
	};

	DrawnBar DrawBar(
		const char* label,
		const double share)
	{
		Harness harness;
		DrawnBar drawn;

		harness.Frame([&]
		{
			drawn.start = ImGui::GetCursorScreenPos().x;
			drawn.frame = ImGui::GetFrameHeight();
			drawn.spacing = ImGui::GetStyle().ItemSpacing.x;
			drawn.from = ImGui::GetColorU32(ok_tone);
			drawn.to = ImGui::GetColorU32(bad_tone);

			Widgets::SlimBar(label, label_width, share, ok_tone, bad_tone);

			const ImDrawList* const draw = ImGui::GetWindowDrawList();
			const ImDrawVert* const fill = draw->VtxBuffer.Data + draw->VtxBuffer.Size - vertices_per_rect;

			drawn.item = { ImGui::GetItemRectMin(), ImGui::GetItemRectMax() };
			drawn.bar = { fill[0].pos, fill[2].pos };
			drawn.left = fill[0].col;
			drawn.right = fill[1].col;
		});

		return drawn;
	};
};

TEST_SUITE("Loom Widgets")
{
	TEST_CASE("a slim bar fills its share of the width from one colour to the other")
	{
		constexpr double share = 0.25;

		const DrawnBar drawn = DrawBar("Week", share);

		CHECK(drawn.item.GetHeight() == doctest::Approx(drawn.frame));
		CHECK(drawn.bar.Min.x == doctest::Approx(drawn.item.Min.x));
		CHECK(drawn.bar.Max.x == doctest::Approx(drawn.item.Min.x + drawn.item.GetWidth() * (float)share));
		CHECK(drawn.left == drawn.from);
		CHECK(drawn.right == drawn.to);
	};

	TEST_CASE("a slim bar sits in the middle of its row and is thinner than it")
	{
		const DrawnBar drawn = DrawBar("Week", half);

		CHECK(drawn.bar.GetHeight() > 0);
		CHECK(drawn.bar.GetHeight() < drawn.item.GetHeight());
		CHECK(drawn.bar.Min.y - drawn.item.Min.y == doctest::Approx(drawn.item.Max.y - drawn.bar.Max.y).epsilon(0.01));
	};

	TEST_CASE("a slim bar's share is clamped to its width")
	{
		const DrawnBar over = DrawBar("Week", 1.5);
		const DrawnBar under = DrawBar("Week", -0.5);

		CHECK(over.bar.Max.x == doctest::Approx(over.item.Max.x));
		CHECK(under.bar.Max.x == doctest::Approx(under.item.Min.x));
	};

	TEST_CASE("slim bars with labels of different lengths start at the same place")
	{
		const DrawnBar shortLabel = DrawBar("5h", half);
		const DrawnBar longLabel = DrawBar("Seven days", half);

		CHECK(shortLabel.item.Min.x == doctest::Approx(longLabel.item.Min.x));
		CHECK(shortLabel.item.Min.x == doctest::Approx(shortLabel.start + label_width + shortLabel.spacing));
	};

	TEST_CASE("a slim bar is the last item, so a tooltip can follow it")
	{
		Harness harness;
		ImGuiID last = 0;

		harness.Frame([&]
		{
			Widgets::SlimBar("Week", label_width, half, ok_tone, bad_tone);
			last = ImGui::GetItemID();
		});

		CHECK(last == ImGui::FindWindowByName(window_name)->GetID("Week"));
	};

	TEST_CASE("a dashed line runs its dashes on across a corner")
	{
		constexpr float length = 4;
		constexpr float gap = 3;
		const std::vector<ImVec2> points = { { 0, 0 }, { 10, 0 }, { 10, 10 } };
		const std::vector<ImVec2> centres = { { 2, 0 }, { 8.5f, 0 }, { 10, 0.5f }, { 10, 6 } };

		Harness harness;
		std::vector<ImVec2> drawn;
		ImU32 colour = 0;

		harness.Frame([&]
		{
			ImDrawList* const draw = ImGui::GetWindowDrawList();
			draw->Flags &= ~(ImDrawListFlags_AntiAliasedLines | ImDrawListFlags_AntiAliasedLinesUseTex);

			const int before = draw->VtxBuffer.Size;

			Widgets::DashedLine(draw, points, IM_COL32_WHITE, length, gap, cross_thickness);

			for (int index = before; index + vertices_per_line <= draw->VtxBuffer.Size; index += vertices_per_line)
				drawn.push_back(Centre(draw->VtxBuffer.Data + index, vertices_per_line));

			colour = draw->VtxBuffer.back().col;
		});

		REQUIRE(drawn.size() == centres.size());

		for (size_t index = 0; index < centres.size(); ++index)
		{
			CHECK(drawn[index].x == doctest::Approx(centres[index].x + line_offset));
			CHECK(drawn[index].y == doctest::Approx(centres[index].y + line_offset));
		};

		CHECK(colour == IM_COL32_WHITE);
	};

	TEST_CASE("a dashed line of fewer than two points draws nothing")
	{
		Harness harness;
		int added = -1;

		harness.Frame([&]
		{
			ImDrawList* const draw = ImGui::GetWindowDrawList();
			const int before = draw->VtxBuffer.Size;

			Widgets::DashedLine(draw, { }, IM_COL32_WHITE, 4, 3, cross_thickness);
			Widgets::DashedLine(draw, { { 5, 5 } }, IM_COL32_WHITE, 4, 3, cross_thickness);
			added = draw->VtxBuffer.Size - before;
		});

		CHECK(added == 0);
	};

	TEST_CASE("a section row is solid behind, tinted across, and sets its name in")
	{
		constexpr float inset = 0.3f;
		constexpr float tint = 0.35f;
		constexpr int columns = 2;

		Harness harness;
		float cellStart = 0;
		float nameStart = 0;
		float line = 0;
		ImU32 behind = 0;
		ImU32 across = 0;
		ImU32 solid = 0;
		ImU32 tinted = 0;

		harness.Frame([&]
		{
			if (!ImGui::BeginTable(table_name, columns))
				return;

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			cellStart = ImGui::GetCursorScreenPos().x;

			Widgets::SectionRow("Active", inset, tint);
			nameStart = ImGui::GetItemRectMin().x;
			line = ImGui::GetTextLineHeight();

			const ImGuiTable* const table = ImGui::GetCurrentTable();
			behind = table->RowBgColor[0];
			across = table->RowBgColor[1];

			ImVec4 window = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
			window.w = 1;
			solid = ImGui::GetColorU32(window);
			tinted = ImGui::GetColorU32(ImGuiCol_Header, tint);

			ImGui::EndTable();
		});

		CHECK(nameStart == doctest::Approx(cellStart + line * inset));
		CHECK(behind == solid);
		CHECK(across == tinted);
	};
}

#pragma endregion

#pragma region Buttons

namespace
{
	struct Stop final
	{
		void Draw()
		{
			if (open)
			{
				ImGui::OpenPopup(popup_id);
				open = false;
			};

			if (Widgets::ConfirmPopup(popup_id, question, confirm_label, cancel_label))
				++confirmed;

			shown = ImGui::IsPopupOpen(popup_id);
		};

		bool open = false;
		bool shown = false;
		int confirmed = 0;
	};
};

TEST_SUITE("Loom Widgets")
{
	TEST_CASE("a cross button is a line-high square drawn in two strokes")
	{
		Harness harness;
		ImVec2 size{ };
		float line = 0;
		int added = 0;

		harness.Frame([&]
		{
			ImDrawList* const draw = ImGui::GetWindowDrawList();
			draw->Flags &= ~(ImDrawListFlags_AntiAliasedLines | ImDrawListFlags_AntiAliasedLinesUseTex);

			const int before = draw->VtxBuffer.Size;

			Widgets::CrossButton(cross_id, hover_tone, cross_inset, cross_thickness);
			size = ImGui::GetItemRectSize();
			line = ImGui::GetTextLineHeight();
			added = draw->VtxBuffer.Size - before;
		});

		CHECK(size.x == doctest::Approx(line));
		CHECK(size.y == doctest::Approx(line));
		CHECK(added == 2 * vertices_per_line);
	};

	TEST_CASE("a cross button is disabled text until hovered, then the hover colour, and true when clicked")
	{
		Harness harness;
		ImU32 colour = 0;
		ImU32 disabled = 0;
		ImU32 hovered = 0;
		ImRect rect;
		int presses = 0;

		const std::function<void()> draw = [&]
		{
			ImDrawList* const list = ImGui::GetWindowDrawList();
			list->Flags &= ~(ImDrawListFlags_AntiAliasedLines | ImDrawListFlags_AntiAliasedLinesUseTex);

			if (Widgets::CrossButton(cross_id, hover_tone, cross_inset, cross_thickness))
				++presses;

			rect = { ImGui::GetItemRectMin(), ImGui::GetItemRectMax() };
			colour = list->VtxBuffer.back().col;
			disabled = ImGui::GetColorU32(ImGuiCol_TextDisabled);
			hovered = ImGui::GetColorU32(hover_tone);
		};

		ImGui::GetIO().AddMousePosEvent(away.x, away.y);
		harness.Settle(draw);
		CHECK(colour == disabled);
		CHECK(presses == 0);

		REQUIRE(harness.Hover(rect, ImGui::FindWindowByName(window_name)->GetID(cross_id), draw));
		harness.Frame(draw);
		CHECK(colour == hovered);

		harness.Click(draw);
		CHECK(presses == 1);
	};

	TEST_CASE("a confirm popup draws nothing and confirms nothing until opened")
	{
		Harness harness;
		Stop stop;

		harness.Settle([&] { stop.Draw(); });

		CHECK_FALSE(stop.shown);
		CHECK(harness.Popup() == nullptr);
		CHECK(stop.confirmed == 0);
	};

	TEST_CASE("a confirm popup confirms once when its confirm button is pressed, then closes")
	{
		Harness harness;
		Stop stop;
		const std::function<void()> draw = [&] { stop.Draw(); };

		stop.open = true;
		harness.Settle(draw);
		REQUIRE(stop.shown);
		CHECK(stop.confirmed == 0);

		REQUIRE(harness.ClickInPopup(confirm_label, draw));
		CHECK(stop.confirmed == 1);
		CHECK_FALSE(stop.shown);

		harness.Settle(draw);
		CHECK(stop.confirmed == 1);
	};

	TEST_CASE("a confirm popup closes without confirming when cancelled")
	{
		Harness harness;
		Stop stop;
		const std::function<void()> draw = [&] { stop.Draw(); };

		stop.open = true;
		harness.Settle(draw);
		REQUIRE(stop.shown);

		REQUIRE(harness.ClickInPopup(cancel_label, draw));
		CHECK_FALSE(stop.shown);
		CHECK(stop.confirmed == 0);
	};

	TEST_CASE("a confirm popup asks its question above its buttons")
	{
		Harness harness;
		Stop stop;
		float width = 0;
		float needed = 0;

		stop.open = true;
		harness.Settle([&]
		{
			stop.Draw();
			needed = ImGui::CalcTextSize(question).x;

			if (const ImGuiWindow* const popup = harness.Popup())
				width = popup->ContentSize.x;
		});

		REQUIRE(stop.shown);
		CHECK(width >= needed);
	};
}

#pragma endregion
