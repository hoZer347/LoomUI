#include "Headless.h"


namespace Loom::Testing
{
	namespace
	{
		constexpr ImVec2 no_position = { -1, -1 };
	};

	Headless::Headless()
		: Headless(Options{ })
	{
	};

	Headless::Headless(const Options& options)
		: m_context(ImGui::CreateContext())
		, m_display(options.display)
	{
		ImGui::SetCurrentContext(m_context);

		ImGui::SetCurrentContext(m_context);
		ImGuiIO& io = ImGui::GetIO();
		io.BackendFlags |= options.backend;
		io.ConfigFlags |= options.config;
		io.IniFilename = nullptr;
		io.DisplaySize = options.display;
		io.DeltaTime = options.frameSeconds;

		ImGuiPlatformIO& platform = ImGui::GetPlatformIO();
		platform.Platform_ClipboardUserData = &m_clipboard;
		platform.Platform_SetClipboardTextFn = [](ImGuiContext* context, const char* text)
		{
			*(std::string*)context->PlatformIO.Platform_ClipboardUserData = text;
		};
		platform.Platform_GetClipboardTextFn = [](ImGuiContext* context)
		{
			return ((std::string*)context->PlatformIO.Platform_ClipboardUserData)->c_str();
		};

		if (!options.buildFonts)
			return;

		unsigned char* pixels = nullptr;
		int width = 0;
		int height = 0;
		io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
	};

	Headless::~Headless()
	{
		ImGui::DestroyContext(m_context);
	};

	ImGuiContext* Headless::Context() const
	{
		return m_context;
	};

	std::string& Headless::Clipboard() const
	{
		return m_clipboard;
	};

	ImVec2 Headless::Display() const
	{
		return m_display;
	};

	void Headless::Frame(const Draw& draw) const
	{
		ImGui::SetCurrentContext(m_context);
		ImGui::NewFrame();
		draw();
		ImGui::EndFrame();
	};

	void Headless::Window(
		const char* const name,
		const Draw& draw) const
	{
		Frame([this, name, &draw]()
		{
			ImGui::SetNextWindowPos({ 0, 0 });
			ImGui::SetNextWindowSize(m_display);
			ImGui::Begin(name, nullptr, ImGuiWindowFlags_NoTitleBar);
			draw();
			ImGui::End();
		});
	};

	ImGuiID Headless::Id(
		const char* const window,
		const char* const label) const
	{
		return ImGui::FindWindowByName(window)->GetID(label);
	};

	bool Headless::Hover(
		const ImRect& area,
		const ImGuiID id,
		const float step,
		const Draw& frame) const
	{
		return Find(area, id, step, frame).x >= 0;
	};

	ImVec2 Headless::Find(
		const ImRect& area,
		const ImGuiID id,
		const float step,
		const Draw& frame) const
	{
		ImGui::SetCurrentContext(m_context);
		ImGuiIO& io = ImGui::GetIO();

		for (float y = area.Min.y; y < area.Max.y; y += step)
			for (float x = area.Min.x; x < area.Max.x; x += step)
			{
				io.AddMousePosEvent(x, y);
				frame();

				if (m_context->HoveredId == id)
					return { x, y };
			};

		return no_position;
	};

	bool Headless::Click(
		const ImRect& area,
		const ImGuiID id,
		const float step,
		const Draw& frame) const
	{
		if (!Hover(area, id, step, frame))
			return false;

		Press(ImGuiMouseButton_Left, frame);

		return true;
	};

	void Headless::Press(
		const int button,
		const Draw& frame) const
	{
		ImGui::SetCurrentContext(m_context);
		ImGuiIO& io = ImGui::GetIO();
		io.AddMouseButtonEvent(button, true);
		frame();
		io.AddMouseButtonEvent(button, false);
		frame();
	};

	void Headless::Type(
		const std::string& letters,
		const Draw& frame) const
	{
		ImGui::SetCurrentContext(m_context);

		for (const char letter : letters)
		{
			ImGui::GetIO().AddInputCharacter((unsigned int)letter);
			frame();
		};
	};

	void Headless::Enter(const Draw& frame) const
	{
		ImGui::SetCurrentContext(m_context);
		ImGuiIO& io = ImGui::GetIO();
		io.AddKeyEvent(ImGuiKey_Enter, true);
		frame();
		io.AddKeyEvent(ImGuiKey_Enter, false);
		frame();
		frame();
	};

	void Headless::Leave(
		const float step,
		const Draw& frame) const
	{
		ImGui::SetCurrentContext(m_context);
		ImGui::GetIO().AddMousePosEvent(m_display.x - step, step);
		Press(ImGuiMouseButton_Left, frame);
	};
};
