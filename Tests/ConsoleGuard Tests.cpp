#include "doctest.h"

#include "ConsoleGuard.h"
#include "Testing/Load.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <objbase.h>

using Loom::Testing::load_slack;

extern std::filesystem::path test_executable;

#pragma region Fake Terminal Windows

namespace
{
	constexpr const wchar_t* console_class = L"ConsoleWindowClass";
	constexpr const wchar_t* pseudo_class = L"PseudoConsoleWindow";
	constexpr const wchar_t* terminal_class = L"CASCADIA_HOSTING_WINDOW_CLASS";
	constexpr size_t class_capacity = 64;
	constexpr auto wait_limit = std::chrono::seconds(10) * load_slack;
	constexpr auto wait_step = std::chrono::milliseconds(5);
	constexpr auto helper_step = std::chrono::milliseconds(10);
	constexpr int helper_failed = 2;
	constexpr int helper_not_owned = 3;
	constexpr int window_size = 100;

	struct Windows final
	{
		HWND found = nullptr;
		size_t count = 0;
		const wchar_t* named = nullptr;
	};

	bool HasClass(HWND window, const wchar_t* expected)
	{
		wchar_t name[class_capacity]{ };

		return GetClassNameW(window, name, static_cast<int>(class_capacity)) > 0 && std::wstring_view(name) == expected;
	};

	Windows Find(const wchar_t* named)
	{
		Windows windows;
		windows.named = named;

		EnumWindows([](HWND window, LPARAM parameter) -> BOOL
		{
			Windows& windows = *reinterpret_cast<Windows*>(parameter);

			if (HasClass(window, windows.named))
			{
				windows.found = window;
				windows.count++;
			};

			return TRUE;
		}, reinterpret_cast<LPARAM>(&windows));

		return windows;
	};

	HWND Make(const wchar_t* named, DWORD style)
	{
		const HINSTANCE instance = GetModuleHandleW(nullptr);

		WNDCLASSW description{ };
		description.lpfnWndProc = DefWindowProcW;
		description.hInstance = instance;
		description.lpszClassName = named;
		RegisterClassW(&description);

		return CreateWindowExW(0, named, L"", style, 0, 0, window_size, window_size, nullptr, nullptr, instance, nullptr);
	};

	void Pump()
	{
		MSG message;

		while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&message);
			DispatchMessageW(&message);
		};
	};

	bool Wait(const std::function<bool()>& done)
	{
		const auto deadline = std::chrono::steady_clock::now() + wait_limit;

		while (std::chrono::steady_clock::now() < deadline)
		{
			Pump();

			if (done())
				return true;

			std::this_thread::sleep_for(helper_step);
		};

		return false;
	};

	void Linger()
	{
		MSG message;

		while (GetMessageW(&message, nullptr, 0, 0) > 0)
		{
			TranslateMessage(&message);
			DispatchMessageW(&message);
		};
	};
};

int FakeConsoleWindows(int count, char** arguments)
{
	const std::string_view kind = count > 2 ? arguments[2] : "";

	if (kind == "terminal" && count > 3)
	{
		const size_t pseudos = static_cast<size_t>(std::atoi(arguments[3]));
		const HWND terminal = Make(terminal_class, WS_OVERLAPPEDWINDOW);

		if (!terminal)
			return helper_failed;

		const auto owned = [terminal, pseudos]()
		{
			size_t each = 0;

			for (HWND window = nullptr; (window = FindWindowExW(nullptr, window, pseudo_class, nullptr)) != nullptr; )
				if (GetWindow(window, GW_OWNER) == terminal)
					each++;

			return each >= pseudos;
		};

		if (!Wait(owned))
			return helper_failed;

		ShowWindow(terminal, SW_SHOWNOACTIVATE);
		Linger();

		return 0;
	};

	if (kind == "idle")
	{
		Linger();

		return 0;
	};

	if (kind == "terminal-now")
	{
		const HWND terminal = Make(terminal_class, WS_OVERLAPPEDWINDOW);

		if (!terminal)
			return helper_failed;

		ShowWindow(terminal, SW_SHOWNOACTIVATE);
		Linger();

		return 0;
	};

	if (kind == "pseudo")
	{
		if (!Wait([]() { return Find(terminal_class).found != nullptr; }))
			return helper_failed;

		const HWND terminal = Find(terminal_class).found;
		const HWND pseudo = Make(pseudo_class, WS_POPUP);

		if (!pseudo)
			return helper_failed;

		SetWindowLongPtrW(pseudo, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(terminal));

		if (GetWindow(pseudo, GW_OWNER) != terminal)
			return helper_not_owned;

		ShowWindow(pseudo, SW_SHOWNOACTIVATE);
		Linger();

		return 0;
	};

	return helper_failed;
};

#pragma endregion

#pragma region Helpers

namespace
{
	constexpr auto settle = std::chrono::milliseconds(500);
	constexpr auto repeats = 50;
	constexpr DWORD all_access = GENERIC_ALL;
	constexpr wchar_t stays_open[] = L"conhost.exe cmd.exe /c ping -n 20 127.0.0.1 >nul";
	constexpr size_t pseudo_windows = 2;
	constexpr auto hide_bound = std::chrono::milliseconds(1000 * load_slack);
	constexpr auto hold_time = std::chrono::milliseconds(3500);
	constexpr auto poll_step = std::chrono::milliseconds(10);

	std::wstring UniqueName()
	{
		GUID guid{ };
		CoCreateGuid(&guid);

		wchar_t text[64]{ };
		StringFromGUID2(guid, text, static_cast<int>(std::size(text)));

		return std::wstring(L"CorrinConsoleGuardTest") + text;
	};

	struct Scene final
	{
		Scene()
		{
			name = UniqueName();
			desktop = CreateDesktopW(name.c_str(), nullptr, nullptr, 0, all_access, nullptr);
			job = CreateJobObjectW(nullptr, nullptr);
			other = CreateJobObjectW(nullptr, nullptr);
		};

		~Scene()
		{
			for (const HANDLE each : { job, other })
				if (each)
				{
					TerminateJobObject(each, 1);
					CloseHandle(each);
				};

			if (desktop)
				CloseDesktop(desktop);
		};

		Scene(const Scene&) = delete;
		Scene& operator=(const Scene&) = delete;

		bool Ready() const
		{
			return desktop && job && other;
		};

		bool Launch(const std::wstring& line, DWORD flags, bool inJob) const
		{
			std::wstring command = line;
			std::wstring desktopName = name;

			STARTUPINFOW startup{ };
			startup.cb = sizeof(startup);
			startup.lpDesktop = desktopName.data();

			PROCESS_INFORMATION information{ };

			if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, flags | CREATE_SUSPENDED, nullptr, nullptr, &startup, &information))
				return false;

			const bool ok = AssignProcessToJobObject(inJob ? job : other, information.hProcess) != FALSE;

			if (ok)
				ResumeThread(information.hThread);
			else
				TerminateProcess(information.hProcess, 1);

			CloseHandle(information.hThread);
			CloseHandle(information.hProcess);

			return ok;
		};

		bool LaunchHelper(const wchar_t* arguments, bool inJob) const
		{
			return Launch(L"\"" + test_executable.wstring() + L"\" --fake-console " + arguments, DETACHED_PROCESS, inJob);
		};

		std::vector<HWND> Windows(const wchar_t* only) const
		{
			struct State final
			{
				const wchar_t* only;
				std::vector<HWND> windows;
			} state{ only, { } };

			EnumDesktopWindows(desktop, [](HWND window, LPARAM parameter) -> BOOL
			{
				State& found = *reinterpret_cast<State*>(parameter);

				const bool wanted = found.only != nullptr
					? HasClass(window, found.only)
					: HasClass(window, console_class) || HasClass(window, pseudo_class) || HasClass(window, terminal_class);

				if (wanted)
					found.windows.push_back(window);

				return TRUE;
			}, reinterpret_cast<LPARAM>(&state));

			return state.windows;
		};

		HWND Terminal() const
		{
			const std::vector<HWND> windows = Windows(terminal_class);

			return windows.empty() ? nullptr : windows.front();
		};

		bool TerminalVisible() const
		{
			const HWND terminal = Terminal();

			return terminal && IsWindowVisible(terminal);
		};

		bool InJob(HWND window) const
		{
			DWORD process = 0;
			GetWindowThreadProcessId(window, &process);

			const HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process);

			if (!handle)
				return false;

			BOOL in = FALSE;
			IsProcessInJob(handle, job, &in);
			CloseHandle(handle);

			return in != FALSE;
		};

		bool Has(bool inJob, bool visibleOnly) const
		{
			for (const HWND window : Windows(nullptr))
				if (InJob(window) == inJob && (IsWindowVisible(window) || !visibleOnly))
					return true;

			return false;
		};

		bool Owned(size_t pseudos) const
		{
			return Terminal() && Windows(pseudo_class).size() == pseudos;
		};

		std::wstring name;
		HDESK desktop = nullptr;
		HANDLE job = nullptr;
		HANDLE other = nullptr;
	};

	bool Observe(const Scene& scene, bool& seen, const std::function<bool()>& done)
	{
		const auto deadline = std::chrono::steady_clock::now() + wait_limit;

		while (std::chrono::steady_clock::now() < deadline)
		{
			seen = seen || scene.TerminalVisible();

			if (done())
				return true;

			std::this_thread::sleep_for(poll_step);
		};

		return false;
	};

	void Watch(const Scene& scene, bool& seen, std::chrono::milliseconds length)
	{
		const auto end = std::chrono::steady_clock::now() + length;

		while (std::chrono::steady_clock::now() < end)
		{
			seen = seen || scene.TerminalVisible();
			std::this_thread::sleep_for(poll_step);
		};
	};

	bool Eventually(const std::function<bool()>& done, std::chrono::milliseconds limit = wait_limit)
	{
		const auto deadline = std::chrono::steady_clock::now() + limit;

		while (std::chrono::steady_clock::now() < deadline)
		{
			if (done())
				return true;

			std::this_thread::sleep_for(wait_step);
		};

		return false;
	};
};

#pragma endregion

TEST_CASE("Without a guard a job process that opens a console shows a console window")
{
	Scene scene;
	REQUIRE(scene.Ready());
	REQUIRE(scene.Launch(stays_open, DETACHED_PROCESS, true));

	CHECK(Eventually([&scene]() { return scene.Has(true, true); }));
};

TEST_CASE("A ConsoleGuard hides the console window a job process opens")
{
	Scene scene;
	REQUIRE(scene.Ready());

	const Loom::Process::ConsoleGuard guard(scene.job, scene.desktop);

	REQUIRE(scene.Launch(stays_open, DETACHED_PROCESS, true));
	REQUIRE(Eventually([&scene]() { return scene.Has(true, false); }));
	CHECK(Eventually([&scene]() { return !scene.Has(true, true); }));

	std::this_thread::sleep_for(settle);

	CHECK_FALSE(scene.Has(true, true));
	CHECK(scene.Has(true, false));
};

TEST_CASE("A ConsoleGuard leaves the console window of a process outside the job visible")
{
	Scene scene;
	REQUIRE(scene.Ready());

	const Loom::Process::ConsoleGuard guard(scene.job, scene.desktop);

	REQUIRE(scene.Launch(stays_open, DETACHED_PROCESS, false));
	REQUIRE(Eventually([&scene]() { return scene.Has(false, true); }));

	std::this_thread::sleep_for(settle);

	CHECK(scene.Has(false, true));
};

TEST_CASE("A ConsoleGuard is created and destroyed many times without hanging")
{
	Scene scene;
	REQUIRE(scene.Ready());

	for (int each = 0; each < repeats; each++)
		const Loom::Process::ConsoleGuard guard(scene.job, scene.desktop);

	CHECK(scene.Ready());
};

TEST_CASE("Without a guard a Terminal window of job processes is shown")
{
	Scene scene;
	REQUIRE(scene.Ready());
	REQUIRE(scene.LaunchHelper(L"terminal 2", true));
	REQUIRE(scene.LaunchHelper(L"pseudo", true));
	REQUIRE(scene.LaunchHelper(L"pseudo", true));
	REQUIRE(Eventually([&scene]() { return scene.Owned(pseudo_windows); }));

	CHECK(Eventually([&scene]() { return scene.TerminalVisible(); }));
};

TEST_CASE("A ConsoleGuard hides a Terminal window whose pseudo consoles are all in the job")
{
	Scene scene;
	REQUIRE(scene.Ready());

	const Loom::Process::ConsoleGuard guard(scene.job, scene.desktop);

	REQUIRE(scene.LaunchHelper(L"terminal 2", true));
	REQUIRE(scene.LaunchHelper(L"pseudo", true));
	REQUIRE(scene.LaunchHelper(L"pseudo", true));
	REQUIRE(Eventually([&scene]() { return scene.Owned(pseudo_windows); }));

	std::this_thread::sleep_for(settle);

	CHECK_FALSE(scene.TerminalVisible());
};

TEST_CASE("A ConsoleGuard leaves a Terminal window visible when a pseudo console is outside the job")
{
	Scene scene;
	REQUIRE(scene.Ready());

	const Loom::Process::ConsoleGuard guard(scene.job, scene.desktop);

	REQUIRE(scene.LaunchHelper(L"terminal 2", true));
	REQUIRE(scene.LaunchHelper(L"pseudo", true));
	REQUIRE(scene.LaunchHelper(L"pseudo", false));
	REQUIRE(Eventually([&scene]() { return scene.Owned(pseudo_windows); }));
	REQUIRE(Eventually([&scene]() { return scene.TerminalVisible(); }));

	std::this_thread::sleep_for(settle);

	CHECK(scene.TerminalVisible());
};

TEST_CASE("A ConsoleGuard keeps a Terminal window outside the job hidden until its pseudo console in the job appears")
{
	Scene scene;
	REQUIRE(scene.Ready());

	const Loom::Process::ConsoleGuard guard(scene.job, scene.desktop);

	bool seen = false;

	REQUIRE(scene.LaunchHelper(L"pseudo", true));
	REQUIRE(scene.LaunchHelper(L"terminal-now -Embedding", false));
	REQUIRE(Observe(scene, seen, [&scene]() { return scene.Owned(1); }));
	CHECK(Eventually([&scene]() { return !scene.TerminalVisible(); }, hide_bound));

	seen = false;
	Watch(scene, seen, settle);

	CHECK_FALSE(seen);
};

TEST_CASE("A ConsoleGuard shows a held Terminal window when its pseudo console is outside the job")
{
	Scene scene;
	REQUIRE(scene.Ready());

	const Loom::Process::ConsoleGuard guard(scene.job, scene.desktop);

	REQUIRE(scene.LaunchHelper(L"idle", true));
	REQUIRE(scene.LaunchHelper(L"pseudo", false));
	REQUIRE(scene.LaunchHelper(L"terminal-now -Embedding", false));
	REQUIRE(Eventually([&scene]() { return scene.Owned(1); }));

	std::this_thread::sleep_for(settle);

	REQUIRE(Eventually([&scene]() { return scene.TerminalVisible(); }));

	std::this_thread::sleep_for(settle);

	CHECK(scene.TerminalVisible());
};

TEST_CASE("A ConsoleGuard shows a held Terminal window again when no pseudo console appears")
{
	Scene scene;
	REQUIRE(scene.Ready());

	const Loom::Process::ConsoleGuard guard(scene.job, scene.desktop);

	REQUIRE(scene.LaunchHelper(L"idle", true));
	REQUIRE(scene.LaunchHelper(L"terminal-now -Embedding", false));
	REQUIRE(Eventually([&scene]() { return scene.Terminal() != nullptr; }));

	std::this_thread::sleep_for(hold_time);

	REQUIRE(Eventually([&scene]() { return scene.TerminalVisible(); }));

	std::this_thread::sleep_for(settle);

	CHECK(scene.TerminalVisible());
};

TEST_CASE("A ConsoleGuard shows a Terminal window normally when no job process started recently")
{
	Scene scene;
	REQUIRE(scene.Ready());

	const Loom::Process::ConsoleGuard guard(scene.job, scene.desktop);

	REQUIRE(scene.LaunchHelper(L"terminal-now -Embedding", false));
	REQUIRE(Eventually([&scene]() { return scene.TerminalVisible(); }));

	std::this_thread::sleep_for(settle);

	CHECK(scene.TerminalVisible());
};

TEST_CASE("A ConsoleGuard never holds a Terminal window that was not started for a handoff")
{
	Scene scene;
	REQUIRE(scene.Ready());

	const Loom::Process::ConsoleGuard guard(scene.job, scene.desktop);

	REQUIRE(scene.LaunchHelper(L"idle", true));
	REQUIRE(scene.LaunchHelper(L"terminal-now", false));
	REQUIRE(Eventually([&scene]() { return scene.TerminalVisible(); }));

	bool hidden = false;
	const auto end = std::chrono::steady_clock::now() + hold_time;

	while (std::chrono::steady_clock::now() < end)
	{
		hidden = hidden || !scene.TerminalVisible();
		std::this_thread::sleep_for(poll_step);
	};

	CHECK_FALSE(hidden);
};
