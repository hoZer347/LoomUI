#pragma once

struct GLFWwindow;


namespace Loom
{
	/**
	* Loom::WindowFrame
	* - Takes the OS title bar off a GLFW window, so the editor's menu bar sits
	*   at the very top and draws the window's own buttons
	* - The window's edges still resize it, and a strip the caller names each
	*   frame drags it, snaps it and maximizes it on a double-click, as a
	*   title bar does
	* - Redraws at each new place and size, and over and over while held
	*   still, while Windows moves or sizes it, which holds the frame loop
	*   until let go
	* - A window over a whole monitor has neither
	*/
	struct WindowFrame final
	{
		enum class Hit { Client, Caption, Left, Right, Top, Bottom, TopLeft, TopRight, BottomLeft, BottomRight };

		struct Rect
		{
			float left = 0.0f;
			float top = 0.0f;
			float right = 0.0f;
			float bottom = 0.0f;
		};

		WindowFrame() = default;
		WindowFrame(const WindowFrame&) = delete;
		WindowFrame& operator=(const WindowFrame&) = delete;
		~WindowFrame();

		using Redraw = void(*)();

		// Takes the title bar off window, keeping its client area the size it was.
		void Install(GLFWwindow* window, Redraw redraw);

		// Puts the frame back after the window has gone over a monitor or
		// come back from one, which resets its style.
		void Fit();

		bool IsInstalled() const { return m_window != nullptr; };

		// In client coordinates; an empty strip drags nothing.
		void SetDragStrip(const Rect& strip) { m_strip = strip; };
		const Rect& DragStrip() const { return m_strip; };

		Redraw Redrawing() const { return m_redraw; };

		// The edge or corner under the mouse, for the caller to show its
		// resizing cursor, which Windows shows by itself.
		Hit Hovered() const;

		// What a point in a client area of width by height is over, with
		// border the depth of its resizing edges, which a maximized window has none of.
		static Hit HitTest(float x, float y, float width, float height, float border, bool maximized, const Rect& strip);

	private:
		GLFWwindow* m_window = nullptr;
		Rect m_strip{ };
		Redraw m_redraw = nullptr;
	};
};
