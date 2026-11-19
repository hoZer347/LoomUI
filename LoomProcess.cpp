#include "LoomProcess.h"

#include "Text.h"

#include <algorithm>
#include <array>
#include <system_error>
#include <unordered_map>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <winternl.h>


namespace Loom
{
	namespace
	{
		constexpr DWORD exit_wait_ms = 500;
		constexpr DWORD cancel_step_ms = 10;
		constexpr size_t frozen_threads = 1;
		constexpr size_t listing_size = 512 * 1024;
		constexpr ULONG system_process_information = 5;
		constexpr LONG length_mismatch = (LONG)0xC0000004;
		constexpr const wchar_t* ntdll_name = L"ntdll.dll";
		constexpr const char* query_name = "NtQuerySystemInformation";

		using QuerySystem = LONG (NTAPI*)(ULONG, PVOID, ULONG, PULONG);

		// Reads the threads of the wanted processes from one system listing. False if it cannot be read.
		bool ThreadCounts(const std::vector<DWORD>& wanted, std::unordered_map<DWORD, size_t>& counts)
		{
			const HMODULE library = GetModuleHandleW(ntdll_name);
			const QuerySystem query = library == nullptr ? nullptr : (QuerySystem)GetProcAddress(library, query_name);

			if (query == nullptr)
				return false;

			std::vector<BYTE> buffer(listing_size);

			while (true)
			{
				ULONG needed = 0;
				const LONG status = query(system_process_information, buffer.data(), (ULONG)buffer.size(), &needed);

				if (status == length_mismatch)
				{
					buffer.resize(std::max<size_t>(needed, buffer.size()) + listing_size);
					continue;
				};

				if (status < 0)
					return false;

				break;
			};

			const BYTE* at = buffer.data();

			while (true)
			{
				const SYSTEM_PROCESS_INFORMATION& entry = *(const SYSTEM_PROCESS_INFORMATION*)at;
				const DWORD id = (DWORD)(ULONG_PTR)entry.UniqueProcessId;

				if (std::ranges::find(wanted, id) != wanted.end())
					counts[id] = entry.NumberOfThreads;

				if (entry.NextEntryOffset == 0)
					break;

				at += entry.NextEntryOffset;
			};

			return true;
		};

		// Whether the process answers to this id and start time, before its threads are counted.
		bool Answers(DWORD id, int64_t startTime)
		{
			if (id == 0)
				return false;

			const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, id);

			if (process == nullptr)
				return false;

			DWORD code = 0;
			const bool same = Process::StartTime(process) == startTime && GetExitCodeProcess(process, &code) && code == STILL_ACTIVE;

			CloseHandle(process);

			return same;
		};

		bool CreateHidden(
			const std::filesystem::path& executable, std::wstring& line, const std::filesystem::path& folder, PROCESS_INFORMATION& process)
		{
			STARTUPINFOW startup{ };
			startup.cb = sizeof(startup);

			if (CreateProcessW(
				executable.c_str(), line.data(), nullptr, nullptr, FALSE,
				CREATE_NO_WINDOW | CREATE_BREAKAWAY_FROM_JOB, nullptr, folder.empty() ? nullptr : folder.c_str(), &startup, &process))
				return true;

			return GetLastError() == ERROR_ACCESS_DENIED && CreateProcessW(
				executable.c_str(), line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, folder.empty() ? nullptr : folder.c_str(), &startup, &process);
		};
	};

	std::string Process::SystemError(unsigned long code)
	{
		return std::system_category().message((int)code);
	};

	void Process::AppendQuoted(std::wstring& line, const std::wstring& argument)
	{
		line += L'"';

		size_t slashes = 0;

		for (const wchar_t letter : argument)
		{
			if (letter == L'\\')
			{
				slashes++;
				line += letter;
				continue;
			};

			if (letter == L'"')
				line.append(slashes + 1, L'\\');

			slashes = 0;
			line += letter;
		};

		line.append(slashes, L'\\');
		line += L'"';
	};

	void Process::EndThread(std::thread& thread)
	{
		if (!thread.joinable())
			return;

		while (WaitForSingleObject(thread.native_handle(), cancel_step_ms) == WAIT_TIMEOUT)
			CancelSynchronousIo(thread.native_handle());

		thread.join();
	};

	int64_t Process::StartTime(void* process)
	{
		FILETIME created{ };
		FILETIME exited{ };
		FILETIME kernel{ };
		FILETIME user{ };

		if (!GetProcessTimes(process, &created, &exited, &kernel, &user))
			return 0;

		return (int64_t)(((uint64_t)created.dwHighDateTime << 32) | created.dwLowDateTime);
	};

	bool Process::Alive(uint32_t id, int64_t startTime)
	{
		return AliveAll({ { id, startTime } }).front();
	};

	// A listing that cannot be read counts as more threads than a frozen process has, so a failure never doubles a task.
	std::vector<bool> Process::AliveAll(const std::vector<std::pair<uint32_t, int64_t>>& processes)
	{
		std::vector<bool> alive(processes.size(), false);
		std::vector<DWORD> running;

		for (size_t index = 0; index < processes.size(); index++)
			if (Answers(processes[index].first, processes[index].second))
			{
				alive[index] = true;
				running.push_back(processes[index].first);
			};

		std::unordered_map<DWORD, size_t> counts;

		if (running.empty() || !ThreadCounts(running, counts))
			return alive;

		for (size_t index = 0; index < processes.size(); index++)
			if (alive[index])
			{
				const auto found = counts.find(processes[index].first);

				alive[index] = found != counts.end() && found->second > frozen_threads;
			};

		return alive;
	};

	void Process::Terminate(uint32_t id, int64_t startTime)
	{
		if (id == 0)
			return;

		const HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, id);

		if (process == nullptr)
			return;

		if (StartTime(process) == startTime && TerminateProcess(process, 0))
			WaitForSingleObject(process, exit_wait_ms);

		CloseHandle(process);
	};

	bool Process::WriteAll(void* file, std::string_view line)
	{
		size_t sent = 0;

		while (sent < line.size())
		{
			DWORD written = 0;

			if (!WriteFile(file, line.data() + sent, (DWORD)(line.size() - sent), &written, nullptr))
				return false;

			sent += written;
		};

		return true;
	};

	bool Process::StartDetached(
		const std::filesystem::path& executable,
		const std::vector<std::wstring>& arguments,
		const std::filesystem::path& workingDirectory,
		void*& process,
		std::string& error)
	{
		std::wstring line;
		AppendQuoted(line, executable.wstring());

		for (const std::wstring& argument : arguments)
		{
			line += L' ';
			AppendQuoted(line, argument);
		};

		PROCESS_INFORMATION started{ };

		if (!CreateHidden(executable, line, workingDirectory, started))
		{
			error = SystemError(GetLastError());
			return false;
		};

		CloseHandle(started.hThread);
		process = started.hProcess;

		return true;
	};

	bool Process::Launch(
		const std::filesystem::path& executable,
		const std::vector<std::string>& arguments,
		const std::filesystem::path& workingDirectory,
		Launched& launched,
		std::string& error,
		const std::wstring& desktop)
	{
		SECURITY_ATTRIBUTES inherit{ sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };

		HANDLE childInput = nullptr;
		HANDLE childOutput = nullptr;
		HANDLE input = nullptr;
		HANDLE output = nullptr;

		const auto closePipes = [&childInput, &childOutput, &input, &output]()
		{
			for (const HANDLE handle : { childInput, childOutput, input, output })
				if (handle != nullptr)
					CloseHandle(handle);
		};

		if (!CreatePipe(&childInput, &input, &inherit, 0) || !CreatePipe(&output, &childOutput, &inherit, 0))
		{
			error = SystemError(GetLastError());
			closePipes();
			return false;
		};

		SetHandleInformation(input, HANDLE_FLAG_INHERIT, 0);
		SetHandleInformation(output, HANDLE_FLAG_INHERIT, 0);

		std::array<HANDLE, 2> inherited = { childInput, childOutput };

		HANDLE job = CreateJobObjectW(nullptr, nullptr);
		JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{ };
		limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;

		if (job == nullptr || !SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
		{
			error = SystemError(GetLastError());
			closePipes();

			if (job != nullptr)
				CloseHandle(job);

			return false;
		};

		constexpr DWORD attribute_count = 2;

		SIZE_T attributeSize = 0;
		InitializeProcThreadAttributeList(nullptr, attribute_count, 0, &attributeSize);
		std::vector<char> attributes(attributeSize);

		STARTUPINFOEXW startup{ };
		startup.StartupInfo.cb = sizeof(startup);
		startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
		startup.StartupInfo.hStdInput = childInput;
		startup.StartupInfo.hStdOutput = childOutput;
		startup.StartupInfo.hStdError = childOutput;
		startup.lpAttributeList = (LPPROC_THREAD_ATTRIBUTE_LIST)attributes.data();

		std::wstring desktopName = desktop;

		if (!desktopName.empty())
			startup.StartupInfo.lpDesktop = desktopName.data();

		InitializeProcThreadAttributeList(startup.lpAttributeList, attribute_count, 0, &attributeSize);
		UpdateProcThreadAttribute(
			startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
			inherited.data(), sizeof(HANDLE) * inherited.size(), nullptr, nullptr);
		UpdateProcThreadAttribute(
			startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_JOB_LIST, &job, sizeof(HANDLE), nullptr, nullptr);

		std::wstring line;
		AppendQuoted(line, executable.wstring());

		for (const std::string& argument : arguments)
		{
			line += L' ';
			AppendQuoted(line, Text::Wide(argument));
		};

		PROCESS_INFORMATION process{ };

		const BOOL started = CreateProcessW(
			executable.c_str(), line.data(), nullptr, nullptr, TRUE,
			CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT,
			nullptr, workingDirectory.empty() ? nullptr : workingDirectory.c_str(),
			&startup.StartupInfo, &process);

		const DWORD startError = GetLastError();
		DeleteProcThreadAttributeList(startup.lpAttributeList);

		if (!started)
		{
			error = SystemError(startError);
			closePipes();
			CloseHandle(job);
			return false;
		};

		CloseHandle(childInput);
		CloseHandle(childOutput);
		CloseHandle(process.hThread);

		launched = { process.hProcess, job, input, output };

		return true;
	};
};
