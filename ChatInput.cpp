#include "ChatInput.h"

#include "imgui_internal.h"

#include <algorithm>
#include <cfloat>


namespace Loom::ChatInput
{
	namespace
	{
		constexpr float min_lines = 2;
		constexpr float max_lines = 12;

		TextLayout::Wrap WrapAt(float width)
		{
			return [font = ImGui::GetFont(), size = ImGui::GetFontSize(), width](std::string_view line)
			{
				const char* const begin = line.data();
				const char* const row = ImFontCalcWordWrapPositionEx(
					font, size, begin, begin + line.size(), width, ImDrawTextFlags_WrapKeepBlanks);

				return (size_t)(row - begin);
			};
		};

		struct Edit final
		{
			std::string& text;
			TextLayout::InputCaret caret;
			size_t first = 0;
			size_t last = 0;
		};

		Edit EditOf(
			std::string& text,
			ImGuiID id)
		{
			const ImGuiInputTextState* const state = ImGui::GetInputTextState(id);

			if (state == nullptr)
				return { text };

			return {
				text,
				{ (size_t)state->GetCursorPos(), state->LastMoveDirectionLR == ImGuiDir_Right },
				(size_t)std::min(state->GetSelectionStart(), state->GetSelectionEnd()),
				(size_t)std::max(state->GetSelectionStart(), state->GetSelectionEnd()) };
		};

		void MoveCaret(
			ImGuiInputTextCallbackData& data,
			const Edit& edit,
			TextLayout::RowStep step)
		{
			ImGuiInputTextState* const state = ImGui::GetInputTextState(data.ID);

			if (state == nullptr)
				return;

			const bool extend = ImGui::GetIO().KeyShift;
			const TextLayout::InputCaret from = extend || edit.first == edit.last
				? edit.caret
				: TextLayout::InputCaret{ step == TextLayout::RowStep::Up ? edit.first : edit.last };

			const TextLayout::InputCaret to = TextLayout::MoveRow(
				{ data.Buf, (size_t)data.BufTextLen },
				from,
				step,
				WrapAt(state->WrapWidth),
				FontMeasure(),
				state->GetPreferredOffsetX());

			data.CursorPos = (int)to.index;
			data.SelectionEnd = data.CursorPos;

			if (extend == false)
				data.SelectionStart = data.CursorPos;

			state->LastMoveDirectionLR = to.trailing ? ImGuiDir_Right : ImGuiDir_Left;
		};

		int OnInput(ImGuiInputTextCallbackData* data)
		{
			const Edit& edit = *(const Edit*)data->UserData;

			if (data->EventFlag == ImGuiInputTextFlags_CallbackResize)
			{
				edit.text.resize(data->BufTextLen);
				data->Buf = edit.text.data();

				return 0;
			};

			if (ImGui::GetIO().KeyCtrl)
				return 0;

			if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
				MoveCaret(*data, edit, TextLayout::RowStep::Up);
			else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
				MoveCaret(*data, edit, TextLayout::RowStep::Down);

			return 0;
		};
	};

	TextLayout::Measure FontMeasure()
	{
		return [font = ImGui::GetFont(), size = ImGui::GetFontSize()](std::string_view text)
		{
			return font->CalcTextSizeA(size, FLT_MAX, 0, text.data(), text.data() + text.size()).x;
		};
	};

	float Height(
		std::string_view text,
		float width)
	{
		const ImGuiStyle& style = ImGui::GetStyle();
		const float wrapWidth = std::max(1.0f, width - style.FramePadding.x - style.ScrollbarSize);
		const float rows = std::clamp((float)TextLayout::InputLines(text, WrapAt(wrapWidth)), min_lines, max_lines);

		return rows * ImGui::GetFontSize() + style.FramePadding.y * 2;
	};

	bool Draw(
		const char* id,
		std::string& text,
		ImVec2 size)
	{
		constexpr ImGuiInputTextFlags flags = ImGuiInputTextFlags_WordWrap |
			ImGuiInputTextFlags_EnterReturnsTrue |
			ImGuiInputTextFlags_CtrlEnterForNewLine |
			ImGuiInputTextFlags_CallbackResize |
			ImGuiInputTextFlags_CallbackAlways;

		Edit edit = EditOf(text, ImGui::GetID(id));

		return ImGui::InputTextMultiline(id, text.data(), text.capacity() + 1, size, flags, OnInput, &edit);
	};

	bool WantsCommands(std::string_view text)
	{
		return !text.empty() && text[0] == '/' && text.find_first_of(" \n") == std::string_view::npos;
	};

	bool DrawCommands(
		std::string& text,
		std::span<const Command> commands,
		size_t limit)
	{
		const std::string_view typed = std::string_view(text).substr(1);
		size_t shown = 0;

		for (const Command& command : commands)
		{
			if (!command.name.starts_with(typed))
				continue;

			if (shown++ == limit)
				break;

			const std::string name(command.name);

			if (ImGui::Selectable(name.c_str()))
			{
				text = "/" + name + " ";

				return true;
			};

			ImGui::SetItemTooltip("%s", std::string(command.description).c_str());
		};

		return false;
	};
};
