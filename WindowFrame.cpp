#include "WindowFrame.h"

#include "imgui.h"

#include <GLFW/glfw3.h>

// The title bar comes off on Windows and X11. Wayland keeps its compositor's, and the web test build compiles this too.
#ifdef _WIN32

#include <GLFW/glfw3.h>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")

#elif !defined(__EMSCRIPTEN__)

#include <GLFW/glfw3.h>

#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3native.h>

#include <cmath>
#include <limits>
#include <unordered_map>

#endif


namespace Loom
{
	WindowFrame::Hit WindowFrame::HitTest(float x, float y, float width, float height, float border, bool maximized, const Rect& strip)
	{
		if (!maximized)
		{
			const bool left = x < border;
			const bool right = x >= width - border;
			const bool top = y < border;
			const bool bottom = y >= height - border;

			if (top && left)
				return Hit::TopLeft;

			if (top && right)
				return Hit::TopRight;

			if (bottom && left)
				return Hit::BottomLeft;

			if (bottom && right)
				return Hit::BottomRight;

			if (left)
				return Hit::Left;

			if (right)
				return Hit::Right;

			if (top)
				return Hit::Top;

			if (bottom)
				return Hit::Bottom;
		};

		if (x >= strip.left && x < strip.right && y >= strip.top && y < strip.bottom)
			return Hit::Caption;

		return Hit::Client;
	};
};

#ifdef _WIN32

namespace Loom
{
	namespace
	{
		// Tags this subclass on the window, among any others it has.
		constexpr UINT_PTR SUBCLASS_ID = 0x4C4F4F4D;

		// Snapping, the maximize animation and the shadow all go with a bare popup.
		constexpr LONG FRAMED_STYLE = WS_CAPTION | WS_THICKFRAME | WS_MAXIMIZEBOX;

		// The one window the OS is moving or sizing, which a thread has at most one of.
		HWND s_held = nullptr;

		// Not inside another redraw, as one that pumps messages would be. False when it was.
		bool RedrawOnce(const WindowFrame& frame)
		{
			static bool redrawing = false;

			if (redrawing)
				return false;

			redrawing = true;
			frame.Redrawing()();
			redrawing = false;

			return true;
		};

		// A held window posts this to itself for its next frame, so a timer's tick does not pace them.
		constexpr UINT REDRAW_AGAIN = WM_APP + 1;

		// One frame to a message, so the OS's loop always has the window back between frames, and its timer resumes them after whatever else waits.
		void RedrawHeld(HWND window, const WindowFrame& frame)
		{
			if (RedrawOnce(frame) && window == s_held && HIWORD(GetQueueStatus(QS_ALLINPUT)) == 0)
				PostMessageW(window, REDRAW_AGAIN, 0, 0);
		};

		LRESULT HitCode(WindowFrame::Hit hit)
		{
			switch (hit)
			{
			case WindowFrame::Hit::Caption: return HTCAPTION;
			case WindowFrame::Hit::Left: return HTLEFT;
			case WindowFrame::Hit::Right: return HTRIGHT;
			case WindowFrame::Hit::Top: return HTTOP;
			case WindowFrame::Hit::Bottom: return HTBOTTOM;
			case WindowFrame::Hit::TopLeft: return HTTOPLEFT;
			case WindowFrame::Hit::TopRight: return HTTOPRIGHT;
			case WindowFrame::Hit::BottomLeft: return HTBOTTOMLEFT;
			case WindowFrame::Hit::BottomRight: return HTBOTTOMRIGHT;
			default: return HTCLIENT;
			};
		};

		// Windows 10's per-monitor calls, which the Windows 7 target leaves undeclared.
		int Metric(HWND window, int index)
		{
			using DpiForWindow = UINT(WINAPI*)(HWND);
			using MetricForDpi = int(WINAPI*)(int, UINT);

			static const HMODULE user32 = GetModuleHandleW(L"user32.dll");
			static const DpiForWindow dpi_for_window = (DpiForWindow)GetProcAddress(user32, "GetDpiForWindow");
			static const MetricForDpi metric_for_dpi = (MetricForDpi)GetProcAddress(user32, "GetSystemMetricsForDpi");

			if (dpi_for_window == nullptr || metric_for_dpi == nullptr)
				return GetSystemMetrics(index);

			return metric_for_dpi(index, dpi_for_window(window));
		};

		LRESULT CALLBACK Procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR, DWORD_PTR data)
		{
			const WindowFrame& frame = *(const WindowFrame*)data;
			const bool framed = (GetWindowLongW(window, GWL_STYLE) & WS_THICKFRAME) != 0;

			// Taken just after a composition, leaving the most time to draw the new size.
			if (message == WM_NCCALCSIZE && wparam && window == s_held && (((const NCCALCSIZE_PARAMS*)lparam)->lppos->flags & SWP_NOSIZE) == 0)
				DwmFlush();

			// A maximized window hangs its frame off the monitor's edges.
			if (message == WM_NCCALCSIZE && wparam && framed)
			{
				if (IsZoomed(window))
				{
					const int padding = Metric(window, SM_CXPADDEDBORDER);
					RECT& client = ((NCCALCSIZE_PARAMS*)lparam)->rgrc[0];

					InflateRect(
						&client,
						-(Metric(window, SM_CXSIZEFRAME) + padding),
						-(Metric(window, SM_CYSIZEFRAME) + padding));
				};

				return 0;
			};

			// The OS holds the frame loop while it moves or sizes the window, so frames come from its moves and size changes, and from the window posting itself the next one, meanwhile.
			if (message == WM_ENTERSIZEMOVE && frame.Redrawing() != nullptr)
			{
				s_held = window;
				SetTimer(window, SUBCLASS_ID, USER_TIMER_MINIMUM, nullptr);
			};

			if (message == WM_EXITSIZEMOVE)
			{
				if (s_held == window)
					s_held = nullptr;

				KillTimer(window, SUBCLASS_ID);
			};

			// The timer only comes once the mouse is still, so each new place and size is drawn before the OS takes the next.
			if (message == WM_WINDOWPOSCHANGED && window == s_held && (((const WINDOWPOS*)lparam)->flags & (SWP_NOSIZE | SWP_NOMOVE)) != (SWP_NOSIZE | SWP_NOMOVE))
			{
				const LRESULT result = DefSubclassProc(window, message, wparam, lparam);
				RedrawHeld(window, frame);
				return result;
			};

			if (message == REDRAW_AGAIN || (message == WM_TIMER && wparam == SUBCLASS_ID))
			{
				if (window == s_held)
					RedrawHeld(window, frame);

				return 0;
			};

			if (message == WM_NCHITTEST && framed)
			{
				POINT point = { (short)LOWORD(lparam), (short)HIWORD(lparam) };
				RECT client{ };

				ScreenToClient(window, &point);
				GetClientRect(window, &client);

				const WindowFrame::Hit hit = WindowFrame::HitTest(
					(float)point.x,
					(float)point.y,
					(float)client.right,
					(float)client.bottom,
					(float)Metric(window, SM_CXSIZEFRAME),
					IsZoomed(window) != FALSE,
					frame.DragStrip());

				return HitCode(hit);
			};

			return DefSubclassProc(window, message, wparam, lparam);
		};
	};

	WindowFrame::~WindowFrame()
	{
		if (m_window == nullptr)
			return;

		const HWND window = glfwGetWin32Window(m_window);

		if (window == s_held)
			s_held = nullptr;

		RemoveWindowSubclass(window, Procedure, SUBCLASS_ID);
	};

	WindowFrame::Hit WindowFrame::Hovered() const
	{
		return Hit::Client;
	};

	void WindowFrame::Install(GLFWwindow* window, Redraw redraw)
	{
		if (window == nullptr || m_window != nullptr)
			return;

		if (!SetWindowSubclass(glfwGetWin32Window(window), Procedure, SUBCLASS_ID, (DWORD_PTR)this))
			return;

		m_window = window;
		m_redraw = redraw;

		// The whole window is client area, so GLFW sizes it as frameless.
		glfwSetWindowAttrib(window, GLFW_DECORATED, GLFW_FALSE);
		Fit();
	};

	void WindowFrame::Fit()
	{
		if (m_window == nullptr)
			return;

		const HWND hwnd = glfwGetWin32Window(m_window);
		const LONG style = GetWindowLongW(hwnd, GWL_STYLE);
		const LONG fitted = glfwGetWindowMonitor(m_window) != nullptr ? style & ~FRAMED_STYLE : style | FRAMED_STYLE;

		if (fitted == style)
			return;

		SetWindowLongW(hwnd, GWL_STYLE, fitted);
		SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
	};
};

#elif !defined(__EMSCRIPTEN__)

namespace Loom
{
	namespace
	{
		// At the content scale, as deep as a window manager's own resizing edges.
		constexpr float BORDER = 6.0f;

		// GTK's defaults, which the desktop's own title bars keep.
		constexpr double DOUBLE_CLICK = 0.4;
		constexpr double DRAG_THRESHOLD = 8.0;

		constexpr double NEVER = -std::numeric_limits<double>::infinity();

		// _NET_WM_MOVERESIZE's directions.
		enum class Direction : long { SizeTopLeft, SizeTop, SizeTopRight, SizeRight, SizeBottomRight, SizeBottom, SizeBottomLeft, SizeLeft, Move };

		// _NET_WM_MOVERESIZE's mark for a request from an application.
		constexpr long FROM_APPLICATION = 1;

		struct Link
		{
			const WindowFrame* frame = nullptr;
			GLFWmousebuttonfun pressed = nullptr;
			GLFWcursorposfun moved = nullptr;
			GLFWcursorenterfun entered = nullptr;
			double caption = NEVER;

			// Kept from the mouse's last move, so a frame asks the X server nothing.
			WindowFrame::Hit hovered = WindowFrame::Hit::Client;

			// Where the caption was pressed, which drags the window once the mouse goes far enough.
			bool held = false;
			double held_x = 0.0;
			double held_y = 0.0;
		};

		std::unordered_map<GLFWwindow*, Link> s_links;

		float Scale(GLFWwindow* window)
		{
			float scale = 1.0f;
			glfwGetWindowContentScale(window, &scale, nullptr);
			return scale;
		};

		WindowFrame::Hit Under(const WindowFrame& frame, GLFWwindow* window, double x, double y)
		{
			if (glfwGetWindowMonitor(window) != nullptr)
				return WindowFrame::Hit::Client;

			int width = 0;
			int height = 0;

			glfwGetWindowSize(window, &width, &height);

			return WindowFrame::HitTest(
				(float)x,
				(float)y,
				(float)width,
				(float)height,
				BORDER * Scale(window),
				glfwGetWindowAttrib(window, GLFW_MAXIMIZED) == GLFW_TRUE,
				frame.DragStrip());
		};

		Direction DirectionOf(WindowFrame::Hit hit)
		{
			switch (hit)
			{
			case WindowFrame::Hit::Left: return Direction::SizeLeft;
			case WindowFrame::Hit::Right: return Direction::SizeRight;
			case WindowFrame::Hit::Top: return Direction::SizeTop;
			case WindowFrame::Hit::Bottom: return Direction::SizeBottom;
			case WindowFrame::Hit::TopLeft: return Direction::SizeTopLeft;
			case WindowFrame::Hit::TopRight: return Direction::SizeTopRight;
			case WindowFrame::Hit::BottomLeft: return Direction::SizeBottomLeft;
			case WindowFrame::Hit::BottomRight: return Direction::SizeBottomRight;
			default: return Direction::Move;
			};
		};

		// The window manager moves or sizes the window as its own title bar would, held at x, y.
		void MoveResize(GLFWwindow* window, Direction direction, double x, double y)
		{
			Display* const display = glfwGetX11Display();
			const ::Window handle = glfwGetX11Window(window);
			const ::Window root = DefaultRootWindow(display);
			::Window child = 0;
			int root_x = 0;
			int root_y = 0;

			XTranslateCoordinates(display, handle, root, (int)x, (int)y, &root_x, &root_y, &child);
			XUngrabPointer(display, CurrentTime);

			XEvent event{ };
			event.xclient.type = ClientMessage;
			event.xclient.window = handle;
			event.xclient.message_type = XInternAtom(display, "_NET_WM_MOVERESIZE", False);
			event.xclient.format = 32;
			event.xclient.data.l[0] = root_x;
			event.xclient.data.l[1] = root_y;
			event.xclient.data.l[2] = (long)direction;
			event.xclient.data.l[3] = Button1;
			event.xclient.data.l[4] = FROM_APPLICATION;

			XSendEvent(display, root, False, SubstructureRedirectMask | SubstructureNotifyMask, &event);
			XFlush(display);
		};

		void Press(GLFWwindow* window, int button, int action, int mods)
		{
			const auto found = s_links.find(window);

			if (found == s_links.end())
				return;

			Link& link = found->second;
			const bool left = button == GLFW_MOUSE_BUTTON_LEFT;

			// ImGui never saw the press this lets go of.
			if (left && action == GLFW_RELEASE && link.held)
			{
				link.held = false;
				return;
			};

			double x = 0.0;
			double y = 0.0;
			glfwGetCursorPos(window, &x, &y);

			const WindowFrame::Hit hit = left && action == GLFW_PRESS ? Under(*link.frame, window, x, y) : WindowFrame::Hit::Client;

			if (hit == WindowFrame::Hit::Client)
			{
				if (link.pressed != nullptr)
					link.pressed(window, button, action, mods);

				return;
			};

			if (hit != WindowFrame::Hit::Caption)
			{
				MoveResize(window, DirectionOf(hit), x, y);
				return;
			};

			const double now = glfwGetTime();
			const bool twice = now - link.caption < DOUBLE_CLICK;

			link.caption = twice ? NEVER : now;
			link.held = !twice;
			link.held_x = x;
			link.held_y = y;

			if (!twice)
				return;

			if (glfwGetWindowAttrib(window, GLFW_MAXIMIZED))
				glfwRestoreWindow(window);
			else glfwMaximizeWindow(window);
		};

		// The window manager takes the mouse once it moves the window, so the
		// move waits for a drag, leaving a click for a double-click.
		void Move(GLFWwindow* window, double x, double y)
		{
			const auto found = s_links.find(window);

			if (found == s_links.end())
				return;

			Link& link = found->second;

			if (link.moved != nullptr)
				link.moved(window, x, y);

			link.hovered = Under(*link.frame, window, x, y);

			const double threshold = DRAG_THRESHOLD * Scale(window);

			if (!link.held || std::hypot(x - link.held_x, y - link.held_y) < threshold)
				return;

			link.held = false;
			MoveResize(window, Direction::Move, link.held_x, link.held_y);
		};

		void Enter(GLFWwindow* window, int entered)
		{
			const auto found = s_links.find(window);

			if (found == s_links.end())
				return;

			Link& link = found->second;

			if (link.entered != nullptr)
				link.entered(window, entered);

			if (!entered)
				link.hovered = WindowFrame::Hit::Client;
		};
	};

	WindowFrame::~WindowFrame()
	{
		const auto found = s_links.find(m_window);

		if (found == s_links.end())
			return;

		glfwSetMouseButtonCallback(m_window, found->second.pressed);
		glfwSetCursorPosCallback(m_window, found->second.moved);
		glfwSetCursorEnterCallback(m_window, found->second.entered);
		s_links.erase(found);
	};

	void WindowFrame::Install(GLFWwindow* window, Redraw redraw)
	{
		if (window == nullptr || m_window != nullptr || glfwGetPlatform() != GLFW_PLATFORM_X11)
			return;

		m_window = window;
		m_redraw = redraw;
		Link& link = s_links[window];
		link.frame = this;
		link.pressed = glfwSetMouseButtonCallback(window, Press);
		link.moved = glfwSetCursorPosCallback(window, Move);
		link.entered = glfwSetCursorEnterCallback(window, Enter);

		glfwSetWindowAttrib(window, GLFW_DECORATED, GLFW_FALSE);
	};

	// GLFW keeps the window undecorated over a monitor and back.
	void WindowFrame::Fit()
	{
	};

	WindowFrame::Hit WindowFrame::Hovered() const
	{
		const auto found = s_links.find(m_window);

		return found == s_links.end() ? Hit::Client : found->second.hovered;
	};
};

#endif

namespace Loom
{
	namespace
	{
		constexpr float GLYPH_SIZE = 0.2f;
		constexpr float GLYPH_OFFSET = 0.5f;
		constexpr float CENTRED = 0.5f;
		constexpr ImU32 CLOSE_HOVERED = IM_COL32(196, 43, 28, 255);
		constexpr ImU32 CLOSE_PRESSED = IM_COL32(148, 32, 21, 255);
		constexpr ImU32 CLOSE_GLYPH = IM_COL32(255, 255, 255, 255);

		ImGuiMouseCursor ResizeCursor(WindowFrame::Hit hit)
		{
			switch (hit)
			{
			case WindowFrame::Hit::Left:
			case WindowFrame::Hit::Right: return ImGuiMouseCursor_ResizeEW;
			case WindowFrame::Hit::Top:
			case WindowFrame::Hit::Bottom: return ImGuiMouseCursor_ResizeNS;
			case WindowFrame::Hit::TopLeft:
			case WindowFrame::Hit::BottomRight: return ImGuiMouseCursor_ResizeNWSE;
			case WindowFrame::Hit::TopRight:
			case WindowFrame::Hit::BottomLeft: return ImGuiMouseCursor_ResizeNESW;
			default: return ImGuiMouseCursor_None;
			};
		};
	};

	WindowFrame::Rect WindowFrame::StripFor(
		const ImVec2 barMin,
		const ImVec2 barMax,
		const float stripLeft,
		const ImVec2 origin)
	{
		const float height = barMax.y - barMin.y;

		return Rect{ stripLeft - origin.x, barMin.y - origin.y, ButtonsLeft(barMax.x, height) - origin.x, barMax.y - origin.y };
	};

	bool WindowFrame::DrawButtons(
		GLFWwindow* const window,
		const ImVec2 barMin,
		const ImVec2 barMax,
		const float stripLeft)
	{
		if (!IsInstalled())
			return false;

		const float height = barMax.y - barMin.y;
		const float width = ButtonWidth(height);
		const float buttonsLeft = ButtonsLeft(barMax.x, height);

		SetDragStrip(ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)
			? Rect{ }
			: StripFor(barMin, barMax, stripLeft, ImGui::GetWindowViewport()->Pos));

		if (const ImGuiMouseCursor cursor = ResizeCursor(Hovered()); cursor != ImGuiMouseCursor_None)
			ImGui::SetMouseCursor(cursor);

		ImDrawList& draw = *ImGui::GetWindowDrawList();
		const float glyph = height * GLYPH_SIZE;
		const float offset = glyph * GLYPH_OFFSET;
		const bool maximized = glfwGetWindowAttrib(window, GLFW_MAXIMIZED);
		ImVec2 center{ };
		ImU32 color = 0;

		const auto button = [&width, &barMin, &color, &height, &barMax, &center, &draw, &buttonsLeft](
			int index,
			const char* id,
			ImU32 hovered,
			ImU32 pressed) -> bool
		{
			const ImVec2 corner(buttonsLeft + width * index, barMin.y);

			ImGui::SetCursorScreenPos(corner);

			const bool clicked = ImGui::InvisibleButton(id, ImVec2(width, height));

			center = ImVec2(corner.x + width * CENTRED, corner.y + height * CENTRED);
			color = ImGui::GetColorU32(ImGuiCol_Text);

			if (ImGui::IsItemHovered())
				draw.AddRectFilled(corner, ImVec2(corner.x + width, barMax.y), ImGui::IsItemActive() ? pressed : hovered);

			return clicked;
		};

		draw.PushClipRect(barMin, barMax, false);

		if (button(0, "##Minimize", ImGui::GetColorU32(ImGuiCol_HeaderHovered), ImGui::GetColorU32(ImGuiCol_HeaderActive)))
			glfwIconifyWindow(window);

		draw.AddLine(ImVec2(center.x - glyph, center.y), ImVec2(center.x + glyph, center.y), color);

		if (button(1, "##Maximize", ImGui::GetColorU32(ImGuiCol_HeaderHovered), ImGui::GetColorU32(ImGuiCol_HeaderActive)))
		{
			if (maximized)
				glfwRestoreWindow(window);
			else glfwMaximizeWindow(window);
		};

		if (maximized)
		{
			draw.AddRect(ImVec2(center.x - glyph, center.y - glyph + offset), ImVec2(center.x + glyph - offset, center.y + glyph), color);
			draw.AddLine(ImVec2(center.x - glyph + offset, center.y - glyph + offset), ImVec2(center.x - glyph + offset, center.y - glyph), color);
			draw.AddLine(ImVec2(center.x - glyph + offset, center.y - glyph), ImVec2(center.x + glyph, center.y - glyph), color);
			draw.AddLine(ImVec2(center.x + glyph, center.y - glyph), ImVec2(center.x + glyph, center.y + glyph - offset), color);
			draw.AddLine(ImVec2(center.x + glyph, center.y + glyph - offset), ImVec2(center.x + glyph - offset, center.y + glyph - offset), color);
		}
		else draw.AddRect(ImVec2(center.x - glyph, center.y - glyph), ImVec2(center.x + glyph, center.y + glyph), color);

		const bool close = button(2, "##Close", CLOSE_HOVERED, CLOSE_PRESSED);

		if (ImGui::IsItemHovered())
			color = CLOSE_GLYPH;

		draw.AddLine(ImVec2(center.x - glyph, center.y - glyph), ImVec2(center.x + glyph, center.y + glyph), color);
		draw.AddLine(ImVec2(center.x - glyph, center.y + glyph), ImVec2(center.x + glyph, center.y - glyph), color);

		draw.PopClipRect();

		return close;
	};
};
