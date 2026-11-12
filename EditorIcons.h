#pragma once


namespace Loom
{
	/*
	* Every kind of asset, panel, document tab and toolbar control has an emoji, so it can be
	* told apart at a glance. EditorTheme merges Segoe UI Emoji into the UI
	* font for them. A texture shows itself where there is room for a picture,
	* and this only where there is not.
	*/
	namespace EditorIcons
	{
		constexpr const char* Folder = "📁";
		constexpr const char* Project = "🧵";
		constexpr const char* Scene = "🎬";
		constexpr const char* Prefab = "📦";
		constexpr const char* Shader = "✨";
		constexpr const char* PixelShader = "🖥";
		constexpr const char* ComputeShader = "🧮";
		constexpr const char* PreVertex = "🌊";
		constexpr const char* Texture = "🖼";
		constexpr const char* TwoD = "🎨";
		constexpr const char* ThreeD = "🗿";
		constexpr const char* Sprite = "👾";
		constexpr const char* Palette = "🌈";
		constexpr const char* Sound = "🔊";
		constexpr const char* Font = "🔤";
		constexpr const char* Model = "🧊";
		constexpr const char* Script = "🧩";
		constexpr const char* State = "🚦";
		constexpr const char* Dialogue = "🗨";
		constexpr const char* Test = "🧪";
		constexpr const char* Bindings = "⌨";
		constexpr const char* Build = "🔨";
		constexpr const char* File = "📄";
		constexpr const char* New = "➕";

		// The Game and Debug windows' play controls.
		constexpr const char* Play = "▶";
		constexpr const char* Pause = "⏸";
		constexpr const char* Stop = "⏹";
		constexpr const char* Step = "⏭";
	};
};
