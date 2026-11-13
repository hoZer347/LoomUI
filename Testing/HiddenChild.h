#pragma once

#include "LoomProcess.h"
#include "Load.h"

#include <windows.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

extern std::filesystem::path test_executable;

#pragma region Hidden child

namespace
{
	constexpr auto child_limit = std::chrono::seconds(30) * Loom::Testing::load_slack;
	constexpr UINT child_timed_out = 124;
	constexpr DWORD output_chunk = 256;

	// A desktop of its own, so a window the child opens is never on the user's screen.
	struct HiddenDesktop final
	{
		HiddenDesktop() :
			name(L"Corrin Code Loom Test Desktop " + std::to_wstring(GetCurrentProcessId())),
			desktop(CreateDesktopW(name.c_str(), nullptr, nullptr, 0, GENERIC_ALL, nullptr))
		{
		};

		~HiddenDesktop()
		{
			if (desktop != nullptr)
				CloseDesktop(desktop);
		};

		HiddenDesktop(const HiddenDesktop&) = delete;
		HiddenDesktop& operator=(const HiddenDesktop&) = delete;

		const std::wstring name;
		const HDESK desktop;
	};

	struct Finished final
	{
		DWORD exit = child_timed_out;
		std::string output;
	};

	// Runs this test executable with arguments on a hidden desktop, and gives its exit code and all it printed.
	Finished RunHidden(const std::vector<std::string>& arguments)
	{
		const HiddenDesktop desktop;
		Loom::Process::Launched launched;
		std::string error;
		Finished finished;

		if (desktop.desktop == nullptr
			|| !Loom::Process::Launch(test_executable, arguments, { }, launched, error, desktop.name))
		{
			finished.output = "launch failed: " + error;
			return finished;
		};

		const DWORD limit = (DWORD)std::chrono::duration_cast<std::chrono::milliseconds>(child_limit).count();

		if (WaitForSingleObject(launched.process, limit) != WAIT_OBJECT_0)
			TerminateProcess(launched.process, child_timed_out);

		WaitForSingleObject(launched.process, INFINITE);
		GetExitCodeProcess(launched.process, &finished.exit);

		char buffer[output_chunk];
		DWORD read = 0;

		while (ReadFile(launched.output, buffer, sizeof(buffer), &read, nullptr) && read > 0)
			finished.output.append(buffer, read);

		for (void* const handle : { launched.process, launched.job, launched.input, launched.output })
			if (handle != nullptr)
				CloseHandle(handle);

		return finished;
	};
}

#pragma endregion
