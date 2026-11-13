#pragma once

#include "imgui.h"
#include "imgui_internal.h"

#include <functional>
#include <string>


namespace Loom::Testing
{
	/**
	* Loom::Testing::Headless
	* - A Dear ImGui context with no window behind it, for tests: a display of
	*   a fixed size, no ini file and a built font atlas
	* - Owns the context and makes it current
	* - Frame runs one frame around a draw callback, Window runs one inside a
	*   full-display window with no title bar
	* - Find, Click, Type, Enter and Leave drive the mouse and keyboard
	*   through that callback, one frame per event, the way a user would
	*/
	class Headless final
	{
	public:
		using Draw = std::function<void()>;

		struct Options final
		{
			ImVec2 display = { 800, 600 };
			float frameSeconds = 1.0f / 60;
			ImGuiBackendFlags backend = 0;
			bool buildFonts = true;
			ImGuiConfigFlags config = 0;
		};

		Headless();
		explicit Headless(const Options& options);
		~Headless();

		Headless(const Headless&) = delete;
		Headless& operator=(const Headless&) = delete;

		ImGuiContext* Context() const;
		ImVec2 Display() const;

		void Frame(const Draw& draw) const;
		void Window(
			const char* name,
			const Draw& draw) const;

		ImGuiID Id(
			const char* window,
			const char* label) const;

		bool Hover(
			const ImRect& area,
			ImGuiID id,
			float step,
			const Draw& frame) const;

		ImVec2 Find(
			const ImRect& area,
			ImGuiID id,
			float step,
			const Draw& frame) const;

		bool Click(
			const ImRect& area,
			ImGuiID id,
			float step,
			const Draw& frame) const;

		void Press(
			int button,
			const Draw& frame) const;

		void Type(
			const std::string& letters,
			const Draw& frame) const;

		void Enter(const Draw& frame) const;
		void Leave(
			float step,
			const Draw& frame) const;

	private:
		ImGuiContext* const m_context;
		const ImVec2 m_display;
	};
};
