#pragma once

#include "imgui.h"

#include <filesystem>
#include <string>
#include <vector>


namespace Loom
{
	namespace EditorTheme
	{
		#pragma region Fonts

		// The font the editor starts in, and the sizes the Settings window offers.
		constexpr const char* DefaultFont = "Roboto-Medium.ttf";
		constexpr float DefaultFontSize = 15.0f;
		constexpr float MinFontSize = 8.0f;
		constexpr float MaxFontSize = 32.0f;

		// Where the fonts are loaded from. Until a consumer names one, the
		// Fonts folder beside the executable.
		void SetFontFolder(const std::filesystem::path& folder);
		std::filesystem::path FontFolder();

		// The fonts in FontFolder, by file name.
		std::vector<std::string> Fonts();

		// What draws EditorIcons, merged into the UI font in this order: Windows'
		// own, and elsewhere Symbola from fonts-symbola then GNU Unifont from
		// fonts-unifont for the emoji newer than Symbola, since colour emoji
		// fonts have no outlines to draw.
		std::vector<std::filesystem::path> EmojiFonts();

		// Makes the named font the atlas's first, so the one ImGui draws with,
		// and the code font the only other. ImGui's built-in ProggyClean stands
		// in for a missing file. Not between NewFrame and Render, when the atlas
		// is locked.
		void LoadFont(const std::string& file, float size);

		// Monospaced, at the size LoadFont was given.
		ImFont* CodeFont();

		#pragma endregion

		#pragma region Style

		// Replaces the engine's stock ImGui sizes and colours with the editor's.
		void ApplyStyle();

		// Text colours for the panels, picked to read on the theme's backgrounds.
		extern const ImVec4 AccentText;
		extern const ImVec4 ErrorText;
		extern const ImVec4 WarningText;
		extern const ImVec4 SuccessText;

		#pragma endregion
	};
};
