#include "ConsoleGuard.h"

#include <algorithm>
#include <cwchar>
#include <future>
#include <string_view>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <TlHelp32.h>
#include <winternl.h>


namespace Loom
{
	namespace
	{
		#pragma region Constants

		constexpr const wchar_t* console_class = L"ConsoleWindowClass";
		constexpr const wchar_t* pseudo_class = L"PseudoConsoleWindow";
		constexpr const wchar_t* terminal_class = L"CASCADIA_HOSTING_WINDOW_CLASS";
		constexpr size_t class_capacity = 64;

		constexpr DWORD no_thread = 0;
		constexpr UINT expire_step_ms = 250;
		constexpr ULONGLONG hold_limit_ms = 3000;
		constexpr ULONGLONG filetime_per_ms = 10000;
		constexpr ULONGLONG recent_spawn = hold_limit_ms * filetime_per_ms;
		constexpr size_t job_process_capacity = 256;
		constexpr int filetime_shift = 32;
		constexpr ULONG command_line_class = 60;
		constexpr const wchar_t* handoff_argument = L"-Embedding";
		constexpr const wchar_t* ntdll_name = L"ntdll.dll";
		constexpr const char* query_name = "NtQueryInformationProcess";
		constexpr size_t command_line_capacity = 4096;

		using QueryProcess = LONG(NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);

		thread_local Process::ConsoleGuard* t_watching = nullptr;

		#pragma endregion

		#pragma region Helpers

		bool HasClass(HWND window, const wchar_t* expected)
		{
			wchar_t name[class_capacity]{ };

			return GetClassNameW(window, name, (int)class_capacity) > 0 && std::wcscmp(name, expected) == 0;
		};

		ULONGLONG Ticks(const FILETIME& time)
		{
			return ((ULONGLONG)time.dwHighDateTime << filetime_shift) | time.dwLowDateTime;
		};

		bool StartedRecently(HANDLE process)
		{
			FILETIME created, exited, kernel, user, now;

			if (!GetProcessTimes(process, &created, &exited, &kernel, &user))
				return false;

			GetSystemTimeAsFileTime(&now);

			return Ticks(now) >= Ticks(created) && Ticks(now) - Ticks(created) <= recent_spawn;
		};

		bool HasArgument(HANDLE process, const wchar_t* argument)
		{
			const QueryProcess query = (QueryProcess)GetProcAddress(GetModuleHandleW(ntdll_name), query_name);

			if (query == nullptr)
				return false;

			std::vector<char> buffer(command_line_capacity);
			ULONG length = 0;

			if (query(process, command_line_class, buffer.data(), (ULONG)buffer.size(), &length) < 0)
				return false;

			const UNICODE_STRING& line = *(const UNICODE_STRING*)buffer.data();

			return std::wstring_view(line.Buffer, line.Length / sizeof(wchar_t)).find(argument) != std::wstring_view::npos;
		};

		DWORD ParentOf(DWORD process)
		{
			const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

			if (snapshot == INVALID_HANDLE_VALUE)
				return 0;

			PROCESSENTRY32W entry{ };
			entry.dwSize = sizeof(entry);
			DWORD parent = 0;

			for (BOOL more = Process32FirstW(snapshot, &entry); more && parent == 0; more = Process32NextW(snapshot, &entry))
				if (entry.th32ProcessID == process)
					parent = entry.th32ParentProcessID;

			CloseHandle(snapshot);

			return parent;
		};

		bool InJob(DWORD process, HANDLE job)
		{
			const HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process);

			if (handle == nullptr)
				return false;

			BOOL inside = FALSE;
			const BOOL asked = IsProcessInJob(handle, job, &inside);
			CloseHandle(handle);

			return asked && inside;
		};

		#pragma endregion
	};

	#pragma region ConsoleGuard

	Process::ConsoleGuard::ConsoleGuard(void* job, void* desktop)
		: m_job(job)
	{
		std::promise<DWORD> ready;
		std::future<DWORD> id = ready.get_future();

		m_watcher = std::thread(&Process::ConsoleGuard::Watch, this, desktop, std::move(ready));
		m_thread = id.get();
	};

	Process::ConsoleGuard::~ConsoleGuard()
	{
		if (m_thread != no_thread)
			PostThreadMessageW(m_thread, WM_QUIT, 0, 0);

		if (m_watcher.joinable())
			m_watcher.join();
	};

	void Process::ConsoleGuard::Watch(void* desktop, std::promise<unsigned long> announce)
	{
		if (desktop != nullptr && !SetThreadDesktop((HDESK)desktop))
		{
			announce.set_value(no_thread);

			return;
		};

		MSG message;
		PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE);

		t_watching = this;
		m_foreground = GetForegroundWindow();

		const HWINEVENTHOOK created = SetWinEventHook(
			EVENT_OBJECT_CREATE, EVENT_OBJECT_CREATE, nullptr, (WINEVENTPROC)&Process::ConsoleGuard::OnEvent, 0, 0,
			WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
		const HWINEVENTHOOK shown = SetWinEventHook(
			EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW, nullptr, (WINEVENTPROC)&Process::ConsoleGuard::OnEvent, 0, 0,
			WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
		const HWINEVENTHOOK focused = SetWinEventHook(
			EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, (WINEVENTPROC)&Process::ConsoleGuard::OnEvent, 0, 0,
			WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

		announce.set_value(created != nullptr && shown != nullptr && focused != nullptr ? GetCurrentThreadId() : no_thread);

		if (created == nullptr || shown == nullptr || focused == nullptr)
			return;

		const UINT_PTR timer = SetTimer(nullptr, 0, expire_step_ms, nullptr);

		EnumWindows(
			[](HWND window, LPARAM guard)
			{
				((ConsoleGuard*)guard)->Hide(window);

				return TRUE;
			},
			(LPARAM)this);

		while (GetMessageW(&message, nullptr, 0, 0) > 0)
		{
			if (message.message == WM_TIMER && message.hwnd == nullptr)
				Expire();

			TranslateMessage(&message);
			DispatchMessageW(&message);
		};

		KillTimer(nullptr, timer);
		UnhookWinEvent(created);
		UnhookWinEvent(shown);
		UnhookWinEvent(focused);
		t_watching = nullptr;
	};

	void Process::ConsoleGuard::Hide(void* window)
	{
		const HWND handle = (HWND)window;
		wchar_t name[class_capacity]{ };

		if (GetClassNameW(handle, name, (int)class_capacity) <= 0)
			return;

		if (std::wcscmp(name, terminal_class) == 0)
		{
			if (Holding(handle) || OwnedOnlyByJob(handle))
				ShowWindow(handle, SW_HIDE);
		}
		else if (std::wcscmp(name, pseudo_class) == 0)
		{
			const HWND terminal = GetAncestor(handle, GA_ROOTOWNER);

			if (terminal == handle || !HasClass(terminal, terminal_class))
				return;

			if (OwnedOnlyByJob(terminal))
			{
				Release(terminal, false);
				ShowWindow(terminal, SW_HIDE);
			}
			else
				Release(terminal, true);
		}
		else if (std::wcscmp(name, console_class) == 0
			&& IsWindowVisible(handle)
			&& GetAncestor(handle, GA_PARENT) == GetDesktopWindow()
			&& InJob(handle))
			ShowWindow(handle, SW_HIDE);
	};

	void Process::ConsoleGuard::Hold(void* window)
	{
		if (!HasClass((HWND)window, terminal_class) || Holding(window) || !Handoff(window) || !SpawnedRecently())
			return;

		ShowWindow((HWND)window, SW_HIDE);
		m_held.push_back({ window, GetTickCount64(), false });
	};

	void Process::ConsoleGuard::Expire()
	{
		const ULONGLONG now = GetTickCount64();

		for (size_t index = m_held.size(); index-- > 0;)
			if (now - m_held[index].since >= hold_limit_ms)
				Release(m_held[index].terminal, true);
	};

	void Process::ConsoleGuard::Release(void* terminal, bool show)
	{
		const auto found = std::find_if(
			m_held.begin(),
			m_held.end(),
			[terminal](const Held& held)
			{
				return held.terminal == terminal;
			});

		if (found == m_held.end())
			return;

		const bool focused = found->focused;
		m_held.erase(found);

		if (!show)
			return;

		ShowWindow((HWND)terminal, SW_SHOWNA);

		if (focused)
			SetForegroundWindow((HWND)terminal);
	};

	void Process::ConsoleGuard::Foreground(void* window)
	{
		if (!Holding(window))
		{
			m_foreground = window;

			return;
		};

		for (Held& held : m_held)
			if (held.terminal == window)
				held.focused = true;

		if (m_foreground != nullptr && IsWindow((HWND)m_foreground))
			SetForegroundWindow((HWND)m_foreground);
	};

	bool Process::ConsoleGuard::Holding(void* terminal) const
	{
		return std::any_of(
			m_held.begin(),
			m_held.end(),
			[terminal](const Held& held)
			{
				return held.terminal == terminal;
			});
	};

	bool Process::ConsoleGuard::Handoff(void* window) const
	{
		DWORD process = 0;
		GetWindowThreadProcessId((HWND)window, &process);
		const HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process);

		if (handle == nullptr)
			return false;

		const bool handed = StartedRecently(handle) && HasArgument(handle, handoff_argument);
		CloseHandle(handle);

		return handed;
	};

	bool Process::ConsoleGuard::InJob(void* window) const
	{
		DWORD process = 0;
		GetWindowThreadProcessId((HWND)window, &process);

		return Loom::InJob(process, (HANDLE)m_job) || Loom::InJob(ParentOf(process), (HANDLE)m_job);
	};

	bool Process::ConsoleGuard::SpawnedRecently() const
	{
		std::vector<char> buffer(sizeof(JOBOBJECT_BASIC_PROCESS_ID_LIST) + job_process_capacity * sizeof(ULONG_PTR));
		const JOBOBJECT_BASIC_PROCESS_ID_LIST& list = *(const JOBOBJECT_BASIC_PROCESS_ID_LIST*)buffer.data();

		QueryInformationJobObject((HANDLE)m_job, JobObjectBasicProcessIdList, buffer.data(), (DWORD)buffer.size(), nullptr);

		for (DWORD index = 0; index < list.NumberOfProcessIdsInList; index++)
		{
			const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)list.ProcessIdList[index]);

			if (process == nullptr)
				continue;

			const bool recent = StartedRecently(process);
			CloseHandle(process);

			if (recent)
				return true;
		};

		return false;
	};

	bool Process::ConsoleGuard::OwnedOnlyByJob(void* terminal) const
	{
		struct Search
		{
			const ConsoleGuard* guard;
			HWND terminal;
			int owned;
			int ours;
		} search{ this, (HWND)terminal, 0, 0 };

		EnumWindows(
			[](HWND window, LPARAM state)
			{
				Search& search = *(Search*)state;

				if (!HasClass(window, pseudo_class) || GetAncestor(window, GA_ROOTOWNER) != search.terminal)
					return TRUE;

				search.owned++;

				if (search.guard->InJob(window))
					search.ours++;

				return TRUE;
			},
			(LPARAM)&search);

		return search.owned > 0 && search.owned == search.ours;
	};

	void __stdcall Process::ConsoleGuard::OnEvent(
		void*,
		unsigned long event,
		void* window,
		long object,
		long child,
		unsigned long,
		unsigned long)
	{
		if (t_watching == nullptr || object != OBJID_WINDOW || child != CHILDID_SELF)
			return;

		if (event == EVENT_SYSTEM_FOREGROUND)
			t_watching->Foreground(window);
		else if (event == EVENT_OBJECT_CREATE)
			t_watching->Hold(window);
		else if (event == EVENT_OBJECT_SHOW)
			t_watching->Hide(window);
	};

	#pragma endregion
};
