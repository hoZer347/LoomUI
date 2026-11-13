#pragma once

#include "imgui.h"

#include <cstdint>
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

		// The colour a share of the way from one to the other, opaque.
		ImVec4 Mix(
			const ImVec4& from,
			const ImVec4& to,
			float share);

		// Cool when 0 through warm at half to hot at 1, for a heat of 0 to 1.
		ImVec4 HeatTone(
			float heat,
			const ImVec4& cool,
			const ImVec4& warm,
			const ImVec4& hot);

		#pragma endregion

		#pragma region Colour icons

		// A picture an app draws in place of an emoji, as RGBA pixels, each icon a code point
		// of the private use plane from ColourIconFirst on, in the order given.
		struct ColourIcon final
		{
			int width = 0;
			int height = 0;
			std::vector<uint8_t> rgba;
		};

		constexpr char32_t ColourIconFirst = 0xF0000;

		// The pixels a gap above and below a glyph in a tab or a menu row, in screen pixels.
		constexpr float ColourIconGap = 3.0f;
		constexpr float ColourIconRowGap = 1.0f;

		// What AddColourIcons merges, replacing the last set. Not between NewFrame and Render.
		void SetColourIcons(std::vector<ColourIcon> icons);

		// The text that draws icon index.
		std::string ColourIconText(int index);

		// Merges the icons into font as coloured glyphs, each centred on the line. False when there are none.
		bool AddColourIcons(ImFont& font);

		// Whether AddColourIcons has run on the font and not been undone, as a font reload undoes it.
		bool HasColourIcons(const ImFont& font);

		// Takes the icons out of the font's atlas, leaving its other sources as they were. Removal discards every bake of the font.
		void RemoveColourIcons(ImFont& font);

		// How many times larger than its pixels an icon is drawn at a font size: whole multiples of the size the font is first built at.
		int ColourIconMagnification(
			float fontSize,
			float defaultFontSize);

		// Where the top of a glyph sits against the top of the text line, centring it on the line.
		float ColourIconTop(
			float fontSize,
			float glyphHeight);

		// How tall a row is that holds an icon tallest pixels high, with ColourIconRowGap above and below it.
		float ColourIconRowHeight(
			int tallest,
			int magnification);

		// How far in an icon of size inner starts to sit in the middle of a slot of size outer, both in icon pixels.
		float ColourIconCentre(
			int outer,
			int inner,
			int magnification);

		// The vertical frame padding that leaves ColourIconGap above and below an icon tallest pixels high, never less than minimum.
		float ColourIconTabPadding(
			float fontSize,
			int tallest,
			int magnification,
			float minimum);

		// The vertical gap between menu rows that leaves an icon tallest pixels high ColourIconRowGap above and below it inside its own row, never less than minimum.
		float ColourIconMenuSpacing(
			float fontSize,
			int tallest,
			int magnification,
			float minimum);

		// Each pixel repeated scale times across and down.
		ColourIcon ColourIconScaled(
			const ColourIcon& icon,
			int scale);

		#pragma endregion
	};
};
