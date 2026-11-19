#include "App.h"

#include "AppPacing.h"
#include "EditorTheme.h"
#include "ImGuiAutoscroll.h"
#include "Placement.h"
#include "TabWindows.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imgui_internal.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <GLFW/glfw3.h>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>


namespace Loom
{
	namespace
	{
		using Microsoft::WRL::ComPtr;

		constexpr const char* glsl_version = "#version 330";
		constexpr const char* screenshot_argument = "--screenshot";
		constexpr const char* placement_file = "window.txt";
		constexpr const char* layout_file = "imgui.ini";
		constexpr float background_veil = 0.7f;
		constexpr float opaque = 1.0f;
		constexpr ImGuiCol veiled_colours[] =
		{
			ImGuiCol_WindowBg,
			ImGuiCol_MenuBarBg,
			ImGuiCol_TitleBg,
			ImGuiCol_TitleBgActive,
			ImGuiCol_TitleBgCollapsed,
			ImGuiCol_DockingEmptyBg,
		};
		constexpr ImVec4 transparent{ };
		constexpr int opengl_major = 3;
		constexpr int opengl_minor = 3;
		constexpr int default_frames = 120;
		constexpr double woken_share = 0.9;
		constexpr int settle_passes = 3;
		constexpr int mouse_buttons[] = { GLFW_MOUSE_BUTTON_LEFT, GLFW_MOUSE_BUTTON_RIGHT, GLFW_MOUSE_BUTTON_MIDDLE };
		constexpr UINT channels = 4;
		constexpr int failure = 1;

		struct State final
		{
			App::Callbacks callbacks;
			GLFWwindow* window = nullptr;
			std::optional<WindowFrame> frame{ };
			std::optional<TabWindows> tabs{ };
			std::optional<AppSettings> settings{ };
			std::vector<std::string> arguments;
			std::vector<std::string> dropped;
			std::string project;
			std::filesystem::path placementFile;
			std::optional<Placement> placed{ };
			std::optional<Placement> lastPlacement{ };
			bool hidden = false;
			bool firstRun = false;
			bool migrated = false;
			bool polling = false;
			int frames = 0;
		};

		State app{ };
		void (*createViewportWindow)(ImGuiViewport*) = nullptr;

		void Call(const std::function<void()>& callback)
		{
			if (callback)
				callback();
		};

		bool Ask(const std::function<bool()>& callback)
		{
			return callback && callback();
		};

		void Padded(
			const bool tall,
			const std::function<void()>& submit)
		{
			if (app.callbacks.pad)
				app.callbacks.pad(tall, submit);
			else
				submit();
		};

		void OnDrop(
			GLFWwindow*,
			int count,
			const char** paths)
		{
			for (int index = 0; index < count; ++index)
				app.dropped.push_back(paths[index]);
		};

		void OnContentScale(
			GLFWwindow* window,
			float,
			float)
		{
			WindowFrame::SetIcon(glfwGetWin32Window(window));
		};

		void OnCreateViewportWindow(ImGuiViewport* viewport)
		{
			createViewportWindow(viewport);

			if ((viewport->Flags & ImGuiViewportFlags_NoTaskBarIcon) == 0)
				WindowFrame::SetIcon(viewport->PlatformHandleRaw);
		};

		std::vector<std::string> CommandLine()
		{
			int count = 0;
			wchar_t** const wide = CommandLineToArgvW(GetCommandLineW(), &count);
			std::vector<std::string> arguments;

			for (int index = 1; wide != nullptr && index < count; ++index)
				arguments.push_back(FolderText(std::filesystem::path(wide[index])));

			LocalFree(wide);

			return arguments;
		};

		bool MigrateLayoutFile(const std::filesystem::path& file)
		{
			if (!app.callbacks.migrateLayout)
				return false;

			std::ifstream in(file, std::ios::binary);

			if (!in)
				return false;

			std::ostringstream read;
			read << in.rdbuf();
			in.close();

			const std::string text = read.str();
			const std::string migrated = app.callbacks.migrateLayout(text);

			if (migrated == text)
				return false;

			std::ofstream out(file, std::ios::binary | std::ios::trunc);
			out << migrated;

			return true;
		};

		bool WritePng(
			const std::filesystem::path& file,
			int width,
			int height,
			const std::vector<uint8_t>& rgba)
		{
			ComPtr<IWICImagingFactory> factory;
			ComPtr<IWICBitmap> bitmap;
			ComPtr<IWICStream> stream;
			ComPtr<IWICBitmapEncoder> encoder;
			ComPtr<IWICBitmapFrameEncode> frame;

			return SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))
				&& SUCCEEDED(factory->CreateBitmapFromMemory((UINT)width, (UINT)height, GUID_WICPixelFormat32bppRGBA,
					(UINT)width * channels, (UINT)rgba.size(), (BYTE*)rgba.data(), &bitmap))
				&& SUCCEEDED(factory->CreateStream(&stream))
				&& SUCCEEDED(stream->InitializeFromFilename(file.c_str(), GENERIC_WRITE))
				&& SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder))
				&& SUCCEEDED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache))
				&& SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr))
				&& SUCCEEDED(frame->Initialize(nullptr))
				&& SUCCEEDED(frame->WriteSource(bitmap.Get(), nullptr))
				&& SUCCEEDED(frame->Commit())
				&& SUCCEEDED(encoder->Commit());
		};

		bool Capture(
			GLFWwindow* window,
			const std::filesystem::path& file)
		{
			int width = 0;
			int height = 0;
			glfwGetFramebufferSize(window, &width, &height);

			const size_t row = (size_t)width * channels;
			std::vector<uint8_t> bottomUp(row * (size_t)height);
			glReadBuffer(GL_BACK);
			glPixelStorei(GL_PACK_ALIGNMENT, 1);
			glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, bottomUp.data());

			std::vector<uint8_t> topDown(bottomUp.size());

			for (int line = 0; line < height; ++line)
				std::copy_n(bottomUp.data() + row * (size_t)(height - 1 - line), row, topDown.data() + row * (size_t)line);

			for (size_t alpha = 3; alpha < topDown.size(); alpha += channels)
				topDown[alpha] = 0xFF;

			return WritePng(file, width, height, topDown);
		};

		void SavePlacement()
		{
			if (app.placementFile.empty())
				return;

			const std::optional<Placement> current = Placement::Take(app.window, app.placed);
			const bool settled = current == app.lastPlacement;
			app.lastPlacement = current;

			if (!settled
				|| !current
				|| current == app.placed)
				return;

			app.placed = current;
			Placement::Write(app.placementFile, *current);
		};

		int PushVeil()
		{
			for (const ImGuiCol colour : veiled_colours)
			{
				ImVec4 veil = ImGui::GetStyle().Colors[colour];
				veil.w = background_veil;
				ImGui::PushStyleColor(colour, veil);
			};

			return (int)std::size(veiled_colours);
		};

		void DrawMenu()
		{
			const ImVec2 padding = ImGui::GetStyle().FramePadding;

			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(padding.x, padding.y * WindowFrame::BarPadding));

			if (!ImGui::BeginMainMenuBar())
			{
				ImGui::PopStyleVar();
				return;
			};

			Call(app.callbacks.menus);

			if (app.frame->DrawButtonRow(app.window))
				glfwSetWindowShouldClose(app.window, GLFW_TRUE);

			ImGui::EndMainMenuBar();
			ImGui::PopStyleVar();
		};

		void BuildFrame(const bool first)
		{
			ImGui_ImplOpenGL3_NewFrame();
			ImGui_ImplGlfw_NewFrame();
			Padded(true, [] { ImGui::NewFrame(); });

			const bool backdrop = Ask(app.callbacks.backdrop);
			const int veiled = backdrop ? PushVeil() : 0;

			DrawMenu();

			if (backdrop)
				ImGui::PushStyleColor(ImGuiCol_WindowBg, transparent);

			ImGuiID dockspace = 0;
			Padded(true, [&dockspace] { dockspace = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport()); });

			if (backdrop)
				ImGui::PopStyleColor();

			if (first && app.firstRun && app.frames == 0 && app.callbacks.layout)
				app.callbacks.layout(dockspace);

			app.tabs->Update(dockspace);

			ExtensionRegistry::DrawAll(app.project, app.dropped, app.callbacks.pad, app.callbacks.panelBegun, app.callbacks.panelsDrawing);
			ImGui::PopStyleColor(veiled);

			Call(app.callbacks.panelsDrawn);

			if (backdrop)
				Call(app.callbacks.drawBackdrop);

			ImGui::Render();

			app.tabs->UpdatePlatformWindows();
		};

		void RenderFrame(const bool draw = true)
		{
			BuildFrame(true);

			for (int pass = 0; pass < settle_passes && Ask(app.callbacks.unsettled); ++pass)
				BuildFrame(false);

			static_cast<void>(Ask(app.callbacks.unsettled));

			if (!draw)
				return;

			glfwMakeContextCurrent(app.window);

			int width = 0;
			int height = 0;
			glfwGetFramebufferSize(app.window, &width, &height);
			glViewport(0, 0, width, height);

			const ImVec4 clear = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
			glClearColor(clear.x, clear.y, clear.z, clear.w);
			glClear(GL_COLOR_BUFFER_BIT);
			ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

			++app.frames;
		};

		void Present()
		{
			if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
			{
				ImGui::RenderPlatformWindowsDefault();
				glfwMakeContextCurrent(app.window);
			};

			glfwSwapBuffers(app.window);
		};

		void RedrawWhileHeld()
		{
			if (!app.polling)
				return;

			app.polling = false;
			RenderFrame();
			Present();
			app.polling = true;
		};
	};

	int App::Run(
		const Options& options,
		const Callbacks& callbacks)
	{
		app.callbacks = callbacks;
		app.arguments = CommandLine();

		std::filesystem::path screenshot;
		int frames = default_frames;

		for (size_t index = 0; index + 1 < app.arguments.size(); ++index)
		{
			if (app.arguments[index] != screenshot_argument)
				continue;

			screenshot = FolderPath(app.arguments[index + 1]);

			if (index + 2 < app.arguments.size())
				if (const int parsed = std::atoi(app.arguments[index + 2].c_str()); parsed > 0)
					frames = parsed;
		};

		app.hidden = !screenshot.empty();

		CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

		if (!glfwInit())
			return failure;

		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, opengl_major);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, opengl_minor);
		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
		glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

		GLFWwindow* const window = glfwCreateWindow(options.width, options.height, options.title.c_str(), nullptr, nullptr);

		if (window == nullptr)
		{
			glfwTerminate();
			return failure;
		};

		glfwMakeContextCurrent(window);
		glfwSwapInterval(1);
		glfwSetDropCallback(window, OnDrop);
		glfwSetWindowContentScaleCallback(window, OnContentScale);
		WindowFrame::SetIcon(glfwGetWin32Window(window));

		app.window = window;
		app.frame.emplace();
		app.frame->Install(window, RedrawWhileHeld);

		app.settings.emplace(options.settingsFolder, options.previousSettingsFolder);
		const std::filesystem::path settings = app.settings->Folder();

		if (!app.hidden)
		{
			app.placementFile = settings / placement_file;
			app.placed = Placement::Read(app.placementFile);
			Placement::Show(window, app.placed);
			app.lastPlacement = app.placed;
		};

		const std::filesystem::path layout = settings / layout_file;
		const std::string layoutText = FolderText(layout);
		std::error_code code;
		app.firstRun = !std::filesystem::exists(layout, code);
		app.migrated = !app.firstRun && MigrateLayoutFile(layout);

		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiAutoscroll::Install();

		ImGuiIO& io = ImGui::GetIO();
		io.IniFilename = layoutText.c_str();
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

		if (!app.hidden)
		{
			io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
			io.ConfigViewportsNoAutoMerge = true;
		};

		EditorTheme::LoadFont(EditorTheme::DefaultFont, EditorTheme::DefaultFontSize);
		Call(app.callbacks.font);
		EditorTheme::ApplyStyle();

		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			ImGuiStyle& style = ImGui::GetStyle();
			style.WindowRounding = 0.0f;
			style.Colors[ImGuiCol_WindowBg].w = opaque;
		};

		ImGui_ImplGlfw_InitForOpenGL(window, true);
		createViewportWindow = ImGui::GetPlatformIO().Platform_CreateWindow;
		ImGui::GetPlatformIO().Platform_CreateWindow = OnCreateViewportWindow;
		ImGui_ImplOpenGL3_Init(glsl_version);

		{
			TabWindows::Hooks hooks = TabWindows::Glfw(RedrawWhileHeld);

			hooks.padded = [](const std::function<void()>& submit)
			{
				Padded(true, submit);
			};

			app.tabs.emplace(std::move(hooks));
			app.tabs->Install();
		};

		Call(app.callbacks.setup);

		bool captured = true;
		double lastInput = glfwGetTime();

		while (!glfwWindowShouldClose(window))
		{
			const bool minimised = glfwGetWindowAttrib(window, GLFW_ICONIFIED) == GLFW_TRUE;
			const bool held = std::ranges::any_of(
				mouse_buttons,
				[window](const int button) { return glfwGetMouseButton(window, button) == GLFW_PRESS; });
			const double chosen = FrameWait(Ask(app.callbacks.busy), held, glfwGetTime() - lastInput, minimised);
			const double wait = app.callbacks.wait ? app.callbacks.wait(chosen) : chosen;

			app.polling = true;
			const double before = glfwGetTime();
			glfwWaitEventsTimeout(wait);
			app.polling = false;

			if (glfwGetTime() - before < wait * woken_share)
				lastInput = glfwGetTime();

			RenderFrame(!minimised);

			Call(app.callbacks.frameDone);

			if (minimised)
				continue;

			if (app.hidden && app.frames >= frames)
			{
				captured = Capture(window, screenshot);
				break;
			};

			Present();
			SavePlacement();
			Call(app.callbacks.presented);
		};

		Call(app.callbacks.stop);

		ImGui_ImplOpenGL3_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();
		app.tabs.reset();

		app.frame.reset();
		glfwDestroyWindow(window);
		glfwTerminate();

		return captured ? 0 : failure;
	};

	int App::Frames() { return app.frames; };
	GLFWwindow* App::Window() { return app.window; };
	bool App::Hidden() { return app.hidden; };
	bool App::FirstRun() { return app.firstRun; };
	bool App::Migrated() { return app.migrated; };
	const std::vector<std::string>& App::Arguments() { return app.arguments; };
	const AppSettings& App::Settings() { return *app.settings; };
	void App::SetProject(const std::string& project) { app.project = project; };
	const std::string& App::Project() { return app.project; };
	void App::Wake() { glfwPostEmptyEvent(); };
};
