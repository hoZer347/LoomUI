#include "doctest.h"

#include "ExtensionRegistry.h"

#include "Testing/Headless.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "ImGuiItemHooks.h"

#include <windows.h>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

using namespace Loom;

#pragma region Helpers

namespace
{
	constexpr float display_side = 512;
	constexpr const char* settings_type = "LoomRegistryTests";
	constexpr const char* window_menu = "Window";
	constexpr int settle_frames = 4;
	constexpr float wide_spacing = 20;
	constexpr ImVec2 alpha_place{ 0, 0 };
	constexpr ImVec2 beta_place{ 300, 300 };
	constexpr ImVec2 panel_size{ 200, 200 };
	constexpr ImVec2 over_alpha{ 100, 100 };
	constexpr ImVec2 over_beta{ 400, 400 };

	struct Context final
	{
		Context(const Context&) = delete;
		Context& operator=(const Context&) = delete;
		Context() = default;

	private:
		struct Cleared final
		{
			Cleared()
			{
				ExtensionRegistry::Panels().clear();
			};

			~Cleared()
			{
				ExtensionRegistry::Panels().clear();
			};
		};

		const Cleared m_cleared;
		const Loom::Testing::Headless m_screen{ Loom::Testing::Headless::Options{ { display_side, display_side }, 1.0f / 60, ImGuiBackendFlags_RendererHasTextures, false } };
	};

	struct Items final : ImGuiItemListener
	{
		void ItemAdded(
			const ImGuiID id,
			const ImRect& rect) override
		{
			rects[id] = rect;
		};

		void ItemLabelled(
			const ImGuiID id,
			const char* const label) override
		{
			if (*label == 0
				|| std::find(ids.begin(), ids.end(), id) != ids.end())
				return;

			ids.push_back(id);
			labels.emplace_back(label);
			tops.push_back(rects[id].Min.y);
		};

		std::map<ImGuiID, ImRect> rects;
		std::vector<ImGuiID> ids;
		std::vector<std::string> labels;
		std::vector<float> tops;
	};

	ExtensionRegistry::Panel& Last()
	{
		return ExtensionRegistry::Panels().back();
	};

	void Frame(
		std::vector<std::string>& dropped,
		const ExtensionRegistry::Pad& pad = { })
	{
		ImGui::NewFrame();
		ExtensionRegistry::DrawAll("project", dropped, pad);
		ImGui::Render();
	};

	void Frame()
	{
		std::vector<std::string> dropped;
		Frame(dropped);
	};

	std::string Joined(const std::vector<std::string>& labels)
	{
		std::string joined;

		for (const std::string& label : labels)
			joined += (joined.empty() ? "" : "|") + label;

		return joined;
	};

	Items WindowItems(const float itemSpacingY)
	{
		Items items;
		ImGuiContext& context = *ImGui::GetCurrentContext();
		context.TestEngine = &items;
		context.TestEngineHookItems = true;

		Items shown;

		for (int frame = 0; frame < settle_frames; ++frame)
		{
			ImGui::NewFrame();

			if (ImGui::BeginMainMenuBar())
			{
				if (!ImGui::IsPopupOpen(window_menu))
					ImGui::OpenPopup(window_menu);

				if (ImGui::BeginMenu(window_menu))
				{
					items = { };
					ExtensionRegistry::DrawWindowItems(itemSpacingY);
					shown = items;
					ImGui::EndMenu();
				};

				ImGui::EndMainMenuBar();
			};

			ImGui::Render();
		};

		context.TestEngine = nullptr;
		context.TestEngineHookItems = false;

		return shown;
	};
}

#pragma endregion

#pragma region Titles

TEST_CASE("a title is the icon, a space, the label, then ### and the id")
{
	CHECK(ExtensionRegistry::Title("I", "Label", "Id") == "I Label###Id");
}

TEST_CASE("a panel's id is what follows the last ###, or the whole title without one")
{
	CHECK(ExtensionRegistry::PanelId("I Label###Id") == "Id");
	CHECK(ExtensionRegistry::PanelId("A###B###C") == "C");
	CHECK(ExtensionRegistry::PanelId("Plain") == "Plain");
	CHECK(ExtensionRegistry::PanelId("Label###") == "");
}

TEST_CASE("a panel is named by its id alone, not by a label or an id it ends in")
{
	const Context context;

	ExtensionRegistry::AddPanel("I", "Alpha", [](const std::string&) { });

	CHECK(ExtensionRegistry::Named(Last(), "Alpha"));
	CHECK_FALSE(ExtensionRegistry::Named(Last(), "I Alpha"));
	CHECK_FALSE(ExtensionRegistry::Named(Last(), "pha"));
	CHECK_FALSE(ExtensionRegistry::Named(Last(), "Beta"));
}

TEST_CASE("an added panel's title is the icon, the name and the name as id, closed, listed and owned by the module that added it")
{
	const Context context;

	ExtensionRegistry::AddPanel("I", "Alpha", [](const std::string&) { });

	CHECK(Last().title == ExtensionRegistry::Title("I", "Alpha", "Alpha"));
	CHECK_FALSE(Last().shown);
	CHECK(ExtensionRegistry::Listed(Last()));
	CHECK(Last().owner == GetModuleHandleW(nullptr));
}

TEST_CASE("a retitled panel keeps its id, and its window keeps one layout entry under that id")
{
	const Context context;

	ExtensionRegistry::AddPanel("I", "Alpha", [](const std::string&) { });
	Last().shown = true;
	Frame();

	ExtensionRegistry::Retitle(Last(), ExtensionRegistry::Title("J", "Renamed", "Alpha"));

	CHECK(Last().title == "J Renamed###Alpha");
	CHECK(ExtensionRegistry::Named(Last(), "Alpha"));

	Frame();

	const std::string layout = ImGui::SaveIniSettingsToMemory();

	CHECK(layout.find("[Window][Alpha]\n") != std::string::npos);
	CHECK(layout.find("Renamed") == std::string::npos);
	CHECK(layout.find("I Alpha") == std::string::npos);
	CHECK(ImGui::FindWindowByName("J Renamed###Alpha") == ImGui::FindWindowByName("I Alpha###Alpha"));
}

TEST_CASE("retitling with the same title leaves the title's storage alone")
{
	const Context context;

	ExtensionRegistry::AddPanel("I", "Alpha", [](const std::string&) { });

	const char* const before = Last().title.c_str();
	ExtensionRegistry::Retitle(Last(), Last().title);

	CHECK(Last().title.c_str() == before);
}

TEST_CASE("focusing a closed panel shows it and makes its window the focused one over a later one")
{
	const Context context;

	ExtensionRegistry::AddPanel("I", "Alpha", [](const std::string&) { });
	ExtensionRegistry::AddPanel("I", "Beta", [](const std::string&) { });

	ExtensionRegistry::Panel& alpha = ExtensionRegistry::Panels().front();
	ExtensionRegistry::Panel& beta = Last();
	alpha.shown = true;
	beta.shown = true;
	Frame();
	Frame();
	alpha.shown = false;
	Frame();

	REQUIRE(ImGui::GetCurrentContext()->NavWindow == ImGui::FindWindowByName(beta.title.c_str()));

	ImGui::NewFrame();
	ExtensionRegistry::Focus(alpha);
	std::vector<std::string> dropped;
	ExtensionRegistry::DrawAll("project", dropped);
	ImGui::Render();
	Frame();

	CHECK(alpha.shown);
	CHECK(ImGui::GetCurrentContext()->NavWindow == ImGui::FindWindowByName(alpha.title.c_str()));
}

#pragma endregion

#pragma region Layout file

TEST_CASE("listed panels write their shown state under the settings type, one id a line, and read it back")
{
	const Context context;

	ExtensionRegistry::AddPanel("I", "Alpha", [](const std::string&) { });
	ExtensionRegistry::AddPanel("I", "Beta", [](const std::string&) { });
	ExtensionRegistry::AddPanel("I", "Hidden", [](const std::string&) { });
	Last().listed = false;
	Last().shown = true;
	ExtensionRegistry::Panels().front().shown = true;
	ExtensionRegistry::AddPanelSettings(settings_type);

	const std::string layout = ImGui::SaveIniSettingsToMemory();

	CHECK(layout.find("[LoomRegistryTests][Panels]\nAlpha=1\nBeta=0\n\n") != std::string::npos);
	CHECK(layout.find("Hidden") == std::string::npos);

	for (ExtensionRegistry::Panel& panel : ExtensionRegistry::Panels())
		panel.shown = false;

	ImGui::LoadIniSettingsFromMemory("[LoomRegistryTests][Panels]\nAlpha=0\nBeta=1\nHidden=1\nUnknown=1\n");

	const auto shown = [](const std::string& id)
	{
		for (const ExtensionRegistry::Panel& panel : ExtensionRegistry::Panels())
			if (ExtensionRegistry::Named(panel, id))
				return panel.shown;

		return false;
	};

	CHECK_FALSE(shown("Alpha"));
	CHECK(shown("Beta"));
	CHECK_FALSE(shown("Hidden"));
}

TEST_CASE("a settings line with no = or under another entry changes nothing")
{
	const Context context;

	ExtensionRegistry::AddPanel("I", "Alpha", [](const std::string&) { });
	ExtensionRegistry::AddPanelSettings(settings_type);

	ImGui::LoadIniSettingsFromMemory("[LoomRegistryTests][Panels]\nAlpha\n[LoomRegistryTests][Other]\nAlpha=1\n");

	CHECK_FALSE(Last().shown);
}

#pragma endregion

#pragma region Window menu

TEST_CASE("the Window menu lists each listed panel by title, in order, spaced as asked")
{
	const Context context;

	ExtensionRegistry::AddPanel("I", "Alpha", [](const std::string&) { });
	ExtensionRegistry::AddPanel("I", "Hidden", [](const std::string&) { });
	Last().listed = false;
	ExtensionRegistry::AddPanel("I", "Beta", [](const std::string&) { });

	const Items normal = WindowItems(0);
	const Items wide = WindowItems(wide_spacing);

	CHECK(Joined(normal.labels) == "I Alpha###Alpha|I Beta###Beta");
	CHECK(Joined(wide.labels) == Joined(normal.labels));

	REQUIRE(normal.tops.size() == 2);
	REQUIRE(wide.tops.size() == 2);

	const float normalStep = normal.tops[1] - normal.tops[0];
	const float wideStep = wide.tops[1] - wide.tops[0];

	CHECK(wideStep - normalStep == doctest::Approx(wide_spacing - ImGui::GetStyle().ItemSpacing.y));
}

#pragma endregion

#pragma region Drawing

TEST_CASE("every panel updates before any draws, and only shown panels draw, each given the project")
{
	const Context context;
	std::vector<std::string> calls;

	ExtensionRegistry::AddPanel(
		"I",
		"Alpha",
		[&calls](const std::string& project) { calls.push_back("draw Alpha " + project); },
		[&calls](const std::string& project) { calls.push_back("update Alpha " + project); });
	ExtensionRegistry::AddPanel(
		"I",
		"Beta",
		[&calls](const std::string& project) { calls.push_back("draw Beta " + project); },
		[&calls](const std::string& project) { calls.push_back("update Beta " + project); });
	ExtensionRegistry::AddPanel(
		"I",
		"Gamma",
		[&calls](const std::string& project) { calls.push_back("draw Gamma " + project); });
	ExtensionRegistry::Panels().front().shown = true;
	Last().shown = true;

	Frame();

	CHECK(calls == std::vector<std::string>{
		"update Alpha project",
		"update Beta project",
		"draw Alpha project",
		"draw Gamma project" });
}

TEST_CASE("closing a panel's window stops its drawing and leaves it closed in the registry")
{
	const Context context;
	int draws = 0;

	ExtensionRegistry::AddPanel("I", "Alpha", [&draws](const std::string&) { ++draws; });
	Last().shown = true;
	Frame();
	Last().shown = false;
	Frame();

	CHECK(draws == 1);
}

TEST_CASE("the pad wraps each shown panel's Begin, tall while its window is not made yet, short once it floats")
{
	const Context context;
	std::vector<bool> talls;
	int draws = 0;

	const ExtensionRegistry::Pad pad = [&talls](const bool tall, const std::function<void()>& submit)
	{
		talls.push_back(tall);
		submit();
	};

	ExtensionRegistry::AddPanel("I", "Alpha", [&draws](const std::string&) { ++draws; });
	ExtensionRegistry::AddPanel("I", "Beta", [](const std::string&) { });
	ExtensionRegistry::Panels().front().shown = true;

	std::vector<std::string> dropped;
	Frame(dropped, pad);
	Frame(dropped, pad);

	CHECK(talls == std::vector<bool>{ true, false });
	CHECK(draws == 2);
}

TEST_CASE("begun hears each shown panel's title once its window begins, before it draws, and drawn runs once after every panel")
{
	const Context context;
	std::vector<std::string> calls;

	ExtensionRegistry::AddPanel("I", "Alpha", [&calls](const std::string&) { calls.push_back("draw Alpha"); });
	ExtensionRegistry::AddPanel("I", "Beta", [&calls](const std::string&) { calls.push_back("draw Beta"); });
	ExtensionRegistry::AddPanel("I", "Gamma", [&calls](const std::string&) { calls.push_back("draw Gamma"); });
	ExtensionRegistry::Panels().front().shown = true;
	Last().shown = true;

	const std::string alpha = ExtensionRegistry::Panels().front().title;
	const std::string gamma = Last().title;
	std::vector<std::string> dropped;

	ImGui::NewFrame();
	ExtensionRegistry::DrawAll(
		"project",
		dropped,
		{ },
		[&calls](const std::string& title) { calls.push_back("begun " + title); },
		[&calls]() { calls.push_back("drawn"); });
	ImGui::Render();

	CHECK(calls == std::vector<std::string>{
		"begun " + alpha,
		"draw Alpha",
		"begun " + gamma,
		"draw Gamma",
		"drawn" });
}

TEST_CASE("drawn runs with no panel shown")
{
	const Context context;
	int drawn = 0;
	std::vector<std::string> dropped;

	ExtensionRegistry::AddPanel("I", "Alpha", [](const std::string&) { });

	ImGui::NewFrame();
	ExtensionRegistry::DrawAll("project", dropped, { }, { }, [&drawn]() { ++drawn; });
	ImGui::Render();

	CHECK(drawn == 1);
}

TEST_CASE("dropped files go to the shown panel under the mouse that takes them, and are gone after either way")
{
	const Context context;
	std::vector<std::vector<std::string>> taken;

	ExtensionRegistry::AddPanel(
		"I",
		"Alpha",
		[](const std::string&) { },
		{ },
		[&taken](const std::vector<std::string>& files) { taken.push_back(files); });
	ExtensionRegistry::AddPanel("I", "Beta", [](const std::string&) { });

	ExtensionRegistry::Panel& alpha = ExtensionRegistry::Panels().front();
	ExtensionRegistry::Panel& beta = Last();
	alpha.shown = true;
	beta.shown = true;

	Frame();

	ImGui::NewFrame();
	ImGui::SetWindowPos(alpha.title.c_str(), alpha_place);
	ImGui::SetWindowSize(alpha.title.c_str(), panel_size);
	ImGui::SetWindowPos(beta.title.c_str(), beta_place);
	ImGui::SetWindowSize(beta.title.c_str(), panel_size);
	std::vector<std::string> none;
	ExtensionRegistry::DrawAll("project", none);
	ImGui::Render();

	ImGuiIO& io = ImGui::GetIO();
	std::vector<std::string> dropped{ "one.txt", "two.txt" };

	io.AddMousePosEvent(over_beta.x, over_beta.y);
	Frame(dropped);

	CHECK(taken.empty());
	CHECK(dropped.empty());

	dropped = { "one.txt", "two.txt" };
	io.AddMousePosEvent(over_alpha.x, over_alpha.y);
	Frame(dropped);

	REQUIRE(taken.size() == 1);
	CHECK(taken.front() == std::vector<std::string>{ "one.txt", "two.txt" });
	CHECK(dropped.empty());

	Frame(dropped);

	CHECK(taken.size() == 1);
}

#pragma endregion
