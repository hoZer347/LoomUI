#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>


namespace Loom
{
	/**
	* Loom::Process
	* - Starts a child with no window, its pipes and a job object that ends it
	*   with its owner, and tells whether a process by id and start time still
	*   does anything
	* - Handles are void* so this header stays free of Windows.h
	*/
	struct Process final
	{
		class ConsoleGuard;

		// A running child, its job and the ends of its pipes. The child dies with the job.
		struct Launched
		{
			void* process = nullptr;
			void* job = nullptr;
			void* input = nullptr;
			void* output = nullptr;
		};

		// Writes every byte of the line to the file, or returns false.
		static bool WriteAll(void* file, std::string_view line);

		// Starts the executable hidden in a job that kills it on close, with its input and output
		// (and error) on pipes. A desktop name puts it on that desktop. On failure error says why.
		static bool Launch(
			const std::filesystem::path& executable,
			const std::vector<std::string>& arguments,
			const std::filesystem::path& workingDirectory,
			Launched& launched,
			std::string& error,
			const std::wstring& desktop = {});

		// Starts the executable hidden and outside any job the caller runs in, if that job lets it.
		// process is the child's handle, which the caller closes. On failure error says why.
		static bool StartDetached(
			const std::filesystem::path& executable,
			const std::vector<std::wstring>& arguments,
			const std::filesystem::path& workingDirectory,
			void*& process,
			std::string& error);

		// How CommandLineToArgvW reads an argument back.
		static void AppendQuoted(std::wstring& line, const std::wstring& argument);

		static std::string SystemError(unsigned long code);

		// Ends a thread still in ReadFile or WriteFile, which comes back once cancelled, and joins it.
		static void EndThread(std::thread& thread);

		// When the process started, in file time ticks, or 0 if that cannot be read.
		static int64_t StartTime(void* process);

		// Whether the process with this id and start time is doing anything. One
		// down to its last thread is frozen, and can never answer or finish.
		static bool Alive(uint32_t id, int64_t startTime);

		// The same for many (id, start time) pairs, reading the system's threads once for all.
		static std::vector<bool> AliveAll(const std::vector<std::pair<uint32_t, int64_t>>& processes);

		// Ends the process with this id and start time, and waits briefly for it to go.
		static void Terminate(uint32_t id, int64_t startTime);
	};
};
