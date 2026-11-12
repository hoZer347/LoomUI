#include "ImGuiAutoscroll.h"

#include "imgui_internal.h"

#include <cmath>


namespace
{
	#pragma region Tuning

	constexpr float DEAD_ZONE = 12.0f;

	// Pixels a second, from each pixel past the dead zone and from its square.
	constexpr float SPEED_LINEAR = 3.0f;
	constexpr float SPEED_SQUARED = 0.03f;

	// tan(22.5 degrees), where a heading turns from an axis to a diagonal.
	constexpr float DIAGONAL_SLOPE = 0.414f;

	// Sizes at this font size, which they scale with.
	constexpr float BASE_FONT_SIZE = 13.0f;
	constexpr float MARKER_RADIUS = 16.0f;
	constexpr float HEADING_SCALE = 1.6f;
	constexpr float DOT_RADIUS = 2.5f;
	constexpr float ARROW_GAP = 4.0f;
	constexpr float ARROW_LENGTH = 6.0f;
	constexpr float ARROW_HALF_WIDTH = 4.5f;
	constexpr float OUTLINE_WIDTH = 1.0f;

	constexpr ImU32 FILL = IM_COL32(255, 255, 255, 255);
	constexpr ImU32 OUTLINE = IM_COL32(0, 0, 0, 255);
	constexpr ImU32 MARKER_FILL = IM_COL32(255, 255, 255, 170);

	#pragma endregion

	#pragma region State

	struct State
	{
		bool active = false;
		bool sticky = false;
		bool dragged = false;

		ImGuiID window = 0;
		ImVec2 origin{ };
		ImVec2 axes{ };
		ImVec2 heading{ };

		// What is left of a step after whole pixels, which is all a window takes.
		ImVec2 remainder{ };

		// One bit per button held from a press nothing else heard.
		int swallowed = 0;

		int keptFrame = -1;
	};

	void OnNewFrame(ImGuiContext* context, ImGuiContextHook* hook);

	State* Find(ImGuiContext* context)
	{
		if (!context)
			return nullptr;

		for (const ImGuiContextHook& hook : context->Hooks)
			if (hook.Callback == OnNewFrame)
				return (State*)hook.UserData;

		return nullptr;
	};

	ImGuiID Owner()
	{
		static const ImGuiID owner = ImHashStr("ImGuiAutoscroll");

		return owner;
	};

	#pragma endregion

	#pragma region Choosing a Window

	// 1 on each axis that overflows and takes the wheel, or 0.
	ImVec2 ScrollAxes(const ImGuiWindow& window)
	{
		if (window.Flags & ImGuiWindowFlags_NoScrollWithMouse)
			return ImVec2(0.0f, 0.0f);

		return ImVec2(
			window.ScrollbarX && window.ScrollMax.x > 0.0f ? 1.0f : 0.0f,
			window.ScrollMax.y > 0.0f ? 1.0f : 0.0f);
	};

	bool Scrolls(const ImVec2& axes)
	{
		return axes.x != 0.0f || axes.y != 0.0f;
	};

	ImGuiWindow* FindTarget(ImGuiWindow* window)
	{
		while (!Scrolls(ScrollAxes(*window)))
		{
			if (!(window->Flags & ImGuiWindowFlags_ChildWindow))
				return nullptr;

			window = window->ParentWindow;
		};

		return window;
	};

	bool OverTabBar(ImGuiContext& g)
	{
		for (int index = 0; index < g.TabBars.GetMapSize(); index++)
		{
			const ImGuiTabBar* bar = g.TabBars.TryGetMapData(index);

			if (bar &&
				bar->Window == g.HoveredWindow &&
				bar->CurrFrameVisible == g.FrameCount - 1 &&
				bar->BarRect.Contains(g.IO.MousePos))
				return true;
		};

		return false;
	};

	bool Taken(ImGuiContext& g, const State& state)
	{
		return
			state.keptFrame == g.FrameCount - 1 ||
			g.IO.MouseDown[ImGuiMouseButton_Left] ||
			g.IO.MouseDown[ImGuiMouseButton_Right] ||
			!g.HoveredWindow ||
			!g.HoveredWindow->InnerRect.Contains(g.IO.MousePos) ||
			OverTabBar(g);
	};

	#pragma endregion

	#pragma region Running

	// Hides a press from everything else, for as long as it is held.
	void Swallow(ImGuiContext& g, State& state, ImGuiMouseButton button)
	{
		g.IO.MouseClicked[button] = false;
		ImGui::SetKeyOwner(ImGui::MouseButtonToKey(button), Owner(), ImGuiInputFlags_LockUntilRelease);
		state.swallowed |= 1 << button;
	};

	bool SwallowPresses(ImGuiContext& g, State& state)
	{
		bool pressed = false;

		for (int button = 0; button < ImGuiMouseButton_COUNT; button++)
			if (g.IO.MouseClicked[button])
			{
				Swallow(g, state, button);
				pressed = true;
			};

		return pressed;
	};

	// The owner's lock drops in the frame a button comes up, so that frame is hidden here.
	void HideReleases(ImGuiContext& g, State& state)
	{
		for (int button = 0; button < ImGuiMouseButton_COUNT; button++)
			if ((state.swallowed & (1 << button)) && !g.IO.MouseDown[button])
			{
				g.IO.MouseReleased[button] = false;
				state.swallowed &= ~(1 << button);
			};
	};

	void TryStart(ImGuiContext& g, State& state)
	{
		if (!g.IO.MouseClicked[ImGuiMouseButton_Middle] || Taken(g, state))
			return;

		const ImGuiWindow* target = FindTarget(g.HoveredWindow);

		if (!target)
			return;

		state.active = true;
		state.sticky = false;
		state.dragged = false;
		state.window = target->ID;
		state.origin = g.IO.MousePos;
		state.axes = ScrollAxes(*target);
		state.heading = ImVec2(0.0f, 0.0f);
		state.remainder = ImVec2(0.0f, 0.0f);

		Swallow(g, state, ImGuiMouseButton_Middle);
	};

	bool Ended(ImGuiContext& g, State& state, const ImGuiWindow* window)
	{
		if (!window || !window->WasActive || g.IO.AppFocusLost)
			return true;

		const bool pressed = SwallowPresses(g, state);

		if (ImGui::IsKeyPressed(ImGuiKey_Escape, ImGuiInputFlags_None, Owner()))
			return true;

		if (!state.sticky)
		{
			if (g.IO.MouseDown[ImGuiMouseButton_Middle])
				return false;

			state.sticky = !state.dragged;

			return state.dragged;
		};

		return pressed;
	};

	float Sign(float value)
	{
		return value < 0.0f ? -1.0f : 1.0f;
	};

	void Step(ImGuiContext& g, State& state, ImGuiWindow& window)
	{
		state.axes = ScrollAxes(window);
		state.heading = ImVec2(0.0f, 0.0f);

		const ImVec2 moved(g.IO.MousePos.x - state.origin.x, g.IO.MousePos.y - state.origin.y);

		if (ImLengthSqr(moved) > DEAD_ZONE * DEAD_ZONE)
			state.dragged = true;

		const ImVec2 offset(moved.x * state.axes.x, moved.y * state.axes.y);
		const float distance = ImSqrt(ImLengthSqr(offset));

		if (distance <= DEAD_ZONE)
			return;

		if (std::fabs(offset.x) > std::fabs(offset.y) * DIAGONAL_SLOPE)
			state.heading.x = Sign(offset.x);

		if (std::fabs(offset.y) > std::fabs(offset.x) * DIAGONAL_SLOPE)
			state.heading.y = Sign(offset.y);

		const float past = distance - DEAD_ZONE;
		const float speed = past * (SPEED_LINEAR + past * SPEED_SQUARED);
		const float share = speed * g.IO.DeltaTime / distance;

		state.remainder.x += offset.x * share;
		state.remainder.y += offset.y * share;

		const float wholeX = std::trunc(state.remainder.x);
		const float wholeY = std::trunc(state.remainder.y);

		state.remainder.x -= wholeX;
		state.remainder.y -= wholeY;

		if (wholeX != 0.0f)
			ImGui::SetScrollX(&window, window.Scroll.x + wholeX);

		if (wholeY != 0.0f)
			ImGui::SetScrollY(&window, window.Scroll.y + wholeY);
	};

	void OnNewFrame(ImGuiContext* context, ImGuiContextHook* hook)
	{
		ImGuiContext& g = *context;
		State& state = *(State*)hook->UserData;

		HideReleases(g, state);

		if (!state.active)
		{
			TryStart(g, state);
			return;
		};

		ImGuiWindow* window = ImGui::FindWindowByID(state.window);

		if (Ended(g, state, window) || !Scrolls(ScrollAxes(*window)))
		{
			state.active = false;
			return;
		};

		// Owned ahead of the frame it is pressed in, or the keyboard's navigation hears it first.
		ImGui::SetKeyOwner(ImGuiKey_Escape, Owner(), ImGuiInputFlags_LockUntilRelease);

		// The pointer is steering, so what it passes over does not light up.
		g.HoveredWindow = nullptr;
		g.HoveredWindowUnderMovingWindow = nullptr;

		if (ImGui::IsMousePosValid())
			Step(g, state, *window);
	};

	#pragma endregion

	#pragma region Drawing

	void DrawArrow(ImDrawList& list, const ImVec2& center, const ImVec2& direction, float scale)
	{
		const ImVec2 side(-direction.y * ARROW_HALF_WIDTH * scale, direction.x * ARROW_HALF_WIDTH * scale);
		const ImVec2 base(center.x + direction.x * ARROW_GAP * scale, center.y + direction.y * ARROW_GAP * scale);
		const ImVec2 tip(base.x + direction.x * ARROW_LENGTH * scale, base.y + direction.y * ARROW_LENGTH * scale);
		const ImVec2 left(base.x + side.x, base.y + side.y);
		const ImVec2 right(base.x - side.x, base.y - side.y);

		list.AddTriangleFilled(left, right, tip, FILL);
		list.AddTriangle(left, right, tip, OUTLINE, OUTLINE_WIDTH);
	};

	void DrawDot(ImDrawList& list, const ImVec2& center, float scale)
	{
		list.AddCircleFilled(center, DOT_RADIUS * scale, FILL);
		list.AddCircle(center, DOT_RADIUS * scale, OUTLINE, 0, OUTLINE_WIDTH);
	};

	void DrawAxes(ImDrawList& list, const ImVec2& center, const ImVec2& axes, float scale)
	{
		if (axes.x != 0.0f)
		{
			DrawArrow(list, center, ImVec2(1.0f, 0.0f), scale);
			DrawArrow(list, center, ImVec2(-1.0f, 0.0f), scale);
		};

		if (axes.y != 0.0f)
		{
			DrawArrow(list, center, ImVec2(0.0f, 1.0f), scale);
			DrawArrow(list, center, ImVec2(0.0f, -1.0f), scale);
		};

		DrawDot(list, center, scale);
	};

	void DrawCursor(ImDrawList& list, const ImVec2& center, const State& state, float scale)
	{
		if (state.heading.x == 0.0f && state.heading.y == 0.0f)
		{
			DrawAxes(list, center, state.axes, scale);
			return;
		};

		const float length = ImSqrt(ImLengthSqr(state.heading));

		DrawArrow(list, center, ImVec2(state.heading.x / length, state.heading.y / length), scale * HEADING_SCALE);
		DrawDot(list, center, scale);
	};

	void OnEndFrame(ImGuiContext* context, ImGuiContextHook* hook)
	{
		ImGuiContext& g = *context;
		const State& state = *(State*)hook->UserData;

		if (!state.active)
			return;

		const ImGuiWindow* window = ImGui::FindWindowByID(state.window);
		const float scale = ImGui::GetFontSize() / BASE_FONT_SIZE;

		ImDrawList& marker = *ImGui::GetForegroundDrawList(window->Viewport);

		marker.AddCircleFilled(state.origin, MARKER_RADIUS * scale, MARKER_FILL);
		marker.AddCircle(state.origin, MARKER_RADIUS * scale, OUTLINE, 0, OUTLINE_WIDTH);
		DrawAxes(marker, state.origin, state.axes, scale);

		ImGui::SetMouseCursor(ImGuiMouseCursor_None);
		DrawCursor(*ImGui::GetForegroundDrawList(g.MouseViewport), g.IO.MousePos, state, scale);
	};

	void OnShutdown(ImGuiContext*, ImGuiContextHook* hook)
	{
		delete (State*)hook->UserData;
	};

	#pragma endregion
};

namespace ImGuiAutoscroll
{
	void Install()
	{
		ImGuiContext* context = ImGui::GetCurrentContext();
		ImGuiContextHook hook;

		hook.UserData = new State();

		hook.Type = ImGuiContextHookType_NewFramePost;
		hook.Callback = OnNewFrame;
		ImGui::AddContextHook(context, &hook);

		hook.Type = ImGuiContextHookType_EndFramePre;
		hook.Callback = OnEndFrame;
		ImGui::AddContextHook(context, &hook);

		hook.Type = ImGuiContextHookType_Shutdown;
		hook.Callback = OnShutdown;
		ImGui::AddContextHook(context, &hook);
	};

	void KeepMiddleMouse()
	{
		ImGuiContext* context = ImGui::GetCurrentContext();

		if (State* state = Find(context))
			state->keptFrame = context->FrameCount;
	};

	bool IsActive()
	{
		const State* state = Find(ImGui::GetCurrentContext());

		return state && state->active;
	};

	bool IsSticky()
	{
		return IsActive() && Find(ImGui::GetCurrentContext())->sticky;
	};

	ImVec2 Heading()
	{
		return IsActive() ? Find(ImGui::GetCurrentContext())->heading : ImVec2(0.0f, 0.0f);
	};
};
