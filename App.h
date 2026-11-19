#pragma once

#include "AppSettings.h"
#include "ExtensionRegistry.h"
#include "WindowFrame.h"
#include "imgui.h"

#include <functional>
#include <string>
#include <vector>

struct GLFWwindow;


namespace Loom
{
	/**
	* Loom::App
	* - The run loop of an ImGui editor on one GLFW window: window, GL, ImGui, theme, dock space, menu bar with
	*   the window's own buttons, the registered panels, files dropped on them, tab windows, pacing and shutdown
	* - The app gives Run its setup and its panels, and App keeps the rest the same from app to app: the window
	*   where it sat, imgui.ini beside it in the app's settings folder, the project argument and --screenshot
	* - --screenshot file [frames] runs hidden without viewports, writes the frame as a PNG and exits, 1 when it cannot
	* - Runs once per process
	*/
	struct App final
	{
		struct Options
		{
			std::string title;

			// The folder under %LOCALAPPDATA% for the app's settings, and one an earlier version used, copied once.
			std::string settingsFolder;
			std::string previousSettingsFolder;

			int width = 1280;
			int height = 800;
		};

		struct Callbacks
		{
			// Once the window, ImGui and the tab windows are up, before the first frame.
			std::function<void()> setup;

			// After the theme font is loaded and before the style is applied, for fonts to merge in.
			std::function<void()> font;

			// How the app widens padding around NewFrame, the dock space and docked windows, see ExtensionRegistry::Pad.
			ExtensionRegistry::Pad pad;

			// Inside the main menu bar, before the window's buttons.
			std::function<void()> menus;

			// Whether art is behind the windows this frame; drawBackdrop then draws it. The windows are veiled to show it.
			std::function<bool()> backdrop;
			std::function<void()> drawBackdrop;

			// On the first frame of a first run, to dock the panels.
			std::function<void(ImGuiID dockspace)> layout;

			// Whether the app wants frames at once, and whether it needs another pass to settle.
			std::function<bool()> busy;
			std::function<bool()> unsettled;

			// Right after each panel's window Begins, with its title, and once after the last panel's End while the veil still applies.
			ExtensionRegistry::Begun panelBegun;
			std::function<void()> panelsDrawing;

			// After the panels are drawn, and after each frame is rendered, and after each frame is shown.
			std::function<void()> panelsDrawn;
			std::function<void()> frameDone;
			std::function<void()> presented;

			// Before ImGui is shut down.
			std::function<void()> stop;

			// The layout file's text brought up to date; unchanged text leaves the file alone.
			std::function<std::string(const std::string&)> migrateLayout;
		};

		static int Run(
			const Options& options,
			const Callbacks& callbacks);

		static int Frames();
		static GLFWwindow* Window();
		static bool Hidden();
		static bool FirstRun();
		static bool Migrated();
		static const std::vector<std::string>& Arguments();
		static const AppSettings& Settings();

		static void SetProject(const std::string& project);
		static const std::string& Project();

		// Ends the wait for events, from any thread.
		static void Wake();
	};
};
