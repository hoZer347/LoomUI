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

	std::string ExtensionRegistry::Title(
		const std::string& icon,
		const std::string& label,
		const std::string& id)
	{
		return icon + " " + label + TitleMarker + id;
	};

	std::string_view ExtensionRegistry::PanelId(const std::string& title)
	{
		const size_t marker = title.rfind(TitleMarker);

		return marker == std::string::npos ? std::string_view(title) : std::string_view(title).substr(marker + std::strlen(TitleMarker));
	};

	bool ExtensionRegistry::Named(
		const Panel& panel,
		const std::string& id)
	{
		return panel.title.ends_with(TitleMarker + id);
	};

	void ExtensionRegistry::Retitle(
		Panel& panel,
		std::string title)
	{
		if (panel.title != title)
			panel.title = std::move(title);
	};

	void ExtensionRegistry::Focus(Panel& panel)
	{
		panel.shown = true;
		ImGui::SetWindowFocus(panel.title.c_str());
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

	void ExtensionRegistry::DrawWindowItems(const float itemSpacingY)
	{
		const ImVec2 spacing = ImGui::GetStyle().ItemSpacing;

		if (itemSpacingY > 0.0f)
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(spacing.x, itemSpacingY));

		for (Panel& panel : Panels())
			if (Listed(panel))
				ImGui::MenuItem(panel.title.c_str(), nullptr, &panel.shown);

		if (itemSpacingY > 0.0f)
			ImGui::PopStyleVar();
	};

	void ExtensionRegistry::DrawAll(
		const std::string& project,
		std::vector<std::string>& dropped,
		const Pad& pad)
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

		dropped.clear();
	};
};
