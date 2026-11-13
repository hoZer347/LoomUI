#include "TabWindows.h"

#include "ExtensionRegistry.h"

#include "imgui_internal.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <utility>


namespace Loom
{
	namespace
	{
		constexpr const char* name_prefix = "##LoomTabWindow";
		constexpr const char* space_name = "LoomTabSpace";
		constexpr const char* settings_type = "LoomTabWindow";
		constexpr const char* settings_section = "Host";
		constexpr const char* settings_key = "Id=";
		constexpr int empty_grace_frames = 3;
		constexpr ImVec2 default_size = { 640.0f, 420.0f };
		constexpr ImVec2 zero = { 0.0f, 0.0f };
		constexpr ImGuiWindowFlags host_flags = ImGuiWindowFlags_NoTitleBar
			| ImGuiWindowFlags_NoCollapse
			| ImGuiWindowFlags_NoDocking
			| ImGuiWindowFlags_NoScrollbar
			| ImGuiWindowFlags_NoScrollWithMouse;
		constexpr ImGuiWindowFlags not_regular = ImGuiWindowFlags_ChildMenu
			| ImGuiWindowFlags_Popup
			| ImGuiWindowFlags_Tooltip
			| ImGuiWindowFlags_Modal
			| ImGuiWindowFlags_DockNodeHost
			| ImGuiWindowFlags_NoDocking;

		// A docked window carries the child flag, a real child has no dock id.
		bool Regular(const ImGuiWindow& window)
		{
			return !(window.Flags & not_regular)
				&& (window.DockId != 0 || !(window.Flags & ImGuiWindowFlags_ChildWindow));
		};

		TabWindows* s_active = nullptr;

		void Tree(
			const ImGuiID id,
			std::vector<ImGuiID>& ids)
		{
			ids.push_back(id);

			if (const ImGuiDockNode* const node = ImGui::DockBuilderGetNode(id))
				for (const ImGuiDockNode* const child : node->ChildNodes)
					if (child != nullptr)
						Tree(child->ID, ids);
		};

		bool Contains(
			const std::vector<ImGuiID>& ids,
			const ImGuiID id)
		{
			return id != 0 && std::ranges::find(ids, id) != ids.end();
		};
	};

	TabWindows::Hooks TabWindows::Glfw(WindowFrame::Redraw redraw)
	{
		using Frames = std::unordered_map<void*, std::unique_ptr<WindowFrame>>;

		const std::shared_ptr<Frames> frames = std::make_shared<Frames>();
		Hooks hooks;

		hooks.current = []() -> void*
		{
			return glfwGetCurrentContext();
		};

		hooks.makeCurrent = [](void* const context)
		{
			glfwMakeContextCurrent(static_cast<GLFWwindow*>(context));
		};

		hooks.frame = [frames, redraw](ImGuiViewport& viewport)
		{
			std::unique_ptr<WindowFrame> frame = std::make_unique<WindowFrame>();

			frame->Install(static_cast<GLFWwindow*>(viewport.PlatformHandle), redraw);
			(*frames)[viewport.PlatformHandle] = std::move(frame);
		};

		hooks.unframe = [frames](ImGuiViewport& viewport)
		{
			frames->erase(viewport.PlatformHandle);
		};

		hooks.buttons = [frames](
			ImGuiViewport& viewport,
			const ImVec2 min,
			const ImVec2 max)
		{
			const Frames::iterator found = frames->find(viewport.PlatformHandle);

			if (found == frames->end() || !found->second->IsInstalled())
				return false;

			return found->second->DrawButtons(static_cast<GLFWwindow*>(viewport.PlatformHandle), min, max, min.x);
		};

		return hooks;
	};

	std::string TabWindows::HostName(const ImGuiID tag)
	{
		char name[32];

		std::snprintf(name, sizeof(name), "%s%08X", name_prefix, tag);

		return name;
	};

	ImGuiID TabWindows::HostDockSpace(const ImGuiID tag)
	{
		return ImHashStr(space_name, 0, ImHashStr(HostName(tag).c_str()));
	};

	float TabWindows::StripHeight()
	{
		const ImGuiStyle& style = ImGui::GetStyle();

		return ImCeil(ImGui::GetFontSize() + style.FramePadding.y * 2.0f * WindowFrame::BarPadding);
	};

	TabWindows::TabWindows(Hooks hooks) :
		m_hooks(std::move(hooks))
	{
	};

	TabWindows::~TabWindows()
	{
		if (!m_installed || s_active != this)
			return;

		s_active = nullptr;

		if (ImGui::GetCurrentContext() != m_context)
			return;

		ImGuiIO& io = ImGui::GetIO();

		if (io.IniFilename != nullptr
			&& m_context->SettingsLoaded)
		{
			ImGui::SaveIniSettingsToDisk(io.IniFilename);
			io.IniFilename = nullptr;
		};

		ImGuiPlatformIO& platform = ImGui::GetPlatformIO();

		if (platform.Platform_CreateWindow == OnCreateWindow)
			platform.Platform_CreateWindow = m_create;

		if (platform.Platform_DestroyWindow == OnDestroyWindow)
			platform.Platform_DestroyWindow = m_destroy;

		if (platform.Renderer_RenderWindow == OnRenderWindow)
			platform.Renderer_RenderWindow = m_render;

		ImVector<ImGuiSettingsHandler>& handlers = m_context->SettingsHandlers;

		for (int index = 0; index < handlers.Size; ++index)
			if (handlers[index].UserData == this)
			{
				handlers.erase(handlers.Data + index);
				break;
			};
	};

	bool TabWindows::Install()
	{
		if (m_installed
			|| ImGui::GetCurrentContext() == nullptr
			|| !(ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable))
			return false;

		m_context = ImGui::GetCurrentContext();
		s_active = this;
		m_installed = true;

		ImGuiPlatformIO& platform = ImGui::GetPlatformIO();

		m_create = platform.Platform_CreateWindow;
		m_destroy = platform.Platform_DestroyWindow;
		m_render = platform.Renderer_RenderWindow;

		if (m_create != nullptr)
			platform.Platform_CreateWindow = OnCreateWindow;

		if (m_destroy != nullptr)
			platform.Platform_DestroyWindow = OnDestroyWindow;

		if (m_render != nullptr)
			platform.Renderer_RenderWindow = OnRenderWindow;

		ImGuiSettingsHandler handler;
		handler.TypeName = settings_type;
		handler.TypeHash = ImHashStr(settings_type);
		handler.ClearAllFn = ClearSettings;
		handler.ReadOpenFn = OpenSettings;
		handler.ReadLineFn = ReadSetting;
		handler.WriteAllFn = WriteSettings;
		handler.UserData = this;
		m_context->SettingsHandlers.push_back(handler);

		return true;
	};

	void TabWindows::OnCreateWindow(ImGuiViewport* const viewport)
	{
		s_active->m_create(viewport);
		s_active->Created(*viewport);
	};

	void TabWindows::OnDestroyWindow(ImGuiViewport* const viewport)
	{
		s_active->Destroying(*viewport);
		s_active->m_destroy(viewport);
	};

	void TabWindows::OnRenderWindow(
		ImGuiViewport* const viewport,
		void* const argument)
	{
		s_active->Rendering(*viewport);
		s_active->m_render(viewport, argument);
	};

	void TabWindows::ClearSettings(
		ImGuiContext*,
		ImGuiSettingsHandler* const handler)
	{
		static_cast<TabWindows*>(handler->UserData)->m_entries.clear();
	};

	void* TabWindows::OpenSettings(
		ImGuiContext*,
		ImGuiSettingsHandler* const handler,
		const char* const name)
	{
		return std::string_view(name) == settings_section ? handler->UserData : nullptr;
	};

	void TabWindows::ReadSetting(
		ImGuiContext*,
		ImGuiSettingsHandler* const handler,
		void*,
		const char* const line)
	{
		constexpr int hex = 16;
		const std::string_view text(line);

		if (!text.starts_with(settings_key))
			return;

		static_cast<TabWindows*>(handler->UserData)->Restore((ImGuiID)std::strtoul(line + std::strlen(settings_key), nullptr, hex));
	};

	void TabWindows::WriteSettings(
		ImGuiContext*,
		ImGuiSettingsHandler* const handler,
		ImGuiTextBuffer* const out)
	{
		for (const Entry& entry : static_cast<const TabWindows*>(handler->UserData)->m_entries)
			out->appendf("[%s][%s]\n%s%08X\n\n", handler->TypeName, settings_section, settings_key, entry.host.tag);
	};

	void TabWindows::Created(ImGuiViewport& viewport)
	{
		const bool host = std::ranges::any_of(
			m_entries,
			[&viewport](const Entry& entry) { return entry.host.windowId == viewport.ID; });

		if (!host)
			return;

		m_framed.insert(viewport.ID);

		if (m_hooks.frame)
			m_hooks.frame(viewport);
	};

	void TabWindows::Destroying(ImGuiViewport& viewport)
	{
		if (m_framed.erase(viewport.ID) != 0 && m_hooks.unframe)
			m_hooks.unframe(viewport);
	};

	void TabWindows::Rendering(ImGuiViewport& viewport)
	{
		if (!m_hooks.current
			|| !m_hooks.makeCurrent
			|| viewport.PlatformHandle == nullptr)
			return;

		if (m_hooks.current() != viewport.PlatformHandle)
			m_hooks.makeCurrent(viewport.PlatformHandle);
	};

	void TabWindows::Restore(const ImGuiID tag)
	{
		if (tag == 0 || Find(tag) != nullptr)
			return;

		Entry entry;
		entry.host.tag = tag;
		entry.host.name = HostName(tag);
		entry.host.windowId = ImHashStr(entry.host.name.c_str());
		entry.host.dockSpace = HostDockSpace(tag);
		m_entries.push_back(std::move(entry));
	};

	TabWindows::Entry* TabWindows::Find(const ImGuiID tag)
	{
		const auto found = std::ranges::find_if(
			m_entries,
			[tag](const Entry& entry) { return entry.host.tag == tag; });

		return found == m_entries.end() ? nullptr : &*found;
	};

	ImGuiID TabWindows::FreeTag(const ImGuiID wanted) const
	{
		ImGuiID tag = wanted;

		while (tag == 0 || std::ranges::any_of(m_entries, [tag](const Entry& entry) { return entry.host.tag == tag; }))
			tag = ImHashData(&tag, sizeof(tag), wanted + 1);

		return tag;
	};

	std::vector<TabWindows::Host> TabWindows::Hosts() const
	{
		std::vector<Host> hosts;

		for (const Entry& entry : m_entries)
			hosts.push_back(entry.host);

		return hosts;
	};

	bool TabWindows::Requested(const Entry& entry) const
	{
		if (entry.closing)
			return true;

		const ImGuiWindow* const window = ImGui::FindWindowByID(entry.host.windowId);

		return window != nullptr && window->Viewport != nullptr && window->Viewport->PlatformRequestClose;
	};

	int TabWindows::Occupants(const Entry& entry) const
	{
		std::vector<ImGuiID> tree;
		Tree(entry.host.dockSpace, tree);

		return (int)std::ranges::count_if(
			m_context->Windows,
			[&tree](const ImGuiWindow* const window) { return window->WasActive && Contains(tree, window->DockId); });
	};

	void TabWindows::Retire(
		Entry entry,
		const ImGuiID mainDockSpace,
		const bool hide)
	{
		std::vector<ImGuiID> tree;
		Tree(entry.host.dockSpace, tree);

		std::vector<std::string> names;

		const auto note = [&names](const char* const name)
		{
			if (std::ranges::find(names, name) == names.end())
				names.emplace_back(name);
		};

		for (const ImGuiWindow* const window : m_context->Windows)
			if (Contains(tree, window->DockId))
				note(window->Name);

		for (ImGuiWindowSettings* settings = m_context->SettingsWindows.begin(); settings != nullptr; settings = m_context->SettingsWindows.next_chunk(settings))
			if (Contains(tree, settings->DockId))
				note(settings->GetName());

		if (hide)
			for (ExtensionRegistry::Panel& panel : ExtensionRegistry::Panels())
				if (std::ranges::find(names, panel.title) != names.end())
					panel.shown = false;

		if (const ImGuiWindow* const window = ImGui::FindWindowByID(entry.host.windowId); window != nullptr && window->Viewport != nullptr)
			window->Viewport->PlatformRequestClose = false;

		ImGui::DockBuilderRemoveNode(entry.host.dockSpace);

		if (mainDockSpace != 0)
			for (const std::string& name : names)
				ImGui::DockBuilderDockWindow(name.c_str(), mainDockSpace);

		std::erase_if(m_entries, [&entry](const Entry& other) { return other.host.tag == entry.host.tag; });
		ImGui::MarkIniSettingsDirty();
	};

	void TabWindows::Adopt(ImGuiWindow& window)
	{
		const float strip = StripHeight();

		Entry entry;
		entry.host.tag = FreeTag(window.ID);
		entry.host.name = HostName(entry.host.tag);
		entry.host.windowId = ImHashStr(entry.host.name.c_str());
		entry.host.dockSpace = HostDockSpace(entry.host.tag);
		entry.placed = true;
		entry.pos = window.Pos;
		entry.size = ImVec2(ImMax(window.Size.x, 1.0f), ImMax(window.Size.y, 1.0f) + strip);

		ImGui::DockBuilderAddNode(entry.host.dockSpace, ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodePos(entry.host.dockSpace, ImVec2(entry.pos.x, entry.pos.y + strip));
		ImGui::DockBuilderSetNodeSize(entry.host.dockSpace, ImVec2(entry.size.x, entry.size.y - strip));
		ImGui::DockBuilderDockWindow(window.Name, entry.host.dockSpace);
		ImGui::DockBuilderFinish(entry.host.dockSpace);

		m_entries.push_back(std::move(entry));
		ImGui::MarkIniSettingsDirty();
	};

	void TabWindows::Draw(Entry& entry)
	{
		ImGuiWindowClass hostClass;
		hostClass.ViewportFlagsOverrideSet = ImGuiViewportFlags_NoAutoMerge;
		hostClass.ViewportFlagsOverrideClear = ImGuiViewportFlags_NoTaskBarIcon;
		hostClass.ParentViewportId = 0;

		if (entry.placed)
		{
			ImGui::SetNextWindowPos(entry.pos, ImGuiCond_Always);
			ImGui::SetNextWindowSize(entry.size, ImGuiCond_Always);
			entry.placed = false;
		}
		else ImGui::SetNextWindowSize(default_size, ImGuiCond_FirstUseEver);

		ImGui::SetNextWindowClass(&hostClass);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, zero);

		const bool open = ImGui::Begin(entry.host.name.c_str(), nullptr, host_flags);

		ImGui::PopStyleVar(3);

		if (open)
		{
			const float strip = StripHeight();
			const ImVec2 min = ImGui::GetWindowPos();
			const ImVec2 max(min.x + ImGui::GetWindowWidth(), min.y + strip);

			ImGui::GetWindowDrawList()->AddRectFilled(min, max, ImGui::GetColorU32(ImGuiCol_MenuBarBg));

			if (m_hooks.buttons && m_hooks.buttons(*ImGui::GetWindowViewport(), min, max))
				entry.closing = true;

			ImGui::SetCursorPos(ImVec2(0.0f, strip));
		};

		const std::function<void()> submit = [&entry]
		{
			ImGui::DockSpace(entry.host.dockSpace, zero, ImGuiDockNodeFlags_None, nullptr);
		};

		if (m_hooks.padded)
			m_hooks.padded(submit);
		else submit();

		ImGui::End();
	};

	void TabWindows::Update(const ImGuiID mainDockSpace)
	{
		if (!m_installed
			|| ImGui::GetCurrentContext() != m_context
			|| !(ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable))
			return;

		for (const Host& host : Hosts())
			if (const Entry* const entry = Find(host.tag); entry != nullptr && Requested(*entry))
				Retire(*entry, mainDockSpace, true);

		const ImGuiViewport* const main = ImGui::GetMainViewport();
		std::vector<ImGuiWindow*> adopted;

		for (ImGuiWindow* const window : m_context->Windows)
		{
			if (!Regular(*window))
				continue;

			if (window->DockId != 0)
			{
				m_docked.insert(window->ID);
				continue;
			};

			if (!m_docked.contains(window->ID)
				|| ImGui::IsMouseDown(ImGuiMouseButton_Left)
				|| (m_context->MovingWindow != nullptr && m_context->MovingWindow->RootWindowDockTree == window))
				continue;

			if (window->WasActive
				&& window->ViewportOwned
				&& window->Viewport != nullptr
				&& window->Viewport != main)
			{
				m_docked.erase(window->ID);
				adopted.push_back(window);
			};
		};

		for (ImGuiWindow* const window : adopted)
			Adopt(*window);

		for (const Host& host : Hosts())
		{
			Entry* const entry = Find(host.tag);

			if (entry == nullptr)
				continue;

			++entry->age;

			if (entry->age > empty_grace_frames && Occupants(*entry) == 0)
				Retire(*entry, mainDockSpace, false);
			else Draw(*entry);
		};
	};

	void TabWindows::UpdatePlatformWindows()
	{
		if (m_installed && ImGui::GetCurrentContext() == m_context)
			for (const ImGuiWindow* const window : m_context->Windows)
				if (window->DockId != 0
					&& Regular(*window))
					m_docked.insert(window->ID);

		if (m_hooks.current
			&& m_hooks.makeCurrent)
			UpdateViewportWindows(m_hooks.current, m_hooks.makeCurrent);
		else ImGui::UpdatePlatformWindows();
	};
};
