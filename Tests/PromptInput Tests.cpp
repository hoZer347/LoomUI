#include "doctest.h"

#include "ChatInput.h"

#include "Testing/Headless.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <string>
#include <string_view>

using namespace Loom::ChatInput;

#pragma region Helpers

namespace
{
	constexpr const char* window_name = "Prompt";
	constexpr const char* box_id = "##Prompt";
	constexpr ImVec2 display = { 800, 600 };
	constexpr ImVec2 box = { 200, 120 };
	constexpr int focus_frames = 3;
	constexpr size_t spare_bytes = 64;

	// A prompt box in an ImGui context of its own, with no window or renderer behind it.
	struct Box final
	{
		explicit Box(std::string_view shown)
			: text(shown)
		{
			text.reserve(text.size() + spare_bytes);

			for (int frame = 0; frame < focus_frames; ++frame)
				Frame(true);
		};

		Box(const Box&) = delete;
		Box& operator=(const Box&) = delete;

		void Place(
			int from,
			int to)
		{
			State().SetSelection(from, to);
		};

		void Place(int caret)
		{
			Place(caret, caret);
		};

		void Press(
			ImGuiKey key,
			ImGuiKey held = ImGuiKey_None)
		{
			ImGuiIO& io = ImGui::GetIO();

			if (held != ImGuiKey_None)
				io.AddKeyEvent(held, true);

			io.AddKeyEvent(key, true);
			Frame(false);
			io.AddKeyEvent(key, false);

			if (held != ImGuiKey_None)
				io.AddKeyEvent(held, false);

			Frame(false);
		};

		void Type(char letter)
		{
			ImGui::GetIO().AddInputCharacter((unsigned int)letter);
			Frame(false);
		};

		int Caret() const
		{
			return State().GetCursorPos();
		};

		int Anchor() const
		{
			return State().GetSelectionStart();
		};

		bool Selected() const
		{
			return State().HasSelection();
		};

		int RowLetters() const
		{
			return (int)(State().WrapWidth / FontMeasure()("a"));
		};

		std::string text;
		bool entered = false;

	private:
		ImGuiInputTextState& State() const
		{
			ImGuiInputTextState* const state = ImGui::GetInputTextState(m_id);

			REQUIRE(state != nullptr);

			return *state;
		};

		void Frame(bool focus)
		{
			m_screen.Frame([this, focus]()
			{
				ImGui::SetNextWindowPos({ 0, 0 });
				ImGui::SetNextWindowSize(display);
				ImGui::Begin(window_name);

				if (focus)
					ImGui::SetKeyboardFocusHere();

				m_id = ImGui::GetID(box_id);
				entered = Draw(box_id, text, box) || entered;

				ImGui::End();
			});
		};

		const Loom::Testing::Headless m_screen{ Loom::Testing::Headless::Options{ display } };
		ImGuiID m_id = 0;
	};
};

#pragma endregion

TEST_SUITE("PromptInput")
{
	constexpr std::string_view two_rows = "abcdef\nghijkl";
	constexpr int second_row = 7;
	constexpr int column = 3;

	TEST_CASE("up on the top row goes to the start")
	{
		Box prompt("hello world");

		prompt.Place(column);
		prompt.Press(ImGuiKey_UpArrow);

		CHECK(prompt.Caret() == 0);
		CHECK(prompt.Selected() == false);
	};

	TEST_CASE("up from a lower row keeps the column")
	{
		Box prompt(two_rows);

		prompt.Place(second_row + column);
		prompt.Press(ImGuiKey_UpArrow);

		CHECK(prompt.Caret() == column);
	};

	TEST_CASE("up onto a shorter row lands at its end, and the next up comes back to the column")
	{
		constexpr int short_row = 7;
		constexpr int short_letters = 2;
		constexpr int last_row = 10;
		constexpr int far_column = 5;

		Box prompt("abcdef\nab\nabcdef");

		prompt.Place(last_row + far_column);
		prompt.Press(ImGuiKey_UpArrow);

		CHECK(prompt.Caret() == short_row + short_letters);

		prompt.Press(ImGuiKey_UpArrow);

		CHECK(prompt.Caret() == far_column);
	};

	TEST_CASE("a wrapped row counts as a row")
	{
		Box prompt("");
		const int letters = prompt.RowLetters();

		REQUIRE(letters > column);

		for (int typed = 0; typed < letters * 2; ++typed)
			prompt.Type('a');

		prompt.Place(letters + column);
		prompt.Press(ImGuiKey_UpArrow);

		CHECK(prompt.Caret() == column);

		prompt.Press(ImGuiKey_UpArrow);

		CHECK(prompt.Caret() == 0);
	};

	TEST_CASE("shift and up take the selection along")
	{
		Box prompt(two_rows);

		prompt.Place(second_row + column);
		prompt.Press(ImGuiKey_UpArrow, ImGuiMod_Shift);

		CHECK(prompt.Anchor() == second_row + column);
		CHECK(prompt.Caret() == column);

		prompt.Press(ImGuiKey_UpArrow, ImGuiMod_Shift);

		CHECK(prompt.Anchor() == second_row + column);
		CHECK(prompt.Caret() == 0);
	};

	TEST_CASE("up with a selection drops it and moves from its start")
	{
		constexpr int from = 1;

		Box prompt(two_rows);

		prompt.Place(second_row + column, second_row + from);
		prompt.Press(ImGuiKey_UpArrow);

		CHECK(prompt.Caret() == from);
		CHECK(prompt.Selected() == false);
	};

	TEST_CASE("control and up leaves the caret alone")
	{
		Box prompt(two_rows);

		prompt.Place(second_row + column);
		prompt.Press(ImGuiKey_UpArrow, ImGuiMod_Ctrl);

		CHECK(prompt.Caret() == second_row + column);
	};

	TEST_CASE("down from an upper row keeps the column, and from the bottom row goes to the end")
	{
		Box prompt(two_rows);

		prompt.Place(column);
		prompt.Press(ImGuiKey_DownArrow);

		CHECK(prompt.Caret() == second_row + column);

		prompt.Press(ImGuiKey_DownArrow);

		CHECK(prompt.Caret() == (int)two_rows.size());
	};

	TEST_CASE("shift and down take the selection along")
	{
		Box prompt(two_rows);

		prompt.Place(column);
		prompt.Press(ImGuiKey_DownArrow, ImGuiMod_Shift);

		CHECK(prompt.Anchor() == column);
		CHECK(prompt.Caret() == second_row + column);
	};

	TEST_CASE("the other keys still work")
	{
		Box prompt(two_rows);

		prompt.Place(column);
		prompt.Press(ImGuiKey_LeftArrow);

		CHECK(prompt.Caret() == column - 1);

		prompt.Type('x');

		CHECK(prompt.text == "abxcdef\nghijkl");
		CHECK(prompt.entered == false);

		prompt.Press(ImGuiKey_Enter);

		CHECK(prompt.entered);
	};
};
