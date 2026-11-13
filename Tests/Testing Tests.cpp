#include "doctest.h"

#include "Descendants.h"
#include "Headless.h"
#include "RunTemp.h"
#include "TempFolder.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

using namespace Loom::Testing;

#pragma region Helpers

namespace
{
	constexpr const char* window_name = "Testing";
	constexpr const char* first_label = "First";
	constexpr const char* second_label = "Second";
	constexpr const char* missing_label = "Missing";
	constexpr const char* input_label = "##input";
	constexpr const char* typed = "hello";
	constexpr float sweep_step = 4;
	constexpr ImRect button_area = { 0, 0, 200, 120 };
	constexpr ImRect corner_area = { 0, 0, 40, 40 };
	constexpr ImVec2 small_display = { 320, 240 };
	constexpr float slow_frame = 1.0f / 30;
	constexpr int settle_frames = 3;
	constexpr size_t input_capacity = 64;

	// Two buttons in a full-display window, counting their presses.
	struct Buttons final
	{
		explicit Buttons(const Headless& headless) :
			m_headless(headless)
		{
			for (int frame = 0; frame < settle_frames; ++frame)
				Frame();
		};

		void Frame()
		{
			m_headless.Window(window_name, [this]()
			{
				if (ImGui::Button(first_label))
					first++;

				if (ImGui::Button(second_label))
					second++;

				if (ImGui::IsItemHovered())
					secondRect = { ImGui::GetItemRectMin(), ImGui::GetItemRectMax() };
			});
		};

		Headless::Draw Drawer()
		{
			return [this]() { Frame(); };
		};

		int first = 0;
		int second = 0;
		ImRect secondRect{ };

	private:
		const Headless& m_headless;
	};

	// A text input in a full-display window that takes the keyboard on its first frame.
	struct Input final
	{
		explicit Input(const Headless& headless) :
			m_headless(headless)
		{
			for (int frame = 0; frame < settle_frames; ++frame)
				Frame();
		};

		void Frame()
		{
			m_headless.Window(window_name, [this]()
			{
				if (m_focus)
					ImGui::SetKeyboardFocusHere();

				m_focus = false;

				if (ImGui::InputText(input_label, buffer, sizeof(buffer), ImGuiInputTextFlags_EnterReturnsTrue))
					entered++;
			});
		};

		Headless::Draw Drawer()
		{
			return [this]() { Frame(); };
		};

		ImGuiID Id() const
		{
			return m_headless.Id(window_name, input_label);
		};

		char buffer[input_capacity]{ };
		int entered = 0;

	private:
		const Headless& m_headless;
		bool m_focus = true;
	};

	std::string Variable(const char* name)
	{
		const char* const value = std::getenv(name);

		return value == nullptr ? std::string() : value;
	};

	std::vector<DWORD> Ids(const std::vector<ProcessEntry>& found)
	{
		std::vector<DWORD> ids;

		for (const ProcessEntry& entry : found)
			ids.push_back(entry.id);

		return ids;
	};
};

#pragma endregion

TEST_SUITE("Loom Testing")
{
	#pragma region Headless

	TEST_CASE("a headless context is current, sized, has no ini file and a built font atlas")
	{
		Headless::Options options;
		options.display = small_display;
		options.frameSeconds = slow_frame;
		options.backend = ImGuiBackendFlags_HasMouseCursors;

		const Headless headless(options);
		const ImGuiIO& io = ImGui::GetIO();

		CHECK(ImGui::GetCurrentContext() == headless.Context());
		CHECK(io.DisplaySize.x == small_display.x);
		CHECK(io.DisplaySize.y == small_display.y);
		CHECK(io.DeltaTime == slow_frame);
		CHECK(io.IniFilename == nullptr);
		CHECK((io.BackendFlags & ImGuiBackendFlags_HasMouseCursors) != 0);
		CHECK(io.Fonts->IsBuilt());
	};

	TEST_CASE("a headless context without fonts leaves its atlas unbuilt")
	{
		Headless::Options options;
		options.buildFonts = false;

		const Headless headless(options);

		CHECK_FALSE(ImGui::GetIO().Fonts->IsBuilt());
	};

	TEST_CASE("a headless context leaves no context current when it goes")
	{
		{
			const Headless headless;
			Input input(headless);
			headless.Type(typed, input.Drawer());
		};

		CHECK(ImGui::GetCurrentContext() == nullptr);
	};

	TEST_CASE("a new headless context starts clean after one that was typed and clicked in")
	{
		{
			const Headless used;
			Input input(used);
			used.Type(typed, input.Drawer());
			used.Leave(sweep_step, input.Drawer());
		};

		const Headless fresh;
		const ImGuiContext* const context = fresh.Context();

		CHECK(context->ActiveId == 0);
		CHECK(context->HoveredId == 0);
		CHECK(context->InputEventsQueue.empty());
		CHECK(context->Windows.empty());
		CHECK(context->FrameCount == 0);
	};

	TEST_CASE("each frame draws in its own context while another is current")
	{
		const Headless first;
		const Headless second;
		const ImGuiContext* drawn_in = nullptr;

		first.Frame([&drawn_in]() { drawn_in = ImGui::GetCurrentContext(); });

		CHECK(drawn_in == first.Context());
	};

	TEST_CASE("typing reaches the headless context it is called on while another is current")
	{
		const Headless first;
		Input input(first);
		const Headless second;

		first.Type(typed, input.Drawer());

		CHECK(std::string(input.buffer) == typed);
		CHECK(second.Context()->InputEventsQueue.empty());
	};

	TEST_CASE("find gives a point inside the named item")
	{
		const Headless headless;
		Buttons buttons(headless);

		const ImVec2 found = headless.Find(button_area, headless.Id(window_name, second_label), sweep_step, buttons.Drawer());

		REQUIRE(found.x >= 0);
		CHECK(buttons.secondRect.Contains(found));
	};

	TEST_CASE("find gives a negative point for an item that is not drawn")
	{
		const Headless headless;
		Buttons buttons(headless);

		const ImVec2 found = headless.Find(corner_area, headless.Id(window_name, missing_label), sweep_step, buttons.Drawer());

		CHECK(found.x < 0);
		CHECK(found.y < 0);
	};

	TEST_CASE("click presses the named item once and nothing else")
	{
		const Headless headless;
		Buttons buttons(headless);

		CHECK(headless.Click(button_area, headless.Id(window_name, second_label), sweep_step, buttons.Drawer()));
		CHECK(buttons.second == 1);
		CHECK(buttons.first == 0);

		CHECK(headless.Click(button_area, headless.Id(window_name, first_label), sweep_step, buttons.Drawer()));
		CHECK(buttons.first == 1);
		CHECK(buttons.second == 1);
	};

	TEST_CASE("click on an item that is not drawn fails and presses nothing")
	{
		const Headless headless;
		Buttons buttons(headless);

		CHECK_FALSE(headless.Click(corner_area, headless.Id(window_name, missing_label), sweep_step, buttons.Drawer()));
		CHECK(buttons.first == 0);
		CHECK(buttons.second == 0);
	};

	TEST_CASE("type delivers each letter to the focused input")
	{
		const Headless headless;
		Input input(headless);

		headless.Type(typed, input.Drawer());

		CHECK(std::string(input.buffer) == typed);
		CHECK(ImGui::GetActiveID() == input.Id());
	};

	TEST_CASE("enter submits the focused input once and keeps its text")
	{
		const Headless headless;
		Input input(headless);
		headless.Type(typed, input.Drawer());

		headless.Enter(input.Drawer());

		CHECK(input.entered == 1);
		CHECK(std::string(input.buffer) == typed);
	};

	TEST_CASE("leave takes the keyboard off the focused input")
	{
		const Headless headless;
		Input input(headless);
		headless.Type(typed, input.Drawer());
		REQUIRE(ImGui::GetActiveID() == input.Id());

		headless.Leave(sweep_step, input.Drawer());

		CHECK(ImGui::GetActiveID() == 0);
		CHECK(input.entered == 0);
	};

	#pragma endregion

	#pragma region RunTemp and TempFolder

	TEST_CASE("a run temp removes everything written inside it, nested folders too")
	{
		const TempFolder base;
		std::filesystem::path folder;

		{
			const RunTemp run(base.folder);
			folder = run.folder;
			std::filesystem::create_directories(folder / "deep" / "deeper");
			WriteBytes(folder / "deep" / "deeper" / "left.txt", typed);
			WriteBytes(folder / "top.txt", typed);
		};

		CHECK_FALSE(std::filesystem::exists(folder));
		CHECK(std::filesystem::exists(base.folder));
	};

	TEST_CASE("a run temp puts back TEMP and TMP as they were, even if they differed")
	{
		const TempFolder base;
		const std::string temp = Variable("TEMP");
		const std::string tmp = Variable("TMP");
		const std::string other = (base.folder / "other").string();
		_putenv_s("TMP", other.c_str());

		{
			const RunTemp run(base.folder);

			CHECK(Variable("TEMP") == run.folder.string());
			CHECK(Variable("TMP") == run.folder.string());
		};

		CHECK(Variable("TEMP") == temp);
		CHECK(Variable("TMP") == other);
		_putenv_s("TMP", tmp.c_str());
	};

	TEST_CASE("a temp folder exists while it lives and is removed with its contents")
	{
		std::filesystem::path folder;

		{
			const TempFolder temp;
			folder = temp.folder;
			REQUIRE(std::filesystem::is_directory(folder));
			WriteBytes(folder / "left.txt", typed);
			CHECK(ReadBytes(folder / "left.txt") == typed);
		};

		CHECK_FALSE(std::filesystem::exists(folder));
	};

	#pragma endregion

	#pragma region Descendants

	TEST_CASE("descendants follow a chain several deep, nearest first")
	{
		constexpr DWORD root = 10;
		constexpr DWORD child = 20;
		constexpr DWORD grandchild = 30;
		constexpr DWORD great_grandchild = 40;
		constexpr uint64_t snapshot_taken = 100;

		const std::vector<ProcessEntry> entries = {
			{ great_grandchild, grandchild, 4 },
			{ grandchild, child, 3 },
			{ child, root, 2 },
			{ root, 0, 1 } };

		CHECK(Ids(Descendants(root, entries, snapshot_taken)) == std::vector<DWORD>{ child, grandchild, great_grandchild });
	};

	TEST_CASE("descendants list a child listed twice once")
	{
		constexpr DWORD root = 10;
		constexpr DWORD child = 20;
		constexpr uint64_t snapshot_taken = 100;

		const std::vector<ProcessEntry> entries = {
			{ root, 0, 1 },
			{ child, root, 2 },
			{ child, root, 2 } };

		CHECK(Ids(Descendants(root, entries, snapshot_taken)) == std::vector<DWORD>{ child });
	};

	TEST_CASE("a root with no children has no descendants")
	{
		constexpr DWORD root = 10;
		constexpr DWORD sibling = 20;
		constexpr uint64_t snapshot_taken = 100;

		const std::vector<ProcessEntry> entries = {
			{ root, 0, 1 },
			{ sibling, 0, 2 } };

		CHECK(Descendants(root, entries, snapshot_taken).empty());
	};

	#pragma endregion
};
