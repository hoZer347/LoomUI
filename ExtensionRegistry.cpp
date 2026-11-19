#include "ExtensionRegistry.h"

#include "imgui_internal.h"

#include <cstring>
#include <utility>

#ifdef _WIN32
	#define WIN32_LEAN_AND_MEAN
	#define NOMINMAX
	#include <Windows.h>
	#include <intrin.h>
	#define LOOM_CALLER_ADDRESS() _ReturnAddress()
#elif defined(__linux__)
	#include <dlfcn.h>
	#define LOOM_CALLER_ADDRESS() __builtin_return_address(0)
#else
	#define LOOM_CALLER_ADDRESS() nullptr
#endif


namespace Loom
{
	namespace
	{
		constexpr const char* PANEL_ENTRY = "Panels";

		const void* ModuleOf(const void* address)
		{
#ifdef _WIN32
			HMODULE module = nullptr;

			if (!GetModuleHandleExW(
				GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				(LPCWSTR)address,
				&module))
				return nullptr;

			return module;
#elif defined(__linux__)
			Dl_info info{ };

			if (dladdr(address, &info) == 0 || info.dli_fname == nullptr)
				return nullptr;

			void* const module = dlopen(info.dli_fname, RTLD_NOW | RTLD_NOLOAD);

			if (module != nullptr)
				dlclose(module);

			return module;
#else
			return nullptr;
#endif
		};
	};

	void ExtensionRegistry::AddPanel(
		const std::string& icon,
		const std::string& name,
		std::function<void(const std::string& project)> draw,
		std::function<void(const std::string& project)> update,
		std::function<void(const std::vector<std::string>& files)> drop)
	{
		const void* const owner = ModuleOf(LOOM_CALLER_ADDRESS());

		Panels().push_back({ Title(icon, name, name), std::move(draw), std::move(update), std::move(drop), false, owner });
	};

	void ExtensionRegistry::AddMenuItem(
		const std::string& menu,
		const std::string& name,
		std::function<void(const std::string& project)> select,
		std::function<bool(const std::string& project)> enabled,
		std::function<std::string(const std::string& project)> tooltip)
	{
		const void* const owner = ModuleOf(LOOM_CALLER_ADDRESS());

		MenuItems().push_back({ menu, name, std::move(select), std::move(enabled), std::move(tooltip), owner });
	};

	std::list<ExtensionRegistry::MenuItem>& ExtensionRegistry::MenuItems()
	{
		static std::list<MenuItem> items{ };

		return items;
	};

	std::list<ExtensionRegistry::Panel>& ExtensionRegistry::Panels()
	{
		static std::list<Panel> panels{ };

		return panels;
	};

	void ExtensionRegistry::AddPanelSettings(const char* const typeName)
	{
		ImGuiSettingsHandler handler;
		handler.TypeName = typeName;
		handler.TypeHash = ImHashStr(handler.TypeName);

		handler.ReadOpenFn = [](ImGuiContext*, ImGuiSettingsHandler*, const char* name) -> void*
		{
			return std::strcmp(name, PANEL_ENTRY) == 0 ? (void*)PANEL_ENTRY : nullptr;
		};

		handler.ReadLineFn = [](ImGuiContext*, ImGuiSettingsHandler*, void*, const char* line)
		{
			const char* const equals = std::strrchr(line, '=');

			if (equals == nullptr)
				return;

			const std::string_view name(line, equals);

			for (Panel& panel : Panels())
				if (Listed(panel)
					&& PanelId(panel.title) == name)
					panel.shown = equals[1] == '1';
		};

		handler.WriteAllFn = [](ImGuiContext*, ImGuiSettingsHandler* handler, ImGuiTextBuffer* buffer)
		{
			buffer->appendf("[%s][%s]\n", handler->TypeName, PANEL_ENTRY);

			for (const Panel& panel : Panels())
			{
				if (!Listed(panel))
					continue;

				const std::string_view id = PanelId(panel.title);

				buffer->appendf("%.*s=%d\n", (int)id.size(), id.data(), panel.shown ? 1 : 0);
			};

			buffer->append("\n");
		};

		ImGui::AddSettingsHandler(&handler);
	};

	void ExtensionRegistry::DrawAll(
		const std::string& project,
		std::vector<std::string>& dropped,
		const Pad& pad,
		const Begun& begun,
		const std::function<void()>& drawn)
	{
		for (Panel& panel : Panels())
			if (panel.update)
				panel.update(project);

		for (Panel& panel : Panels())
		{
			if (!panel.shown)
				continue;

			const bool open = [&panel, &pad]
			{
				const auto begin = [&panel](bool* const result)
				{
					*result = ImGui::Begin(panel.title.c_str(), &panel.shown);
				};

				bool began = false;

				if (!pad)
				{
					begin(&began);
					return began;
				};

				// A floating window keeps its title bar at the normal height; one not made yet may be docked by saved layout.
				const ImGuiWindow* const window = ImGui::FindWindowByName(panel.title.c_str());
				pad(window == nullptr || window->DockId != 0, [&begin, &began] { begin(&began); });

				return began;
			}();

			if (begun)
				begun(panel.title);

			if (open)
			{
				panel.draw(project);

				if (panel.drop && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && !dropped.empty())
				{
					std::vector<std::string> files;
					files.swap(dropped);
					panel.drop(files);
				};
			};

			ImGui::End();
		};

		if (drawn)
			drawn();

		dropped.clear();
	};
};
