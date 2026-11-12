#include "ImGuiItemHooks.h"

#include "imgui_internal.h"


namespace
{
	ImGuiItemListener* ListenerOf(ImGuiContext* context)
	{
		return (ImGuiItemListener*)context->TestEngine;
	};
};


void ImGuiTestEngineHook_ItemAdd(ImGuiContext* context, ImGuiID id, const ImRect& bb, const ImGuiLastItemData*)
{
	if (ImGuiItemListener* listener = ListenerOf(context))
		listener->ItemAdded(id, bb);
};

void ImGuiTestEngineHook_ItemInfo(ImGuiContext* context, ImGuiID id, const char* label, ImGuiItemStatusFlags)
{
	if (ImGuiItemListener* listener = ListenerOf(context))
		listener->ItemLabelled(id, label);
};

void ImGuiTestEngineHook_Log(ImGuiContext*, const char*, ...)
{
};

const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext*, ImGuiID)
{
	return nullptr;
};
