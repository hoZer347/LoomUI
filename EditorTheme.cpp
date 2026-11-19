#include "EditorTheme.h"

#include "Images.h"
#include "imgui_internal.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <utility>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#endif


namespace Loom
{
	namespace
	{
		#pragma region Colours

		constexpr float ChannelMax = 255.0f;
		constexpr unsigned int ChannelMask = 0xFF;
		constexpr int RedShift = 16;
		constexpr int GreenShift = 8;

		constexpr ImVec4 Rgb(unsigned int hex, float alpha = 1.0f)
		{
			return ImVec4(
				((hex >> RedShift) & ChannelMask) / ChannelMax,
				((hex >> GreenShift) & ChannelMask) / ChannelMax,
				(hex & ChannelMask) / ChannelMax,
				alpha);
		};

		constexpr ImVec4 WithAlpha(ImVec4 colour, float alpha)
		{
			return ImVec4(colour.x, colour.y, colour.z, alpha);
		};

		// Neutral greys from the deepest well up to the most raised control.
		constexpr ImVec4 Well = Rgb(0x141517);
		constexpr ImVec4 Panel = Rgb(0x1B1C1F);
		constexpr ImVec4 Chrome = Rgb(0x232428);
		constexpr ImVec4 Control = Rgb(0x2B2D31);
		constexpr ImVec4 ControlHovered = Rgb(0x36383D);
		constexpr ImVec4 ControlActive = Rgb(0x43464C);
		constexpr ImVec4 Edge = Rgb(0x313338);

		constexpr ImVec4 Text = Rgb(0xDCDEE3);
		constexpr ImVec4 TextDim = Rgb(0x7A7E87);

		constexpr ImVec4 Accent = Rgb(0x4C8DF6);
		constexpr ImVec4 AccentHovered = Rgb(0x6AA1F8);
		constexpr ImVec4 AccentActive = Rgb(0x3A73D6);
		constexpr ImVec4 AccentLight = Rgb(0x9DC1FF);
		constexpr ImVec4 Error = Rgb(0xF0625D);
		constexpr ImVec4 Warning = Rgb(0xE5C07B);
		constexpr ImVec4 Success = Rgb(0x8BCF8E);

		constexpr float SelectionAlpha = 0.35f;
		constexpr float SelectionActiveAlpha = 0.5f;
		constexpr float DimAlpha = 0.6f;
		constexpr float GripAlpha = 0.25f;

		constexpr ImVec4 Clear = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
		constexpr ImVec4 Dimmer = ImVec4(0.0f, 0.0f, 0.0f, DimAlpha);

		#pragma endregion

		#pragma region Sizes

		constexpr float ControlRounding = 4.0f;
		constexpr float GrabRounding = 3.0f;
		constexpr float ScrollbarRounding = 6.0f;
		constexpr float ScrollbarSize = 12.0f;
		constexpr float GrabMinSize = 10.0f;
		constexpr float IndentSpacing = 14.0f;
		constexpr float DockingSeparatorSize = 2.0f;
		constexpr ImVec2 WindowPadding = ImVec2(8.0f, 8.0f);
		constexpr ImVec2 FramePadding = ImVec2(6.0f, 4.0f);
		constexpr ImVec2 ItemSpacing = ImVec2(8.0f, 5.0f);
		constexpr ImVec2 ItemInnerSpacing = ImVec2(5.0f, 4.0f);
		constexpr ImVec2 CellPadding = ImVec2(6.0f, 3.0f);

		#pragma endregion

		#pragma region Fonts

		// What the consumer last gave SetFontFolder; empty until then.
		std::filesystem::path fontFolder;

		// Where Loom UI's fonts ship, next to the executable.
		constexpr const char* FontFolderName = "Fonts";

		std::filesystem::path ExecutableFolder()
		{
			std::error_code code;
#ifdef _WIN32
			// The longest path Windows allows, in characters.
			constexpr DWORD path_capacity = 32768;

			std::wstring buffer(path_capacity, L'\0');
			buffer.resize(GetModuleFileNameW(nullptr, buffer.data(), path_capacity));

			if (!buffer.empty())
				return std::filesystem::path(buffer).parent_path();
#else
			const std::filesystem::path executable = std::filesystem::read_symlink("/proc/self/exe", code);

			if (!executable.empty())
				return executable.parent_path();
#endif

			return std::filesystem::current_path(code);
		};

		// Monospaced, for the shader editor.
		constexpr const char* CodeFontFile = "Cousine-Regular.ttf";

		ImFont* codeFont = nullptr;

		// ImGui's monospaced ProggyClean stands in for a missing file.
		ImFont* AddFont(const std::string& file, float size)
		{
			ImFontAtlas& atlas = *ImGui::GetIO().Fonts;
			const std::filesystem::path path = EditorTheme::FontFolder() / file;
			std::error_code code;

			if (std::filesystem::exists(path, code))
				return atlas.AddFontFromFileTTF(path.string().c_str(), size);

			std::cerr << "EditorTheme: no font at " << path.string() << std::endl;

			return atlas.AddFontDefault();
		};

		// Without FreeType ImGui has no colour glyphs. Where two fonts have a glyph, the first draws it.
		void AddEmoji(float size)
		{
			for (const std::filesystem::path& path : EditorTheme::EmojiFonts())
			{
				std::error_code code;

				if (!std::filesystem::exists(path, code))
				{
					std::cerr << "EditorTheme: no emoji font at " << path.string() << std::endl;
					continue;
				};

				ImFontConfig config;
				config.MergeMode = true;

				ImGui::GetIO().Fonts->AddFontFromFileTTF(path.string().c_str(), size, &config);
			};
		};

		#pragma endregion

		#pragma region Colour icons

		constexpr const char* IconLoaderName = "Loom colour icon";
		constexpr int IconChannels = 4;
		constexpr float IconCentred = 0.5f;
		constexpr float IconHalves = 2.0f;
		constexpr float HeatMiddle = 0.5f;
		constexpr char32_t Utf8LeadByte = 0xF0;
		constexpr char32_t Utf8TrailByte = 0x80;
		constexpr char32_t Utf8TrailMask = 0x3F;
		constexpr int Utf8TrailBits = 6;
		constexpr int Utf8TrailCount = 3;

		EditorTheme::ColourIconSource colourIcons;

		bool IconContains(
			ImFontAtlas*,
			ImFontConfig*,
			const ImWchar codepoint)
		{
			return codepoint >= (ImWchar)EditorTheme::ColourIconFirst
				&& codepoint < (ImWchar)EditorTheme::ColourIconFirst + (ImWchar)colourIcons.count
				&& colourIcons.held((int)(codepoint - (ImWchar)EditorTheme::ColourIconFirst));
		};

		bool LoadIcon(
			ImFontAtlas* const atlas,
			ImFontConfig* const source,
			ImFontBaked* const baked,
			void*,
			const ImWchar codepoint,
			ImFontGlyph* const glyph,
			float* const advance)
		{
			if (!IconContains(atlas, source, codepoint))
				return false;

			const EditorTheme::ColourIcon* const made = colourIcons.icon((int)(codepoint - (ImWchar)EditorTheme::ColourIconFirst));

			if (made == nullptr)
				return false;

			const EditorTheme::ColourIcon& icon = *made;
			const int scale = EditorTheme::ColourIconMagnification(baked->Size, EditorTheme::DefaultFontSize);
			const float width = (float)(icon.width * scale);

			if (advance != nullptr)
			{
				*advance = width;
				return true;
			};

			const EditorTheme::ColourIcon scaled = EditorTheme::ColourIconScaled(icon, scale);
			const ImFontAtlasRectId pack = ImFontAtlasPackAddRect(atlas, scaled.width, scaled.height);

			if (pack == ImFontAtlasRectId_Invalid)
				return false;

			const float top = EditorTheme::ColourIconTop(baked->Size, (float)scaled.height);

			glyph->Codepoint = codepoint;
			glyph->AdvanceX = width;
			glyph->X0 = 0.0f;
			glyph->Y0 = top;
			glyph->X1 = width;
			glyph->Y1 = top + (float)scaled.height;
			glyph->Visible = true;
			glyph->Colored = true;
			glyph->PackId = pack;

			ImFontAtlasBakedSetFontGlyphBitmap(
				atlas,
				baked,
				source,
				glyph,
				ImFontAtlasPackGetRect(atlas, pack),
				scaled.rgba.data(),
				ImTextureFormat_RGBA32,
				scaled.width * IconChannels);

			return true;
		};

		const ImFontLoader& IconLoader()
		{
			static const ImFontLoader loader = []
			{
				ImFontLoader made;
				made.Name = IconLoaderName;
				made.FontSrcContainsGlyph = IconContains;
				made.FontBakedLoadGlyph = LoadIcon;

				return made;
			}();

			return loader;
		};

		#pragma endregion
	};

	#pragma region Fonts

	void EditorTheme::SetFontFolder(const std::filesystem::path& folder)
	{
		fontFolder = folder;
	};

	std::filesystem::path EditorTheme::FontFolder()
	{
		if (!fontFolder.empty())
			return fontFolder;

		return ExecutableFolder() / FontFolderName;
	};

	std::vector<std::string> EditorTheme::Fonts()
	{
		std::vector<std::string> fonts;
		std::error_code code;

		for (const auto& entry : std::filesystem::directory_iterator(FontFolder(), code))
			if (entry.path().extension() == ".ttf")
				fonts.push_back(entry.path().filename().string());

		return fonts;
	};

	std::vector<std::filesystem::path> EditorTheme::EmojiFonts()
	{
		std::vector<std::filesystem::path> paths;

#ifdef _WIN32
		constexpr const char* emojiFont = "seguiemj.ttf";

		PWSTR fonts = nullptr;

		if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Fonts, 0, nullptr, &fonts)))
			paths.push_back(std::filesystem::path(fonts) / emojiFont);

		CoTaskMemFree(fonts);
#else
		constexpr const char* symbolaFont = "/usr/share/fonts/truetype/ancient-scripts/Symbola_hint.ttf";
		constexpr const char* unifontFont = "/usr/share/fonts/opentype/unifont/unifont_upper.otf";

		paths = { symbolaFont, unifontFont };
#endif

		return paths;
	};

	void EditorTheme::LoadFont(const std::string& file, float size)
	{
		ImFontAtlas& atlas = *ImGui::GetIO().Fonts;
		atlas.ClearFonts();

		AddFont(file, size);
		AddEmoji(size);
		codeFont = AddFont(CodeFontFile, size);
	};

	ImFont* EditorTheme::CodeFont()
	{
		return codeFont;
	};

	#pragma endregion

	#pragma region Colour icons

	void EditorTheme::SetColourIcons(std::vector<ColourIcon> icons)
	{
		const auto kept = std::make_shared<const std::vector<ColourIcon>>(std::move(icons));

		SetColourIconSource({
			(int)kept->size(),
			[](const int) { return true; },
			[kept](const int index) { return &(*kept)[(size_t)index]; } });
	};

	void EditorTheme::SetColourIconSource(ColourIconSource source)
	{
		colourIcons = std::move(source);
	};

	std::string EditorTheme::ColourIconText(const int index)
	{
		const char32_t codepoint = ColourIconFirst + (char32_t)(std::max)(index, 0);
		std::string text(1, (char)(Utf8LeadByte | (codepoint >> (Utf8TrailBits * Utf8TrailCount))));

		for (int trail = Utf8TrailCount - 1; trail >= 0; --trail)
			text += (char)(Utf8TrailByte | ((codepoint >> (Utf8TrailBits * trail)) & Utf8TrailMask));

		return text;
	};

	bool EditorTheme::AddColourIcons(ImFont& font)
	{
		if (colourIcons.count <= 0)
			return false;

		ImFontConfig config;
		config.MergeMode = true;
		config.DstFont = &font;
		config.FontLoader = &IconLoader();

		return ImGui::GetIO().Fonts->AddFont(&config) != nullptr;
	};

	bool EditorTheme::HasColourIcons(const ImFont& font)
	{
		for (const ImFontConfig* const source : font.Sources)
			if (source->FontLoader == &IconLoader())
				return true;

		return false;
	};

	void EditorTheme::RemoveColourIcons(ImFont& font)
	{
		ImFontAtlas* const atlas = font.OwnerAtlas;

		if (atlas == nullptr)
			return;

		ImFontAtlasFontDestroyOutput(atlas, &font);

		for (int index = 0; index < atlas->Sources.Size; ++index)
		{
			ImFontConfig& source = atlas->Sources[index];

			if (source.DstFont != &font || source.FontLoader != &IconLoader())
				continue;

			ImFontAtlasFontDestroySourceData(atlas, &source);
			atlas->Sources.erase(&source);
			--index;
		};

		ImFontAtlasBuildUpdatePointers(atlas);
		ImFontAtlasFontInitOutput(atlas, &font);
	};

	int EditorTheme::ColourIconMagnification(
		const float fontSize,
		const float defaultFontSize)
	{
		return (std::max)(1, (int)(fontSize / defaultFontSize));
	};

	float EditorTheme::ColourIconTop(
		const float fontSize,
		const float glyphHeight)
	{
		return std::floor((fontSize - glyphHeight) * IconCentred);
	};

	float EditorTheme::ColourIconRowHeight(
		const int tallest,
		const int magnification)
	{
		return (float)(tallest * magnification) + ColourIconRowGap * IconHalves;
	};

	float EditorTheme::ColourIconCentre(
		const int outer,
		const int inner,
		const int magnification)
	{
		return std::floor((float)((outer - inner) * magnification) * IconCentred);
	};

	float EditorTheme::ColourIconTabPadding(
		const float fontSize,
		const int tallest,
		const int magnification,
		const float minimum)
	{
		const float top = ColourIconTop(fontSize, (float)(tallest * magnification));

		return (std::max)(minimum, ColourIconGap - top);
	};

	float EditorTheme::ColourIconMenuSpacing(
		const float fontSize,
		const int tallest,
		const int magnification,
		const float minimum)
	{
		const float top = ColourIconTop(fontSize, (float)(tallest * magnification));
		const float selectable = std::ceil(-top + ColourIconRowGap) * IconHalves;

		return (std::max)({ minimum, ColourIconRowHeight(tallest, magnification) - fontSize, selectable });
	};

	EditorTheme::ColourIcon EditorTheme::ColourIconScaled(
		const ColourIcon& icon,
		const int scale)
	{
		Images::Pixels enlarged = Images::Enlarged({ icon.width, icon.height, icon.rgba }, scale);

		return { enlarged.width, enlarged.height, std::move(enlarged.rgba) };
	};

	#pragma endregion

	#pragma region Style

	const ImVec4 EditorTheme::AccentText = AccentLight;
	const ImVec4 EditorTheme::ErrorText = Error;
	const ImVec4 EditorTheme::WarningText = Warning;
	const ImVec4 EditorTheme::SuccessText = Success;

	void EditorTheme::ApplyStyle()
	{
		ImGuiStyle& style = ImGui::GetStyle();

		style.WindowPadding = WindowPadding;
		style.FramePadding = FramePadding;
		style.ItemSpacing = ItemSpacing;
		style.ItemInnerSpacing = ItemInnerSpacing;
		style.CellPadding = CellPadding;
		style.IndentSpacing = IndentSpacing;
		style.ScrollbarSize = ScrollbarSize;
		style.GrabMinSize = GrabMinSize;
		style.DockingSeparatorSize = DockingSeparatorSize;

		// Square panels, since nearly all are docked edge to edge.
		style.WindowRounding = 0.0f;
		style.ChildRounding = ControlRounding;
		style.FrameRounding = ControlRounding;
		style.PopupRounding = ControlRounding;
		style.TabRounding = ControlRounding;
		style.GrabRounding = GrabRounding;
		style.ScrollbarRounding = ScrollbarRounding;

		style.WindowBorderSize = 1.0f;
		style.ChildBorderSize = 1.0f;
		style.PopupBorderSize = 1.0f;
		style.FrameBorderSize = 0.0f;
		style.TabBorderSize = 0.0f;
		style.TabBarBorderSize = 1.0f;

		// Docked panels already show their tabs, so the tab-list menu button is noise.
		style.WindowMenuButtonPosition = ImGuiDir_None;

		ImVec4* colours = style.Colors;

		colours[ImGuiCol_Text] = Text;
		colours[ImGuiCol_TextDisabled] = TextDim;
		colours[ImGuiCol_TextLink] = Accent;
		colours[ImGuiCol_TextSelectedBg] = WithAlpha(Accent, SelectionAlpha);

		colours[ImGuiCol_WindowBg] = Panel;
		colours[ImGuiCol_ChildBg] = Clear;
		colours[ImGuiCol_PopupBg] = Chrome;
		colours[ImGuiCol_Border] = Edge;
		colours[ImGuiCol_BorderShadow] = Clear;

		colours[ImGuiCol_FrameBg] = Control;
		colours[ImGuiCol_FrameBgHovered] = ControlHovered;
		colours[ImGuiCol_FrameBgActive] = ControlActive;

		colours[ImGuiCol_TitleBg] = Chrome;
		colours[ImGuiCol_TitleBgActive] = Chrome;
		colours[ImGuiCol_TitleBgCollapsed] = Chrome;
		colours[ImGuiCol_MenuBarBg] = Chrome;

		colours[ImGuiCol_ScrollbarBg] = Clear;
		colours[ImGuiCol_ScrollbarGrab] = ControlHovered;
		colours[ImGuiCol_ScrollbarGrabHovered] = ControlActive;
		colours[ImGuiCol_ScrollbarGrabActive] = TextDim;

		colours[ImGuiCol_CheckMark] = Accent;
		colours[ImGuiCol_SliderGrab] = Accent;
		colours[ImGuiCol_SliderGrabActive] = AccentHovered;

		colours[ImGuiCol_Button] = Control;
		colours[ImGuiCol_ButtonHovered] = ControlHovered;
		colours[ImGuiCol_ButtonActive] = ControlActive;

		colours[ImGuiCol_Header] = WithAlpha(Accent, SelectionAlpha);
		colours[ImGuiCol_HeaderHovered] = ControlHovered;
		colours[ImGuiCol_HeaderActive] = WithAlpha(Accent, SelectionActiveAlpha);

		colours[ImGuiCol_Separator] = Edge;
		colours[ImGuiCol_SeparatorHovered] = AccentHovered;
		colours[ImGuiCol_SeparatorActive] = Accent;

		colours[ImGuiCol_ResizeGrip] = WithAlpha(Accent, GripAlpha);
		colours[ImGuiCol_ResizeGripHovered] = AccentHovered;
		colours[ImGuiCol_ResizeGripActive] = Accent;

		colours[ImGuiCol_Tab] = Chrome;
		colours[ImGuiCol_TabHovered] = ControlHovered;
		colours[ImGuiCol_TabSelected] = Panel;
		colours[ImGuiCol_TabSelectedOverline] = Accent;
		colours[ImGuiCol_TabDimmed] = Chrome;
		colours[ImGuiCol_TabDimmedSelected] = Panel;
		colours[ImGuiCol_TabDimmedSelectedOverline] = Clear;

		colours[ImGuiCol_DockingPreview] = WithAlpha(Accent, SelectionActiveAlpha);
		colours[ImGuiCol_DockingEmptyBg] = Well;

		colours[ImGuiCol_PlotLines] = Accent;
		colours[ImGuiCol_PlotLinesHovered] = AccentHovered;
		colours[ImGuiCol_PlotHistogram] = Accent;
		colours[ImGuiCol_PlotHistogramHovered] = AccentHovered;

		colours[ImGuiCol_TableHeaderBg] = Chrome;
		colours[ImGuiCol_TableBorderStrong] = Edge;
		colours[ImGuiCol_TableBorderLight] = Control;
		colours[ImGuiCol_TableRowBg] = Clear;
		colours[ImGuiCol_TableRowBgAlt] = WithAlpha(Control, SelectionAlpha);

		colours[ImGuiCol_DragDropTarget] = Accent;
		colours[ImGuiCol_NavHighlight] = Accent;
		colours[ImGuiCol_NavWindowingHighlight] = Accent;
		colours[ImGuiCol_NavWindowingDimBg] = Dimmer;
		colours[ImGuiCol_ModalWindowDimBg] = Dimmer;
	};

	ImVec4 EditorTheme::Mix(
		const ImVec4& from,
		const ImVec4& to,
		const float share)
	{
		return {
			from.x + (to.x - from.x) * share,
			from.y + (to.y - from.y) * share,
			from.z + (to.z - from.z) * share,
			1 };
	};

	ImVec4 EditorTheme::HeatTone(
		const float heat,
		const ImVec4& cool,
		const ImVec4& warm,
		const ImVec4& hot)
	{
		return heat < HeatMiddle
			? Mix(cool, warm, heat / HeatMiddle)
			: Mix(warm, hot, (heat - HeatMiddle) / HeatMiddle);
	};

	#pragma endregion
};
