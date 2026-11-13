#pragma once

#include "imgui.h"

#include <functional>
#include <list>
#include <string>
#include <vector>


// The registry is exported from the editor executable, so an extension built
// with LOOM_SCRIPT_MODULE imports it rather than carrying a second one. A
// static consumer of Loom UI defines LOOM_UI_STATIC and has neither.
#if defined(_WIN32) && !defined(__EMSCRIPTEN__) && !defined(LOOM_UI_STATIC)
	#ifdef LOOM_SCRIPT_MODULE
		#define LOOM_UI_API __declspec(dllimport)
	#else
		#define LOOM_UI_API __declspec(dllexport)
	#endif
#else
	#define LOOM_UI_API
#endif

// What the editor looks up in an extension's library. Stop runs before the
// library unloads, so its threads and child processes end there rather than
// under the loader's lock.
#ifdef _WIN32
	#define LOOM_EXTENSION_EXPORT extern "C" __declspec(dllexport)
#else
	#define LOOM_EXTENSION_EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace Loom
{
	// Raised by hand with any change to what an extension calls here or in
	// EditorGui.h. Nothing checks that it was.
	inline constexpr int ExtensionApiRevision = 3;

	// An extension calls straight into ImGui and these, and shares the editor's
	// standard library types. One built against another ImGui, API revision or
	// configuration is refused.
#ifdef _DEBUG
	inline constexpr int ExtensionAbi = (IMGUI_VERSION_NUM * 100 + ExtensionApiRevision) * 10 + 2;
#else
	inline constexpr int ExtensionAbi = (IMGUI_VERSION_NUM * 100 + ExtensionApiRevision) * 10 + 1;
#endif

	/**
	* Loom::ExtensionRegistry
	* - What an extension adds to the editor. A panel docks, tears off and
	*   shows in the Window menu as the editor's own do, and stays closed
	*   until the user opens it there. A menu item joins the main menu bar
	* - Whatever an extension adds, while it starts or later, is its own, and
	*   leaves with it when it unloads; other extensions' and the editor's stay.
	*   The owner is the module the call to AddPanel or AddMenuItem came from
	* - An extension is a shared library in the extensions folder, built with
	*   LOOM_SCRIPT_MODULE against the editor's import library as scripts are.
	*   It exports LoomExtensionAbi, LoomExtensionStart and LoomExtensionStop
	* - An install leaves <name>.extension.json beside it, holding Source and
	*   LoomRoot. update-editor.ps1 then runs Source's update-extension.ps1
	*   -LoomRoot <that Loom> whenever the editor is newer than the library. A
	*   manifest may also carry Library, the file that script builds, to judge
	*   that rather than a copy in the extensions folder
	* - A .loomproject may hold extension=<path> lines, each a library built the
	*   same way, relative to the project's folder. They load with the project
	*   and unload when another opens
	*/
	struct LOOM_UI_API ExtensionRegistry final
	{
		struct Panel
		{
			std::string title;
			std::function<void(const std::string& project)> draw;
			std::function<void(const std::string& project)> update;
			std::function<void(const std::vector<std::string>& files)> drop;
			bool shown = false;
			const void* owner = nullptr;
		};

		struct MenuItem
		{
			std::string menu;
			std::string name;
			std::function<void(const std::string& project)> select;
			std::function<bool(const std::string& project)> enabled;
			std::function<std::string(const std::string& project)> tooltip;
			const void* owner = nullptr;
		};

		// draw runs between ImGui::Begin and End while the panel shows. update
		// runs every frame whether it shows or not, and draws nothing. Both are
		// given the open project's folder. drop takes files dropped on the
		// panel from outside the editor, which then stay out of the project.
		static void AddPanel(
			const std::string& icon,
			const std::string& name,
			std::function<void(const std::string& project)> draw,
			std::function<void(const std::string& project)> update = { },
			std::function<void(const std::vector<std::string>& files)> drop = { });

		// A list, so what one library takes out on unloading moves no other
		// entry. A pointer to a panel holds until that panel is removed.
		static std::list<Panel>& Panels();

		// Puts an item in the editor's main menu bar under the top-level menu
		// named menu, such as "File". select runs when it is clicked. enabled,
		// if given, decides whether it can be; tooltip, if given, shows on hover
		// when it gives text. Each is given the open project's folder. A menu
		// the editor lacks gets its own, after the editor's.
		static void AddMenuItem(
			const std::string& menu,
			const std::string& name,
			std::function<void(const std::string& project)> select,
			std::function<bool(const std::string& project)> enabled = { },
			std::function<std::string(const std::string& project)> tooltip = { });

		static std::list<MenuItem>& MenuItems();
	};
};
