#pragma once

#include "imgui.h"

struct ImRect;


/**
* ImGuiItemListener
* - Hears about every item ImGui lays out, through the hooks imgui_test_engine
*   would otherwise fill in (IMGUI_ENABLE_TEST_ENGINE, in imconfig.h)
* - Set as the context's TestEngine, with TestEngineHookItems on. A context
*   with no listener set costs a branch per item and calls nothing
*/
struct ImGuiItemListener
{
	virtual ~ImGuiItemListener() = default;

	// Every item, with the rectangle it takes up.
	virtual void ItemAdded(ImGuiID id, const ImRect& rect) = 0;

	// The items that have a label, after ItemAdded: ImGui's own label, ## and all.
	virtual void ItemLabelled(ImGuiID id, const char* label) = 0;
};
