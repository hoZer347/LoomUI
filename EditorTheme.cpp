#include "EditorTheme.h"

#include <filesystem>
#include <iostream>

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

	#pragma endregion
};
