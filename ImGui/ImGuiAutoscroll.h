#pragma once

#include "imgui.h"


/**
* ImGuiAutoscroll
* - A browser's middle-click scrolling, for every window of a context that
*   scrolls: a middle press over one marks where it was and scrolls toward the
*   pointer, faster the further it gets. Letting go after a drag ends it.
*   Letting go without one leaves it running until the next click or Escape
* - The window that takes it is the one the wheel would. An axis with nothing
*   to scroll stays put
* - Whoever uses the middle button where the mouse is keeps it, by calling
*   KeepMiddleMouse every frame the mouse is over them. A tab's own close keeps
*   it without asking
*/
namespace ImGuiAutoscroll
{
	// Once, on the current context, which it lasts as long as.
	IMGUI_API void Install();

	IMGUI_API void KeepMiddleMouse();

	IMGUI_API bool IsActive();

	// Running with the button up, waiting for a click or Escape to end it.
	IMGUI_API bool IsSticky();

	// The way it is scrolling, -1, 0 or 1 on each axis. Nothing in the dead zone.
	IMGUI_API ImVec2 Heading();
};
