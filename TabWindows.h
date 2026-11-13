#pragma once

#include "WindowFrame.h"

#include "imgui.h"

#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

struct ImGuiSettingsHandler;
struct ImGuiWindow;


namespace Loom
{
	// The backend makes a new viewport's context current and destroying one leaves none, so the caller's context is put back.
	template <typename Current, typename MakeCurrent>
	void UpdateViewportWindows(
		const Current& current,
		const MakeCurrent& makeCurrent)
	{
		const auto kept = current();
		ImGui::UpdatePlatformWindows();

		if (current() != kept)
			makeCurrent(kept);
	};

	/**
	* Loom::TabWindows
	* - A tab dragged out of a dock node and let go outside the main window
	*   becomes its own OS window: a host window holding a dock space, with its
	*   own tab bar that other tabs dock into, and the window buttons above it
	* - Dragging a tab back onto another tab bar docks it there with ImGui's own
	*   docking; a host with no tabs left goes
	* - Closing a host, by its button or the OS, hides its tabs the way their own
	*   close buttons do, and docks them into the main dock space
	* - Nothing here calls the platform: the platform window's frame, the window
	*   buttons and the GL context come in through Hooks, so the logic runs
	*   with fake handles
	* - Does nothing while ImGui's viewports are off
	*/
	struct TabWindows final
	{
		using Padded = std::function<void(const std::function<void()>&)>;

		struct Hooks
		{
			// The GL context current now, and a way to make one current again.
			std::function<void*()> current;
			std::function<void(void*)> makeCurrent;

			// A host's platform window has just been made, and is about to be destroyed.
			std::function<void(ImGuiViewport&)> frame;
			std::function<void(ImGuiViewport&)> unframe;

			// Draws the window buttons in the strip (screen coordinates) and names its drag strip; true when close is pressed.
			std::function<bool(ImGuiViewport&, ImVec2, ImVec2)> buttons;

			// Runs the call that submits a host's dock space, for the app's tab padding.
			Padded padded;
		};

		struct Host
		{
			ImGuiID tag = 0;
			std::string name;
			ImGuiID windowId = 0;
			ImGuiID dockSpace = 0;
		};

		// Hooks that frame each host's GLFW window with a WindowFrame and draw its buttons, on the current GLFW context.
		static Hooks Glfw(WindowFrame::Redraw redraw);

		static std::string HostName(ImGuiID tag);
		static ImGuiID HostDockSpace(ImGuiID tag);

		explicit TabWindows(Hooks hooks);
		TabWindows(const TabWindows&) = delete;
		TabWindows& operator=(const TabWindows&) = delete;
		~TabWindows();

		// Wraps the backend's platform and renderer callbacks, so call it after they are set, and adds the settings handler. False, and nothing done, when viewports are off.
		bool Install();

		bool IsInstalled() const { return m_installed; };

		// Once a frame between NewFrame and the windows' Begin calls, after the main dock space is submitted.
		void Update(ImGuiID mainDockSpace);

		// After Render in place of ImGui::UpdatePlatformWindows, keeping the caller's GL context.
		void UpdatePlatformWindows();

		std::vector<Host> Hosts() const;

		// Strip height at the current style.
		static float StripHeight();

	private:
		struct Entry
		{
			Host host;
			int age = 0;
			bool placed = false;
			bool closing = false;
			ImVec2 pos{ };
			ImVec2 size{ };
		};

		static void OnCreateWindow(ImGuiViewport* viewport);
		static void OnDestroyWindow(ImGuiViewport* viewport);
		static void OnRenderWindow(ImGuiViewport* viewport, void* argument);
		static void ClearSettings(ImGuiContext* context, ImGuiSettingsHandler* handler);
		static void* OpenSettings(ImGuiContext* context, ImGuiSettingsHandler* handler, const char* name);
		static void ReadSetting(ImGuiContext* context, ImGuiSettingsHandler* handler, void* entry, const char* line);
		static void WriteSettings(ImGuiContext* context, ImGuiSettingsHandler* handler, ImGuiTextBuffer* out);

		void Created(ImGuiViewport& viewport);
		void Destroying(ImGuiViewport& viewport);
		void Rendering(ImGuiViewport& viewport);
		void Restore(ImGuiID tag);
		Entry* Find(ImGuiID tag);
		ImGuiID FreeTag(ImGuiID wanted) const;
		bool Requested(const Entry& entry) const;
		int Occupants(const Entry& entry) const;
		void Retire(Entry entry, ImGuiID mainDockSpace, bool hide);
		void Adopt(ImGuiWindow& window);
		void Draw(Entry& entry);

		Hooks m_hooks;
		ImGuiContext* m_context = nullptr;
		bool m_installed = false;
		std::vector<Entry> m_entries;
		std::unordered_set<ImGuiID> m_docked;
		std::unordered_set<ImGuiID> m_framed;
		void(*m_create)(ImGuiViewport*) = nullptr;
		void(*m_destroy)(ImGuiViewport*) = nullptr;
		void(*m_render)(ImGuiViewport*, void*) = nullptr;
	};
};
