#pragma once

#include "imgui.h"

#include <deque>
#include <functional>
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
	inline constexpr int ExtensionApiRevision = 2;

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
	*   until the user opens it there
	* - An extension is a shared library in the extensions folder, built with
	*   LOOM_SCRIPT_MODULE against the editor's import library as scripts are.
	*   It exports LoomExtensionAbi, LoomExtensionStart and LoomExtensionStop
	* - An install leaves <name>.extension.json beside it, holding Source and
	*   LoomRoot. update-editor.ps1 then runs Source's update-extension.ps1
	*   -LoomRoot <that Loom> whenever the editor is newer than the library
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

		static std::deque<Panel>& Panels();
	};
};
