#pragma once

#include "Descendants.h"

#include <chrono>
#include <filesystem>
#include <mutex>
#include <process.h>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <TlHelp32.h>


namespace Loom::Testing
{
	/**
	* Loom::Testing cleanup
	* - KeepFolder notes a folder a test made, and RemoveKept removes every
	*   one noted, plus any folder under the temp folder named
	*   "<prefix>... Tests <this process id>", for a test main to call at exit
	* - EndChildren ends every process this one started, however deep, and
	*   spares one that only reused a dead parent's id
	*/
	namespace CleanupDetail
	{
		inline constexpr int remove_attempts = 5;
		inline constexpr auto remove_retry = std::chrono::milliseconds(100);
		inline constexpr UINT ended_code = 1;

		inline std::mutex& KeptLock()
		{
			static std::mutex lock;

			return lock;
		};

		inline std::vector<std::filesystem::path>& Kept()
		{
			static std::vector<std::filesystem::path> folders;

			return folders;
		};

		inline uint64_t Combine(const FILETIME& time)
		{
			return (uint64_t(time.dwHighDateTime) << 32) | time.dwLowDateTime;
		};

		inline uint64_t CreationTime(const HANDLE process)
		{
			FILETIME created{ };
			FILETIME ignored{ };

			return GetProcessTimes(process, &created, &ignored, &ignored, &ignored) ? Combine(created) : 0;
		};

		inline uint64_t CreationTime(const DWORD process)
		{
			const HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process);

			if (handle == nullptr)
				return 0;

			const uint64_t created = CreationTime(handle);
			CloseHandle(handle);

			return created;
		};

		inline std::vector<ProcessEntry> DescendantsOf(const DWORD root)
		{
			FILETIME now{ };
			GetSystemTimeAsFileTime(&now);
			const uint64_t snapshot_taken = Combine(now);

			const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

			if (snapshot == INVALID_HANDLE_VALUE)
				return { };

			std::vector<ProcessEntry> entries;
			PROCESSENTRY32W entry{ };
			entry.dwSize = sizeof(entry);

			for (BOOL more = Process32FirstW(snapshot, &entry); more; more = Process32NextW(snapshot, &entry))
				entries.push_back({ entry.th32ProcessID, entry.th32ParentProcessID, CreationTime(entry.th32ProcessID) });

			CloseHandle(snapshot);

			return Descendants(root, entries, snapshot_taken);
		};

		inline void RemoveFolder(const std::filesystem::path& folder)
		{
			std::error_code ignored;

			for (int attempt = 0; attempt < remove_attempts && std::filesystem::exists(folder, ignored); attempt++)
			{
				std::filesystem::remove_all(folder, ignored);

				if (std::filesystem::exists(folder, ignored))
					std::this_thread::sleep_for(remove_retry);
			};
		};
	};

	inline void KeepFolder(const std::filesystem::path& folder)
	{
		const std::lock_guard<std::mutex> guard(CleanupDetail::KeptLock());
		CleanupDetail::Kept().push_back(folder);
	};

	inline void RemoveKept(const std::string& prefix)
	{
		const std::string suffix = " Tests " + std::to_string(_getpid());

		std::vector<std::filesystem::path> folders;

		{
			const std::lock_guard<std::mutex> guard(CleanupDetail::KeptLock());
			folders = CleanupDetail::Kept();
		};

		std::error_code ignored;

		for (std::filesystem::directory_iterator each(std::filesystem::temp_directory_path(), ignored), end; !ignored && each != end; each.increment(ignored))
		{
			const std::string name = each->path().filename().string();

			if (name.starts_with(prefix) && name.ends_with(suffix))
				folders.push_back(each->path());
		};

		for (const std::filesystem::path& folder : folders)
			CleanupDetail::RemoveFolder(folder);
	};

	inline void EndChildren()
	{
		for (const ProcessEntry& process : CleanupDetail::DescendantsOf(GetCurrentProcessId()))
			if (const HANDLE handle = OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process.id))
			{
				if (CleanupDetail::CreationTime(handle) == process.created)
					TerminateProcess(handle, CleanupDetail::ended_code);

				CloseHandle(handle);
			};
	};
};
