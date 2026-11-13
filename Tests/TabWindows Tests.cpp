#include "doctest.h"

#include "ExtensionRegistry.h"
#include "TabWindows.h"
#include "WindowFrame.h"

#include "Testing/Headless.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

using Loom::TabWindows;

#pragma region Helpers

namespace
{
	constexpr ImVec2 display = { 1600, 900 };
	constexpr ImVec2 outside_position = { 2400, 100 };
	constexpr ImVec2 further_position = { 2400, 500 };
	constexpr ImVec2 outside_size = { 400, 300 };
	constexpr const char* first_tab = "First";
	constexpr const char* second_tab = "Second";
	constexpr const char* loose_window = "Loose";
	constexpr const char* settings_type = "LoomTabWindow";
	constexpr const char* host_prefix = "##LoomTabWindow";
	constexpr float frame_seconds = 1.0f / 60;
	constexpr int frame_limit = 30;
	constexpr ImGuiID sample_tag = 0x1234ABCD;
	constexpr uintptr_t main_handle = 1;
	constexpr uintptr_t wrong_handle = 2;
	constexpr uintptr_t first_window_handle = 100;

	void* current_context = nullptr;
	uintptr_t next_handle = first_window_handle;
	int created = 0;
	int destroyed = 0;
	int restored = 0;
	int padded = 0;
	int rendered = 0;
	int rendered_elsewhere = 0;
	ImGuiID close_viewport = 0;
	bool unframed_without_handle = false;
	std::vector<ImGuiID> created_ids;
	std::vector<ImGuiID> destroyed_ids;
	std::vector<ImGuiID> framed_ids;
	std::vector<ImGuiID> unframed_ids;
	std::vector<ImGuiID> button_ids;
	ImVec2 strip_min{ };
	ImVec2 strip_max{ };

	void* Current()
	{
		return current_context;
	};

	void MakeCurrent(void* const context)
	{
		current_context = context;
		++restored;
	};

	void* MainContext()
	{
		return reinterpret_cast<void*>(main_handle);
	};

	bool Has(
		const std::vector<ImGuiID>& ids,
		const ImGuiID id)
	{
		return std::ranges::find(ids, id) != ids.end();
	};

	void FakeCreateWindow(ImGuiViewport* const viewport)
	{
		viewport->PlatformHandle = reinterpret_cast<void*>(next_handle++);
		current_context = viewport->PlatformHandle;
		created_ids.push_back(viewport->ID);
		++created;
	};

	void FakeDestroyWindow(ImGuiViewport* const viewport)
	{
		if (current_context == viewport->PlatformHandle)
			current_context = nullptr;

		if (viewport->PlatformHandle != nullptr)
			destroyed_ids.push_back(viewport->ID);

		viewport->PlatformHandle = nullptr;
		++destroyed;
	};

	void FakeRenderWindow(
		ImGuiViewport* const viewport,
		void*)
	{
		++rendered;

		if (current_context != viewport->PlatformHandle)
			++rendered_elsewhere;
	};

	void FakeShowWindow(ImGuiViewport*)
	{
	};

	void FakeSetWindowPos(
		ImGuiViewport*,
		ImVec2)
	{
	};

	ImVec2 FakeGetWindowPos(ImGuiViewport* const viewport)
	{
		return viewport->Pos;
	};

	void FakeSetWindowSize(
		ImGuiViewport*,
		ImVec2)
	{
	};

	ImVec2 FakeGetWindowSize(ImGuiViewport* const viewport)
	{
		return viewport->Size;
	};

	void FakeSetWindowFocus(ImGuiViewport*)
	{
	};

	bool FakeGetWindowFocus(ImGuiViewport*)
	{
		return false;
	};

	bool FakeGetWindowMinimized(ImGuiViewport*)
	{
		return false;
	};

	void FakeSetWindowTitle(
		ImGuiViewport*,
		const char*)
	{
	};

	TabWindows::Hooks FakeHooks()
	{
		TabWindows::Hooks hooks;

		hooks.current = Current;
		hooks.makeCurrent = MakeCurrent;

		hooks.frame = [](ImGuiViewport& viewport)
		{
			framed_ids.push_back(viewport.ID);
		};

		hooks.unframe = [](ImGuiViewport& viewport)
		{
			if (viewport.PlatformHandle == nullptr)
				unframed_without_handle = true;

			unframed_ids.push_back(viewport.ID);
		};

		hooks.buttons = [](
			ImGuiViewport& viewport,
			const ImVec2 barMin,
			const ImVec2 barMax)
		{
			button_ids.push_back(viewport.ID);
			strip_min = barMin;
			strip_max = barMax;

			return viewport.ID == close_viewport;
		};

		hooks.padded = [](const std::function<void()>& submit)
		{
			++padded;
			submit();
		};

		return hooks;
	};

	struct Options final
	{
		bool viewports = true;
		bool install = true;
		bool noAutoMerge = true;
		bool noTaskBarIcon = false;
		bool noDefaultParent = true;
		const char* ini = nullptr;
		const char* iniFile = nullptr;
	};

	struct Tabs final
	{
		explicit Tabs(const Options& options = { })
		{
			current_context = MainContext();
			next_handle = first_window_handle;
			created = 0;
			destroyed = 0;
			restored = 0;
			padded = 0;
			rendered = 0;
			rendered_elsewhere = 0;
			close_viewport = 0;
			unframed_without_handle = false;
			created_ids.clear();
			destroyed_ids.clear();
			framed_ids.clear();
			unframed_ids.clear();
			button_ids.clear();

			ImGuiIO& io = ImGui::GetIO();
			io.IniFilename = options.iniFile;
			io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
			io.ConfigViewportsNoAutoMerge = options.noAutoMerge;
			io.ConfigViewportsNoTaskBarIcon = options.noTaskBarIcon;
			io.ConfigViewportsNoDefaultParent = options.noDefaultParent;

			if (options.viewports)
				io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

			ImGuiPlatformIO& platform = ImGui::GetPlatformIO();
			platform.Platform_CreateWindow = FakeCreateWindow;
			platform.Platform_DestroyWindow = FakeDestroyWindow;
			platform.Platform_ShowWindow = FakeShowWindow;
			platform.Platform_SetWindowPos = FakeSetWindowPos;
			platform.Platform_GetWindowPos = FakeGetWindowPos;
			platform.Platform_SetWindowSize = FakeSetWindowSize;
			platform.Platform_GetWindowSize = FakeGetWindowSize;
			platform.Platform_SetWindowFocus = FakeSetWindowFocus;
			platform.Platform_GetWindowFocus = FakeGetWindowFocus;
			platform.Platform_GetWindowMinimized = FakeGetWindowMinimized;
			platform.Platform_SetWindowTitle = FakeSetWindowTitle;
			platform.Renderer_RenderWindow = FakeRenderWindow;

			ImGuiPlatformMonitor monitor;
			monitor.MainPos = { 0, 0 };
			monitor.MainSize = display;
			monitor.WorkPos = { 0, 0 };
			monitor.WorkSize = display;
			platform.Monitors.push_back(monitor);

			ImGui::GetMainViewport()->PlatformHandle = MainContext();

			Loom::ExtensionRegistry::Panels().clear();

			for (const char* const title : { first_tab, second_tab })
			{
				Loom::ExtensionRegistry::Panel panel;
				panel.title = title;
				panel.shown = true;
				panel.draw = [](const std::string&)
				{
				};

				Loom::ExtensionRegistry::Panels().push_back(std::move(panel));
			};

			m_tabs.emplace(FakeHooks());

			if (options.install)
				m_installed = m_tabs->Install();

			if (options.ini != nullptr)
				ImGui::LoadIniSettingsFromMemory(options.ini);
		};

		~Tabs()
		{
			ImGui::DestroyPlatformWindows();
			m_tabs.reset();
			Loom::ExtensionRegistry::Panels().clear();
		};

		Tabs(const Tabs&) = delete;
		Tabs& operator=(const Tabs&) = delete;

		TabWindows& Windows() { return *m_tabs; };
		bool Installed() const { return m_installed; };
		ImGuiID Main() const { return m_main; };

		// A frame as the app runs one: the main dock space, the tab windows, the panels, then the platform windows.
		void Frame()
		{
			ImGui::NewFrame();

			m_main = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

			if (m_tabs->IsInstalled())
				m_tabs->Update(m_main);

			for (Loom::ExtensionRegistry::Panel& panel : Loom::ExtensionRegistry::Panels())
			{
				if (!panel.shown)
					continue;

				ImGui::SetNextWindowDockID(m_main, ImGuiCond_FirstUseEver);

				if (m_placing == panel.title)
				{
					ImGui::SetNextWindowPos(m_place, ImGuiCond_Always);
					ImGui::SetNextWindowSize(outside_size, ImGuiCond_Always);
					m_placing.clear();
				};

				ImGui::Begin(panel.title.c_str(), &panel.shown);
				ImGui::End();
			};

			if (m_loose)
			{
				ImGui::SetNextWindowPos(outside_position, ImGuiCond_Always);
				ImGui::SetNextWindowSize(outside_size, ImGuiCond_Always);
				ImGui::Begin(loose_window);
				ImGui::End();
			};

			if (m_during)
				m_during();

			ImGui::Render();

			if (m_tabs->IsInstalled())
				m_tabs->UpdatePlatformWindows();
			else if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
				Loom::UpdateViewportWindows(Current, MakeCurrent);

			if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
			{
				current_context = MainContext();
				ImGui::RenderPlatformWindowsDefault();
				current_context = MainContext();
			};
		};

		void Frames(const int count)
		{
			for (int frame = 0; frame < count; ++frame)
				Frame();
		};

		// Takes a docked tab out of its node and drops it at position, as a drag that lets go there does.
		void Detach(
			const char* const title,
			const ImVec2 position)
		{
			ImGuiWindow* const window = ImGui::FindWindowByName(title);

			REQUIRE(window != nullptr);
			REQUIRE(window->DockId != 0);

			ImGui::DockContextQueueUndockWindow(ImGui::GetCurrentContext(), window);
			m_placing = title;
			m_place = position;
		};

		// Frames until there are count hosts, or the frame limit.
		bool AwaitHosts(const size_t count)
		{
			for (int frame = 0; frame < frame_limit; ++frame)
			{
				Frame();

				if (m_tabs->Hosts().size() == count)
					return true;
			};

			return false;
		};

		void Loose(const bool loose) { m_loose = loose; };
		void During(std::function<void()> during) { m_during = std::move(during); };

	private:
		const Loom::Testing::Headless m_screen{ Loom::Testing::Headless::Options{ display, frame_seconds, ImGuiBackendFlags_PlatformHasViewports | ImGuiBackendFlags_RendererHasViewports } };
		std::optional<TabWindows> m_tabs;
		bool m_installed = false;
		ImGuiID m_main = 0;
		std::string m_placing;
		ImVec2 m_place{ };
		bool m_loose = false;
		std::function<void()> m_during;
	};

	Loom::ExtensionRegistry::Panel& PanelNamed(const char* const title)
	{
		for (Loom::ExtensionRegistry::Panel& panel : Loom::ExtensionRegistry::Panels())
			if (panel.title == title)
				return panel;

		FAIL("no panel named " << title);
		return Loom::ExtensionRegistry::Panels().front();
	};

	ImGuiID RootNode(const char* const title)
	{
		const ImGuiWindow* const window = ImGui::FindWindowByName(title);

		if (window == nullptr || window->DockNode == nullptr)
			return 0;

		return ImGui::DockNodeGetRootNode(window->DockNode)->ID;
	};

	ImGuiViewportP* HostViewport(const TabWindows::Host& host)
	{
		const ImGuiWindow* const window = ImGui::FindWindowByName(host.name.c_str());

		return window != nullptr ? window->Viewport : nullptr;
	};

	// Docks both tabs in the main dock space, then lets the first go outside the main window.
	TabWindows::Host Adopted(Tabs& tabs)
	{
		tabs.Frames(2);

		REQUIRE(RootNode(first_tab) == tabs.Main());

		tabs.Detach(first_tab, outside_position);

		REQUIRE(tabs.AwaitHosts(1));

		return tabs.Windows().Hosts().front();
	};
};

#pragma endregion

#pragma region Install

TEST_CASE("installing wraps the platform and renderer callbacks and adds the tab window settings handler, and its end puts them back")
{
	Tabs tabs;

	REQUIRE(tabs.Installed());
	CHECK(tabs.Windows().IsInstalled());
	CHECK(ImGui::FindSettingsHandler(settings_type) != nullptr);

	const ImGuiPlatformIO& platform = ImGui::GetPlatformIO();

	CHECK(platform.Platform_CreateWindow != FakeCreateWindow);
	CHECK(platform.Platform_DestroyWindow != FakeDestroyWindow);
	CHECK(platform.Renderer_RenderWindow != FakeRenderWindow);

	tabs.Frames(2);
	CHECK(tabs.Windows().Hosts().empty());
}

TEST_CASE("with viewports off nothing is installed and a tab let go outside stays a plain floating window")
{
	Options options;
	options.viewports = false;

	Tabs tabs(options);

	CHECK_FALSE(tabs.Installed());
	CHECK_FALSE(tabs.Windows().IsInstalled());
	CHECK(ImGui::FindSettingsHandler(settings_type) == nullptr);

	const ImGuiPlatformIO& platform = ImGui::GetPlatformIO();

	CHECK(platform.Platform_CreateWindow == FakeCreateWindow);
	CHECK(platform.Platform_DestroyWindow == FakeDestroyWindow);
	CHECK(platform.Renderer_RenderWindow == FakeRenderWindow);

	tabs.Frames(2);
	tabs.Detach(first_tab, outside_position);
	tabs.Frames(frame_limit);

	CHECK(tabs.Windows().Hosts().empty());
	CHECK(RootNode(first_tab) == 0);
	CHECK(created == 0);

	for (const ImGuiWindow* const window : ImGui::GetCurrentContext()->Windows)
		CHECK(std::string(window->Name).find(host_prefix) == std::string::npos);
}

TEST_CASE("an update with viewports off, and so never installed, adopts nothing")
{
	Options options;
	options.viewports = false;

	Tabs tabs(options);

	tabs.Frames(2);
	tabs.Detach(first_tab, outside_position);

	for (int frame = 0; frame < frame_limit; ++frame)
	{
		ImGui::NewFrame();
		const ImGuiID main = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
		tabs.Windows().Update(main);
		ImGui::Begin(first_tab);
		ImGui::End();
		ImGui::Render();
		tabs.Windows().UpdatePlatformWindows();
	};

	CHECK(tabs.Windows().Hosts().empty());
	CHECK(RootNode(first_tab) == 0);
}

TEST_CASE("a host's name and dock space come from its tag")
{
	CHECK(TabWindows::HostName(sample_tag) == "##LoomTabWindow1234ABCD");
	CHECK(TabWindows::HostDockSpace(sample_tag) != 0);
	CHECK(TabWindows::HostDockSpace(sample_tag) != TabWindows::HostDockSpace(sample_tag + 1));
}

TEST_CASE("the GLFW hooks fill in every hook but the padding, which is the app's")
{
	const TabWindows::Hooks hooks = TabWindows::Glfw(nullptr);

	CHECK(static_cast<bool>(hooks.current));
	CHECK(static_cast<bool>(hooks.makeCurrent));
	CHECK(static_cast<bool>(hooks.frame));
	CHECK(static_cast<bool>(hooks.unframe));
	CHECK(static_cast<bool>(hooks.buttons));
	CHECK_FALSE(static_cast<bool>(hooks.padded));
}

#pragma endregion

#pragma region Adoption

TEST_CASE("a docked tab let go outside the main window is adopted into a host holding its own dock space")
{
	Tabs tabs;

	const TabWindows::Host host = Adopted(tabs);
	const ImGuiWindow* const window = ImGui::FindWindowByName(host.name.c_str());

	REQUIRE(window != nullptr);
	CHECK(host.name == TabWindows::HostName(host.tag));
	CHECK(host.windowId == window->ID);
	CHECK(host.dockSpace == TabWindows::HostDockSpace(host.tag));

	const ImGuiDockNode* const space = ImGui::DockBuilderGetNode(host.dockSpace);

	REQUIRE(space != nullptr);
	CHECK(space->IsDockSpace());
	REQUIRE(space->HostWindow != nullptr);
	CHECK(space->HostWindow->ParentWindow == window);

	CHECK(RootNode(first_tab) == host.dockSpace);
	CHECK(ImGui::FindWindowByName(first_tab)->Viewport == window->Viewport);
	CHECK(RootNode(second_tab) == tabs.Main());
}

TEST_CASE("a host's viewport is a top-level window with a taskbar button that never merges into the main one")
{
	Options options;
	options.noAutoMerge = false;
	options.noTaskBarIcon = true;
	options.noDefaultParent = false;

	Tabs tabs(options);

	const TabWindows::Host host = Adopted(tabs);

	tabs.Frames(2);

	const ImGuiViewportP* const viewport = HostViewport(host);

	REQUIRE(viewport != nullptr);
	CHECK(viewport != ImGui::GetMainViewport());
	CHECK(viewport->ParentViewportId == 0);
	CHECK((viewport->Flags & ImGuiViewportFlags_NoTaskBarIcon) == 0);
	CHECK((viewport->Flags & ImGuiViewportFlags_NoAutoMerge) != 0);
}

TEST_CASE("a host's platform window is framed once made, and the main context stays current")
{
	Tabs tabs;

	const TabWindows::Host host = Adopted(tabs);

	tabs.Frames(2);

	const ImGuiViewportP* const viewport = HostViewport(host);

	REQUIRE(viewport != nullptr);
	CHECK(viewport->PlatformHandle != nullptr);
	CHECK(Has(created_ids, viewport->ID));
	CHECK(Has(framed_ids, viewport->ID));
	CHECK(Current() == MainContext());

	CHECK(std::ranges::count(framed_ids, viewport->ID) == 1);
}

TEST_CASE("a host with its tab shown stays, and its platform window is made once")
{
	Tabs tabs;

	const TabWindows::Host host = Adopted(tabs);

	tabs.Frames(2);

	const int made = created;

	tabs.Frames(frame_limit);

	REQUIRE(tabs.Windows().Hosts().size() == 1);
	CHECK(tabs.Windows().Hosts().front().tag == host.tag);
	CHECK(created == made);
	CHECK_FALSE(Has(destroyed_ids, host.windowId));
	CHECK(RootNode(first_tab) == host.dockSpace);
}

TEST_CASE("a tab still held by the mouse is not adopted until it is let go")
{
	Tabs tabs;

	tabs.Frames(2);
	tabs.Detach(first_tab, outside_position);
	tabs.Frame();

	ImGuiIO& io = ImGui::GetIO();
	ImGuiWindow* const window = ImGui::FindWindowByName(first_tab);

	REQUIRE(window != nullptr);

	const ImVec2 grip = { window->Pos.x + outside_size.x / 2, window->Pos.y + window->TitleBarHeight / 2 };

	io.AddMousePosEvent(grip.x, grip.y);
	io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);

	tabs.During([window]
		{
			if (ImGui::GetCurrentContext()->MovingWindow == nullptr)
				ImGui::StartMouseMovingWindow(window);
		});
	tabs.Frame();
	tabs.During({ });

	REQUIRE(ImGui::GetCurrentContext()->MovingWindow == window);

	tabs.Frames(frame_limit);

	CHECK(tabs.Windows().Hosts().empty());

	io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);

	REQUIRE(tabs.AwaitHosts(1));
	CHECK(RootNode(first_tab) == tabs.Windows().Hosts().front().dockSpace);
}

TEST_CASE("a window never docked and floating outside is not adopted")
{
	Tabs tabs;

	tabs.Loose(true);
	tabs.Frames(frame_limit);

	CHECK(ImGui::FindWindowByName(loose_window)->Viewport != ImGui::GetMainViewport());
	CHECK(tabs.Windows().Hosts().empty());
}

TEST_CASE("a tooltip or popup outside the main window is not adopted")
{
	Tabs tabs;

	tabs.During([]
		{
			ImGui::SetNextWindowPos(outside_position);
			ImGui::SetTooltip("tip");

			ImGui::OpenPopup("Menu");
			ImGui::SetNextWindowPos(further_position);

			if (ImGui::BeginPopup("Menu"))
				ImGui::EndPopup();
		});
	tabs.Frames(frame_limit);

	CHECK(tabs.Windows().Hosts().empty());
}

TEST_CASE("a tab dragged out of a host into empty space makes a second host")
{
	Tabs tabs;

	const TabWindows::Host first = Adopted(tabs);

	tabs.Frames(2);
	ImGui::DockBuilderDockWindow(second_tab, first.dockSpace);
	tabs.Frames(2);

	REQUIRE(RootNode(second_tab) == first.dockSpace);

	tabs.Detach(second_tab, further_position);

	REQUIRE(tabs.AwaitHosts(2));

	const std::vector<TabWindows::Host> hosts = tabs.Windows().Hosts();

	CHECK(hosts[0].tag != hosts[1].tag);
	CHECK(hosts[0].name != hosts[1].name);
	CHECK(hosts[0].dockSpace != hosts[1].dockSpace);
	CHECK(RootNode(first_tab) != RootNode(second_tab));
	CHECK(HostViewport(hosts[0]) != HostViewport(hosts[1]));
}

#pragma endregion

#pragma region Strip

TEST_CASE("a host draws its window buttons in a strip above its dock space, which it submits through the padding hook")
{
	Tabs tabs;

	const TabWindows::Host host = Adopted(tabs);

	button_ids.clear();
	padded = 0;
	tabs.Frames(2);

	const ImGuiViewportP* const viewport = HostViewport(host);
	const ImGuiDockNode* const space = ImGui::DockBuilderGetNode(host.dockSpace);

	REQUIRE(viewport != nullptr);
	REQUIRE(space != nullptr);
	CHECK(padded >= 2);
	CHECK(Has(button_ids, viewport->ID));
	CHECK_FALSE(Has(button_ids, ImGui::GetMainViewport()->ID));
	CHECK(strip_max.y - strip_min.y == doctest::Approx(TabWindows::StripHeight()));
	CHECK(strip_min.y == doctest::Approx(viewport->Pos.y));
	CHECK(strip_min.x >= viewport->Pos.x);
	CHECK(strip_max.x <= viewport->Pos.x + viewport->Size.x);
	CHECK(space->Pos.y >= strip_max.y);
}

#pragma endregion

#pragma region Re-dock

TEST_CASE("a tab docked back into the main dock space leaves its host, which goes, unframed before its platform window is destroyed, with the main context kept")
{
	Tabs tabs;

	const TabWindows::Host host = Adopted(tabs);

	tabs.Frames(2);

	const ImGuiID viewport = host.windowId;

	REQUIRE(HostViewport(host) != nullptr);
	REQUIRE(HostViewport(host)->PlatformHandle != nullptr);

	ImGui::DockBuilderDockWindow(first_tab, tabs.Main());

	REQUIRE(tabs.AwaitHosts(0));

	for (int frame = 0; frame < frame_limit && !Has(destroyed_ids, viewport); ++frame)
		tabs.Frame();

	CHECK(RootNode(first_tab) == tabs.Main());
	CHECK(ImGui::FindWindowByName(first_tab)->Viewport == ImGui::GetMainViewport());
	CHECK(Has(destroyed_ids, viewport));
	CHECK(Has(unframed_ids, viewport));
	CHECK_FALSE(unframed_without_handle);
	CHECK(Current() == MainContext());
}

#pragma endregion

#pragma region Close

TEST_CASE("an OS close request hides the host's panels and docks them into the main dock space, where they reopen")
{
	Tabs tabs;

	const TabWindows::Host host = Adopted(tabs);

	tabs.Frames(2);

	ImGuiViewportP* const viewport = HostViewport(host);

	REQUIRE(viewport != nullptr);

	viewport->PlatformRequestClose = true;

	REQUIRE(tabs.AwaitHosts(0));
	tabs.Frames(2);

	CHECK_FALSE(PanelNamed(first_tab).shown);
	CHECK(PanelNamed(second_tab).shown);
	CHECK(Has(destroyed_ids, host.windowId));
	CHECK(Current() == MainContext());

	PanelNamed(first_tab).shown = true;
	tabs.Frames(2);

	CHECK(RootNode(first_tab) == tabs.Main());
	CHECK(ImGui::FindWindowByName(first_tab)->Viewport == ImGui::GetMainViewport());
	CHECK(tabs.Windows().Hosts().empty());
}

TEST_CASE("the host's close button hides its panels and docks them into the main dock space, where they reopen")
{
	Tabs tabs;

	const TabWindows::Host host = Adopted(tabs);

	tabs.Frames(2);
	ImGui::DockBuilderDockWindow(second_tab, host.dockSpace);
	tabs.Frames(2);

	REQUIRE(RootNode(second_tab) == host.dockSpace);

	close_viewport = host.windowId;

	REQUIRE(tabs.AwaitHosts(0));

	close_viewport = 0;
	tabs.Frames(2);

	CHECK_FALSE(PanelNamed(first_tab).shown);
	CHECK_FALSE(PanelNamed(second_tab).shown);
	CHECK(Has(destroyed_ids, host.windowId));

	PanelNamed(first_tab).shown = true;
	PanelNamed(second_tab).shown = true;
	tabs.Frames(2);

	CHECK(RootNode(first_tab) == tabs.Main());
	CHECK(RootNode(second_tab) == tabs.Main());
	CHECK(tabs.Windows().Hosts().empty());
}

#pragma endregion

#pragma region Settings

TEST_CASE("saved settings recreate a host on the next start with its tab docked in it")
{
	std::string ini;
	TabWindows::Host saved;

	{
		Tabs tabs;

		saved = Adopted(tabs);
		tabs.Frames(2);

		ini = ImGui::SaveIniSettingsToMemory();
	};

	REQUIRE(ini.find(std::string("[") + settings_type + "]") != std::string::npos);

	Options options;
	options.ini = ini.c_str();

	Tabs tabs(options);

	tabs.Frames(4);

	const std::vector<TabWindows::Host> hosts = tabs.Windows().Hosts();

	REQUIRE(hosts.size() == 1);
	CHECK(hosts.front().tag == saved.tag);
	CHECK(hosts.front().name == saved.name);
	CHECK(RootNode(first_tab) == hosts.front().dockSpace);
	CHECK(RootNode(second_tab) == tabs.Main());

	const ImGuiViewportP* const viewport = HostViewport(hosts.front());

	REQUIRE(viewport != nullptr);
	CHECK(viewport != ImGui::GetMainViewport());
	CHECK(viewport->PlatformHandle != nullptr);
	CHECK(Has(framed_ids, viewport->ID));
	CHECK(Current() == MainContext());
}

TEST_CASE("the settings file written at shutdown keeps the host even when the tab windows are destroyed before the context")
{
	const std::string file = (std::filesystem::temp_directory_path() / "tab windows shutdown.ini").string();

	std::filesystem::remove(file);

	TabWindows::Host saved;

	{
		Options options;
		options.iniFile = file.c_str();

		Tabs tabs(options);

		saved = Adopted(tabs);
		tabs.Frames(2);
	};

	std::ifstream stream(file);
	const std::string ini((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());

	stream.close();
	std::filesystem::remove(file);

	REQUIRE(ini.find(std::string("[") + settings_type + "]") != std::string::npos);

	Options options;
	options.ini = ini.c_str();

	Tabs tabs(options);

	tabs.Frames(4);

	const std::vector<TabWindows::Host> hosts = tabs.Windows().Hosts();

	REQUIRE(hosts.size() == 1);
	CHECK(hosts.front().tag == saved.tag);
	CHECK(RootNode(first_tab) == hosts.front().dockSpace);
}

TEST_CASE("settings saved with no host recreate none")
{
	std::string ini;

	{
		Tabs tabs;

		tabs.Frames(2);
		ini = ImGui::SaveIniSettingsToMemory();
	};

	Options options;
	options.ini = ini.c_str();

	Tabs tabs(options);

	tabs.Frames(4);

	CHECK(tabs.Windows().Hosts().empty());
	CHECK(RootNode(first_tab) == tabs.Main());
}

#pragma endregion

#pragma region Context

TEST_CASE("each viewport is drawn with its own context current, even when another is current at render time")
{
	Tabs tabs;

	Adopted(tabs);
	tabs.Frames(2);

	rendered = 0;
	rendered_elsewhere = 0;
	current_context = reinterpret_cast<void*>(wrong_handle);

	ImGui::RenderPlatformWindowsDefault();

	CHECK(rendered > 0);
	CHECK(rendered_elsewhere == 0);
}

TEST_CASE("the main context stays current on every frame of a tab becoming a host")
{
	Tabs tabs;

	tabs.Frames(2);
	tabs.Detach(first_tab, outside_position);

	for (int frame = 0; frame < frame_limit; ++frame)
	{
		tabs.Frame();
		CHECK(Current() == MainContext());
	};

	CHECK(created > 0);
}

#pragma endregion

#pragma region Window buttons

TEST_CASE("the window buttons take three widths of 1.6 bar heights at the bar's right end")
{
	constexpr float bar_height = 20;
	constexpr float bar_right = 1000;

	CHECK(Loom::WindowFrame::ButtonWidth(bar_height) == doctest::Approx(32));
	CHECK(Loom::WindowFrame::ButtonsLeft(bar_right, bar_height) == doctest::Approx(904));
}

TEST_CASE("the drag strip runs from the free left edge to the buttons, in the viewport's client coordinates")
{
	constexpr ImVec2 bar_min = { 100, 10 };
	constexpr ImVec2 bar_max = { 1000, 30 };
	constexpr float strip_left = 150;
	constexpr ImVec2 origin = { 50, 5 };

	const Loom::WindowFrame::Rect strip = Loom::WindowFrame::StripFor(bar_min, bar_max, strip_left, origin);

	CHECK(strip.left == doctest::Approx(100));
	CHECK(strip.top == doctest::Approx(5));
	CHECK(strip.right == doctest::Approx(854));
	CHECK(strip.bottom == doctest::Approx(25));
}

TEST_CASE("a frame not installed on a window draws no buttons, names no drag strip and is never closed, from the menu bar or a strip")
{
	Tabs tabs;
	Loom::WindowFrame frame;
	bool from_menu = true;
	bool from_strip = true;

	tabs.During([&frame, &from_menu, &from_strip]
		{
			if (ImGui::BeginMainMenuBar())
			{
				ImGui::TextUnformatted("File");
				from_menu = frame.DrawButtonRow(nullptr);
				ImGui::EndMainMenuBar();
			};

			ImGui::Begin("Strip");

			const ImVec2 top = ImGui::GetWindowPos();

			from_strip = frame.DrawButtons(
				nullptr,
				top,
				{ top.x + outside_size.x, top.y + TabWindows::StripHeight() },
				top.x);
			ImGui::End();
		});
	tabs.Frame();

	CHECK_FALSE(from_menu);
	CHECK_FALSE(from_strip);
	CHECK(frame.DragStrip().right == 0);
	CHECK(frame.DragStrip().bottom == 0);
}

#pragma endregion
