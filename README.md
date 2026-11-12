# Loom UI

The ImGui-based UI shared by Loom Editor and standalone apps: the ImGui core with its GLFW and OpenGL3
backends, `EditorTheme`, `WindowFrame`, `ExtensionRegistry`, the `EditorIcons` glyphs and
`ImGuiAutoscroll`. A static library in namespace `Loom`.

It lives in the Loom repository at `Loom UI/` and is published to
https://github.com/hoZer347/LoomUI by `sync-loom-ui.ps1` (a `git subtree split`).

## Layout

- `Loom UI.vcxproj`: static library, x64 Debug and Release, output `Build/<Configuration>/Loom UI.lib`.
- `Loom UI.props`: import it from a consumer. Adds the include folders, defines `LOOM_UI_STATIC`
  and links `Loom UI.lib`, `glfw3.lib` and the Windows libraries.
- `ImGui/`: the ImGui sources, `imgui_stdlib` and `ImGuiAutoscroll`.
- `Fonts/`: the fonts `EditorTheme` loads.
- `External/glfw/`: GLFW headers and the vc2022 library.

## Building

    msbuild "Loom UI.vcxproj" /p:Configuration=Release /p:Platform=x64

## Using it

Import `Loom UI.props` and add a project reference to `Loom UI.vcxproj`.

- Fonts are looked for in `Fonts` next to the exe. `EditorTheme::SetFontFolder(path)` points elsewhere.
- `LOOM_UI_API` is `__declspec(dllexport)` by default, `dllimport` under `LOOM_SCRIPT_MODULE`, and
  empty when `LOOM_UI_STATIC` is defined or off Windows. The props define `LOOM_UI_STATIC`.
