#pragma once

#include "TextLayout.h"

#include "imgui.h"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>


namespace Loom::ChatInput
{
	TextLayout::Measure FontMeasure();

	// The height the prompt box takes to show text at a width.
	float Height(
		std::string_view text,
		float width);

	// The prompt box: Enter sends, Ctrl+Enter breaks the line, and up and down
	// move the caret a row as MoveRow says, with Shift taking the selection along.
	// True when Enter was pressed.
	bool Draw(
		const char* id,
		std::string& text,
		ImVec2 size);

	struct Command
	{
		std::string_view name;
		std::string_view description;
	};

	// Whether text is a slash command still being typed: it starts with a slash and has no space or line break yet.
	bool WantsCommands(std::string_view text);

	// Lists up to limit of the commands whose names start with what follows the slash. True when one was chosen,
	// and then text is a slash, its name and a space.
	bool DrawCommands(
		std::string& text,
		std::span<const Command> commands,
		size_t limit);
};
