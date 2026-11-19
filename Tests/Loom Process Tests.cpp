#include "doctest.h"

#include "Testing/Load.h"

#include "ConsoleGuard.h"
#include "LoomProcess.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <shellapi.h>

using Loom::Testing::load_slack;

using Loom::Process;

extern std::filesystem::path test_executable;

#pragma region Child

namespace
{
	constexpr auto child_hang = std::chrono::seconds(60);
	constexpr int child_failed = 2;
	constexpr int first_extra = 4;
	constexpr std::string_view to_output = "-";
	constexpr const wchar_t* console_command = L"conhost.exe cmd.exe /c ping -n 20 127.0.0.1 >nul";
	constexpr const wchar_t* hang_argument = L"--hang";

	void Tell(const std::string& target, const std::string& text)
	{
		if (target == to_output)
		{
			std::cout << text << std::endl;
			return;
		};

		std::ofstream(target, std::ios::app) << text << std::endl;
	};

	DWORD Start(std::wstring command, DWORD flags)
	{
		STARTUPINFOW startup{ };
		startup.cb = sizeof(startup);

		PROCESS_INFORMATION information{ };

		if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, flags, nullptr, nullptr, &startup, &information))
			return 0;

		CloseHandle(information.hThread);
		CloseHandle(information.hProcess);

		return information.dwProcessId;
	};

	std::wstring HangCommand()
	{
		std::wstring line;
		Process::AppendQuoted(line, test_executable.wstring());

		return line + L" " + hang_argument;
	};
};

// --process-child <kind> <target> [extra...], telling what it found to the file target, or to its output for -.
int ProcessChild(int count, char** arguments)
{
	if (count < first_extra)
		return child_failed;

	const std::string_view kind = arguments[2];
	const std::string target = arguments[3];

	if (kind == "arguments")
	{
		for (int argument = first_extra; argument < count; argument++)
			Tell(target, arguments[argument]);

		return 0;
	};

	if (kind == "folder")
	{
		Tell(target, std::filesystem::current_path().string());
		return 0;
	};

	if (kind == "error" && count > first_extra)
	{
		std::cerr << arguments[first_extra] << std::endl;
		return 0;
	};

	if (kind == "console")
	{
		const HWND window = GetConsoleWindow();

		Tell(target, window != nullptr && IsWindowVisible(window) ? "visible" : "hidden");
		return 0;
	};

	if (kind == "spawn")
	{
		Tell(target, std::to_string(Start(HangCommand(), CREATE_NO_WINDOW)));
		std::this_thread::sleep_for(child_hang);
		return 0;
	};

	if (kind == "detach")
	{
		void* process = nullptr;
		std::string error;

		Tell(target, Process::StartDetached(test_executable, { hang_argument }, std::filesystem::current_path(), process, error) ? std::to_string(GetProcessId(process)) : "0");
		std::this_thread::sleep_for(child_hang);
		return 0;
	};

	if (kind == "conhost")
	{
		for (std::string line; std::getline(std::cin, line); )
			Tell(target, std::to_string(Start(console_command, DETACHED_PROCESS)));

		return 0;
	};

	return child_failed;
};

#pragma endregion

#pragma region Helpers

namespace
{
	constexpr auto wait_limit = std::chrono::seconds(10) * load_slack;
	constexpr auto wait_step = std::chrono::milliseconds(10);
	constexpr auto settle = std::chrono::milliseconds(500);
	constexpr DWORD wait_limit_ms = DWORD(std::chrono::duration_cast<std::chrono::milliseconds>(wait_limit).count());
	constexpr DWORD settle_ms = DWORD(settle.count());
	constexpr DWORD read_size = 4096;
	constexpr UINT ended_code = 1;
	constexpr size_t big_payload = 1024 * 1024;
	constexpr int launches = 8;
	constexpr size_t guarded_consoles = 2;
	constexpr int exit_code = 7;
	constexpr int64_t other_start = 1;
	constexpr size_t class_capacity = 64;
	constexpr size_t path_capacity = 1024;
	constexpr const wchar_t* console_class = L"ConsoleWindowClass";

	std::atomic<int> made = 0;

	bool Eventually(const std::function<bool()>& done, std::chrono::milliseconds limit = wait_limit)
	{
		const auto deadline = std::chrono::steady_clock::now() + limit;

		while (std::chrono::steady_clock::now() < deadline)
		{
			if (done())
				return true;

			std::this_thread::sleep_for(wait_step);
		};

		return done();
	};

	std::string WithoutReturns(std::string text)
	{
		std::erase(text, '\r');

		return text;
	};

	std::vector<std::string> Lines(const std::string& text)
	{
		std::vector<std::string> lines;
		size_t start = 0;

		for (size_t end = text.find('\n'); end != std::string::npos; end = text.find('\n', start))
		{
			lines.push_back(text.substr(start, end - start));
			start = end + 1;
		};

		return lines;
	};

	DWORD IdIn(const std::string& line)
	{
		return DWORD(std::strtoul(line.c_str(), nullptr, 10));
	};

	std::optional<std::string> Read(void* output, bool firstLine = false)
	{
		std::string text;
		std::atomic<bool> done = false;

		std::thread reader([output, firstLine, &text, &done]()
		{
			std::array<char, read_size> buffer{ };
			const DWORD size = firstLine ? 1 : read_size;
			DWORD read = 0;

			while (ReadFile(output, buffer.data(), size, &read, nullptr) && read > 0)
			{
				text.append(buffer.data(), read);

				if (firstLine && text.back() == '\n')
					break;
			};

			done = true;
		});

		Eventually([&done]() { return done.load(); });
		Process::EndThread(reader);

		text = WithoutReturns(text);

		if (firstLine && !text.ends_with('\n'))
			return std::nullopt;

		if (!done)
			return std::nullopt;

		if (firstLine)
			text.pop_back();

		return text;
	};

	std::string ReadLine(void* output)
	{
		return Read(output, true).value_or("");
	};

	std::wstring UniqueName()
	{
		return L"Loom Process Tests " + std::to_wstring(GetCurrentProcessId()) + L" " + std::to_wstring(made++);
	};

	std::string ThisDesktop()
	{
		std::array<wchar_t, class_capacity> name{ };
		DWORD length = 0;

		GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()), UOI_NAME, name.data(), DWORD(sizeof(name)), &length);

		return std::filesystem::path(name.data()).string();
	};

	bool OwnJobsLetGo()
	{
		BOOL inside = FALSE;

		if (!IsProcessInJob(GetCurrentProcess(), nullptr, &inside) || !inside)
			return true;

		JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{ };

		if (!QueryInformationJobObject(nullptr, JobObjectExtendedLimitInformation, &limits, sizeof(limits), nullptr))
			return false;

		return (limits.BasicLimitInformation.LimitFlags & (JOB_OBJECT_LIMIT_BREAKAWAY_OK | JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK)) != 0;
	};

	struct Child final
	{
		explicit Child(
			const std::vector<std::string>& arguments,
			const std::wstring& desktop = { },
			const std::filesystem::path& folder = { })
			: started(Process::Launch(test_executable, arguments, folder, launched, error, desktop))
		{
		};

		~Child()
		{
			if (launched.job != nullptr)
				TerminateJobObject(launched.job, ended_code);

			for (void* const handle : { launched.input, launched.output, launched.job, launched.process })
				if (handle != nullptr)
					CloseHandle(handle);
		};

		Child(const Child&) = delete;
		Child& operator=(const Child&) = delete;

		bool Ends() const
		{
			return WaitForSingleObject(launched.process, wait_limit_ms) == WAIT_OBJECT_0;
		};

		DWORD ExitCode() const
		{
			DWORD code = STILL_ACTIVE;

			if (Ends())
				GetExitCodeProcess(launched.process, &code);

			return code;
		};

		void Close(void*& handle)
		{
			CloseHandle(handle);
			handle = nullptr;
		};

		Process::Launched launched{ };
		std::string error{ };
		bool started = false;
	};

	struct Descendant final
	{
		Descendant(DWORD id, int64_t since)
			: handle(id == 0 ? nullptr : OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, id))
		{
			if (handle == nullptr)
				return;

			std::array<wchar_t, path_capacity> image{ };
			DWORD length = DWORD(image.size());
			std::error_code ignored;

			ours = QueryFullProcessImageNameW(handle, 0, image.data(), &length)
				&& std::filesystem::equivalent(image.data(), test_executable, ignored)
				&& Process::StartTime(handle) >= since;
		};

		~Descendant()
		{
			if (handle == nullptr)
				return;

			if (ours)
				TerminateProcess(handle, ended_code);

			CloseHandle(handle);
		};

		Descendant(const Descendant&) = delete;
		Descendant& operator=(const Descendant&) = delete;

		bool Ends(DWORD within) const
		{
			return WaitForSingleObject(handle, within) == WAIT_OBJECT_0;
		};

		bool In(HANDLE job) const
		{
			BOOL inside = FALSE;

			return IsProcessInJob(handle, job, &inside) && inside;
		};

		HANDLE handle = nullptr;
		bool ours = false;
	};

	struct Desktop final
	{
		Desktop()
			: name(UniqueName())
			, handle(CreateDesktopW(name.c_str(), nullptr, nullptr, 0, GENERIC_ALL, nullptr))
		{
		};

		~Desktop()
		{
			if (handle != nullptr)
				CloseDesktop(handle);
		};

		Desktop(const Desktop&) = delete;
		Desktop& operator=(const Desktop&) = delete;

		size_t Consoles(bool visibleOnly) const
		{
			struct Search final
			{
				bool visibleOnly;
				size_t found;
			} search{ visibleOnly, 0 };

			EnumDesktopWindows(handle, [](HWND window, LPARAM parameter) -> BOOL
			{
				Search& search = *reinterpret_cast<Search*>(parameter);
				std::array<wchar_t, class_capacity> name{ };

				const bool console = GetClassNameW(window, name.data(), int(name.size())) > 0 && std::wstring_view(name.data()) == console_class;

				if (console && (!search.visibleOnly || IsWindowVisible(window)))
					search.found++;

				return TRUE;
			}, reinterpret_cast<LPARAM>(&search));

			return search.found;
		};

		bool StaysHidden() const
		{
			const auto end = std::chrono::steady_clock::now() + settle;

			while (std::chrono::steady_clock::now() < end)
			{
				if (Consoles(true) > 0)
					return false;

				std::this_thread::sleep_for(wait_step);
			};

			return true;
		};

		std::wstring name;
		HDESK handle = nullptr;
	};

	struct Record final
	{
		Record()
			: path(std::filesystem::temp_directory_path() / ("Loom UI Process " + std::to_string(made++) + " Tests " + std::to_string(GetCurrentProcessId())))
		{
		};

		~Record()
		{
			std::error_code ignored;
			std::filesystem::remove(path, ignored);
		};

		Record(const Record&) = delete;
		Record& operator=(const Record&) = delete;

		std::string FirstLine() const
		{
			std::string line;

			Eventually([this, &line]()
			{
				std::ifstream file(path);
				const std::string text = WithoutReturns(std::string(std::istreambuf_iterator<char>(file), { }));
				const size_t end = text.find('\n');

				if (end == std::string::npos)
					return false;

				line = text.substr(0, end);
				return true;
			});

			return line;
		};

		std::filesystem::path path;
	};

	struct LeavableJob final
	{
		LeavableJob()
			: handle(CreateJobObjectW(nullptr, nullptr))
		{
			JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{ };
			limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_BREAKAWAY_OK | JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;

			ready = handle != nullptr && SetInformationJobObject(handle, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
		};

		~LeavableJob()
		{
			Close();

			if (process != nullptr)
				CloseHandle(process);
		};

		LeavableJob(const LeavableJob&) = delete;
		LeavableJob& operator=(const LeavableJob&) = delete;

		bool Start(const std::vector<std::wstring>& arguments)
		{
			std::wstring line;
			Process::AppendQuoted(line, test_executable.wstring());

			for (const std::wstring& argument : arguments)
			{
				line += L' ';
				Process::AppendQuoted(line, argument);
			};

			STARTUPINFOW startup{ };
			startup.cb = sizeof(startup);

			PROCESS_INFORMATION information{ };

			if (!CreateProcessW(test_executable.c_str(), line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr, &startup, &information))
				return false;

			const bool assigned = AssignProcessToJobObject(handle, information.hProcess) != FALSE;

			if (assigned)
				ResumeThread(information.hThread);
			else
				TerminateProcess(information.hProcess, ended_code);

			CloseHandle(information.hThread);
			process = information.hProcess;

			return assigned;
		};

		void Close()
		{
			if (handle != nullptr)
				CloseHandle(handle);

			handle = nullptr;
		};

		HANDLE handle = nullptr;
		HANDLE process = nullptr;
		bool ready = false;
	};
};

#pragma endregion

TEST_SUITE("Loom Process")
{
	TEST_CASE("a launched child's output carries what it prints, and its exit code")
	{
		Child child({ "--print", "printed", std::to_string(exit_code) });

		REQUIRE(child.started);
		CHECK(Read(child.launched.output) == "printed\n");
		CHECK(child.ExitCode() == DWORD(exit_code));
	};

	TEST_CASE("what is written to a launched child's input reaches it in order, and writing nothing succeeds")
	{
		Child child({ "--echo" });

		REQUIRE(child.started);
		CHECK(Process::WriteAll(child.launched.input, ""));
		CHECK(Process::WriteAll(child.launched.input, "first\nsecond\n"));

		child.Close(child.launched.input);

		CHECK(Read(child.launched.output) == "first\nsecond\n");
	};

	TEST_CASE("a write far larger than the pipe arrives whole while the child reads it")
	{
		Child child({ "--echo" });

		REQUIRE(child.started);

		const std::string payload = std::string(big_payload, 'x') + "\n";
		std::atomic<bool> written = false;

		std::thread writer([&child, &payload, &written]()
		{
			written = Process::WriteAll(child.launched.input, payload);
			child.Close(child.launched.input);
		});

		const std::optional<std::string> echoed = Read(child.launched.output);
		writer.join();

		CHECK(written);
		REQUIRE(echoed.has_value());
		CHECK(echoed->size() == payload.size());
		CHECK(*echoed == payload);
	};

	TEST_CASE("writing to a child that has exited fails")
	{
		Child child({ "--print", "gone", "0" });

		REQUIRE(child.started);
		REQUIRE(Read(child.launched.output).has_value());
		REQUIRE(child.Ends());

		CHECK_FALSE(Process::WriteAll(child.launched.input, "late\n"));
	};

	TEST_CASE("a launched child's error output arrives on its output")
	{
		Child child({ "--process-child", "error", std::string(to_output), "complaint" });

		REQUIRE(child.started);
		CHECK(Read(child.launched.output) == "complaint\n");
	};

	TEST_CASE("arguments reach a launched child exactly as given")
	{
		const std::vector<std::string> given = { "plain", "with space", "", "trailing\\", "quote\"inside", "slashes\\\\\"quote", "ends with slashes\\\\", "tab\there" };

		std::vector<std::string> arguments = { "--process-child", "arguments", std::string(to_output) };
		arguments.insert(arguments.end(), given.begin(), given.end());

		Child child(arguments);

		REQUIRE(child.started);

		const std::optional<std::string> told = Read(child.launched.output);

		REQUIRE(told.has_value());
		CHECK(Lines(*told) == given);
	};

	TEST_CASE("a quoted argument reads back the same through CommandLineToArgvW")
	{
		const std::vector<std::wstring> given = { L"plain", L"with space", L"", L"trailing\\", L"quote\"inside", L"slashes\\\\\"quote", L"\\\\", L"café", L"\"" };

		std::wstring line;
		Process::AppendQuoted(line, L"program");

		for (const std::wstring& argument : given)
		{
			line += L' ';
			Process::AppendQuoted(line, argument);
		};

		int count = 0;
		wchar_t** const parsed = CommandLineToArgvW(line.c_str(), &count);

		REQUIRE(parsed != nullptr);
		REQUIRE(count == int(given.size()) + 1);

		for (size_t index = 0; index < given.size(); index++)
			CHECK(std::wstring(parsed[index + 1]) == given[index]);

		LocalFree(parsed);
	};

	TEST_CASE("a launched child works in the folder given, or in the caller's with none")
	{
		const std::filesystem::path folder = std::filesystem::temp_directory_path();

		Child given({ "--process-child", "folder", std::string(to_output) }, { }, folder);
		Child none({ "--process-child", "folder", std::string(to_output) });

		REQUIRE(given.started);
		REQUIRE(none.started);

		std::error_code ignored;

		CHECK(std::filesystem::equivalent(ReadLine(given.launched.output), folder, ignored));
		CHECK(std::filesystem::equivalent(ReadLine(none.launched.output), std::filesystem::current_path(), ignored));
	};

	TEST_CASE("a launch of a missing executable fails, says why and hands back nothing")
	{
		Process::Launched launched{ };
		std::string error;

		CHECK_FALSE(Process::Launch(test_executable.parent_path() / "missing.exe", { }, { }, launched, error));
		CHECK_FALSE(error.empty());
		CHECK(launched.process == nullptr);
		CHECK(launched.job == nullptr);
		CHECK(launched.input == nullptr);
		CHECK(launched.output == nullptr);
	};

	TEST_CASE("a launched child has no console window")
	{
		const Desktop desktop;
		REQUIRE(desktop.handle != nullptr);

		Child child({ "--process-child", "console", std::string(to_output) }, desktop.name);

		REQUIRE(child.started);
		CHECK(ReadLine(child.launched.output) == "hidden");
	};

	TEST_CASE("a launched child runs on the desktop named, and on the caller's with none")
	{
		const Desktop desktop;
		REQUIRE(desktop.handle != nullptr);

		Child named({ "--desktop" }, desktop.name);
		Child none({ "--desktop" });

		REQUIRE(named.started);
		REQUIRE(none.started);
		CHECK(ReadLine(named.launched.output) == std::filesystem::path(desktop.name).string());
		CHECK(ReadLine(none.launched.output) == ThisDesktop());
	};

	TEST_CASE("a launched child is in its job, and closing the job ends it and every process it started")
	{
		Child child({ "--process-child", "spawn", std::string(to_output) });

		REQUIRE(child.started);

		BOOL inside = FALSE;
		CHECK((IsProcessInJob(child.launched.process, child.launched.job, &inside) && inside));

		const Descendant grandchild(IdIn(ReadLine(child.launched.output)), Process::StartTime(child.launched.process));

		REQUIRE(grandchild.ours);
		REQUIRE(grandchild.In(child.launched.job));

		child.Close(child.launched.job);

		CHECK(child.Ends());
		CHECK(grandchild.Ends(wait_limit_ms));
	};

	TEST_CASE("launches on many threads at once each read only their own output to its end")
	{
		std::vector<std::unique_ptr<Child>> hanging(launches);
		std::vector<std::optional<std::string>> outputs(launches);
		std::vector<DWORD> codes(launches, STILL_ACTIVE);
		std::vector<std::thread> threads;

		for (int index = 0; index < launches; index++)
			threads.emplace_back([index, &hanging, &outputs, &codes]()
			{
				hanging[index] = std::make_unique<Child>(std::vector<std::string>{ "--hang" });

				Child printing({ "--print", std::to_string(index), std::to_string(index) });

				if (!printing.started)
					return;

				outputs[index] = Read(printing.launched.output);
				codes[index] = printing.ExitCode();
			});

		for (std::thread& thread : threads)
			thread.join();

		for (int index = 0; index < launches; index++)
		{
			CHECK(hanging[index]->started);
			CHECK(outputs[index] == std::to_string(index) + "\n");
			CHECK(codes[index] == DWORD(index));
		};
	};

	TEST_CASE("a thread blocked reading a child's output ends at once, and ending no thread does nothing")
	{
		Child child({ "--hang" });

		REQUIRE(child.started);

		std::atomic<bool> reading = false;

		std::thread reader([&child, &reading]()
		{
			char letter = 0;
			DWORD read = 0;

			reading = true;
			ReadFile(child.launched.output, &letter, 1, &read, nullptr);
		});

		REQUIRE(Eventually([&reading]() { return reading.load(); }));
		std::this_thread::sleep_for(settle);

		const auto start = std::chrono::steady_clock::now();
		Process::EndThread(reader);

		CHECK_FALSE(reader.joinable());
		CHECK(std::chrono::steady_clock::now() - start < wait_limit);

		std::thread none;
		Process::EndThread(none);

		CHECK_FALSE(none.joinable());
	};

	TEST_CASE("a start time is read from a process, and is 0 with none")
	{
		Child child({ "--hang" });

		REQUIRE(child.started);

		const int64_t start = Process::StartTime(child.launched.process);

		CHECK(start != 0);
		CHECK(Process::StartTime(GetCurrentProcess()) <= start);
		CHECK(Process::StartTime(nullptr) == 0);
	};

	TEST_CASE("a child with threads at work is alive under its own start time only, and not once ended")
	{
		Child child({ "--threads" });

		REQUIRE(child.started);

		const uint32_t id = GetProcessId(child.launched.process);
		const int64_t start = Process::StartTime(child.launched.process);

		CHECK(Eventually([id, start]() { return Process::Alive(id, start); }));
		CHECK_FALSE(Process::Alive(id, other_start));

		Process::Terminate(id, other_start);

		CHECK(WaitForSingleObject(child.launched.process, settle_ms) == WAIT_TIMEOUT);

		Process::Terminate(id, start);

		CHECK(child.Ends());
		CHECK_FALSE(Process::Alive(id, start));
		CHECK_FALSE(Process::Alive(0, 0));
	};

	TEST_CASE("many processes are checked at once, in the order asked, and none asked gives none")
	{
		Child child({ "--threads" });

		REQUIRE(child.started);

		const uint32_t id = GetProcessId(child.launched.process);
		const int64_t start = Process::StartTime(child.launched.process);
		const std::vector<std::pair<uint32_t, int64_t>> asked = { { 0, 0 }, { id, other_start }, { id, start }, { GetCurrentProcessId(), 0 } };

		REQUIRE(Eventually([id, start]() { return Process::Alive(id, start); }));

		const std::vector<bool> alive = Process::AliveAll(asked);

		REQUIRE(alive.size() == asked.size());
		CHECK_FALSE(alive[0]);
		CHECK_FALSE(alive[1]);
		CHECK(alive[2]);
		CHECK_FALSE(alive[3]);
		CHECK(Process::AliveAll({ }).empty());
	};

	TEST_CASE("terminating nothing, or a process already gone, does nothing")
	{
		Process::Terminate(0, 0);

		Child child({ "--hang" });

		REQUIRE(child.started);

		const uint32_t id = GetProcessId(child.launched.process);
		const int64_t start = Process::StartTime(child.launched.process);

		Process::Terminate(id, start);
		Process::Terminate(id, start);

		CHECK(child.Ends());
	};

	TEST_CASE("a system error reads as the system's message")
	{
		CHECK_FALSE(Process::SystemError(ERROR_FILE_NOT_FOUND).empty());
		CHECK(Process::SystemError(ERROR_FILE_NOT_FOUND) == std::system_category().message(ERROR_FILE_NOT_FOUND));
	};

	TEST_CASE("a detached start runs the child hidden with the arguments given, and hands back its process")
	{
		const Record console;
		const Record told;
		const std::filesystem::path folder = std::filesystem::temp_directory_path();

		void* hidden = nullptr;
		void* arguments = nullptr;
		std::string error;

		REQUIRE(Process::StartDetached(test_executable, { L"--process-child", L"console", console.path.wstring() }, folder, hidden, error));
		REQUIRE(Process::StartDetached(test_executable, { L"--process-child", L"arguments", told.path.wstring(), L"with space", L"quote\"inside" }, folder, arguments, error));

		CHECK(console.FirstLine() == "hidden");
		CHECK(told.FirstLine() == "with space");

		for (void* const process : { hidden, arguments })
		{
			CHECK(WaitForSingleObject(process, wait_limit_ms) == WAIT_OBJECT_0);
			CloseHandle(process);
		};
	};

	TEST_CASE("a detached start works in the folder given, and carries its exit code")
	{
		const Record given;
		const std::filesystem::path folder = std::filesystem::temp_directory_path();

		void* inFolder = nullptr;
		void* printing = nullptr;
		std::string error;

		REQUIRE(Process::StartDetached(test_executable, { L"--process-child", L"folder", given.path.wstring() }, folder, inFolder, error));
		REQUIRE(Process::StartDetached(test_executable, { L"--print", L"printed", std::to_wstring(exit_code) }, folder, printing, error));

		std::error_code ignored;

		CHECK(std::filesystem::equivalent(given.FirstLine(), folder, ignored));

		DWORD code = STILL_ACTIVE;
		CHECK(WaitForSingleObject(printing, wait_limit_ms) == WAIT_OBJECT_0);
		GetExitCodeProcess(printing, &code);
		CHECK(code == DWORD(exit_code));

		for (void* const process : { inFolder, printing })
		{
			WaitForSingleObject(process, wait_limit_ms);
			CloseHandle(process);
		};
	};

	TEST_CASE("a detached start with no folder works in the caller's, as a launch does")
	{
		const Record none;

		void* process = nullptr;
		std::string error;

		REQUIRE(Process::StartDetached(test_executable, { L"--process-child", L"folder", none.path.wstring() }, { }, process, error));

		std::error_code ignored;

		CHECK(std::filesystem::equivalent(none.FirstLine(), std::filesystem::current_path(), ignored));
		CHECK(WaitForSingleObject(process, wait_limit_ms) == WAIT_OBJECT_0);

		CloseHandle(process);
	};

	TEST_CASE("a detached start of a missing executable fails, says why and hands back nothing")
	{
		void* process = nullptr;
		std::string error;

		CHECK_FALSE(Process::StartDetached(test_executable.parent_path() / "missing.exe", { }, { }, process, error));
		CHECK_FALSE(error.empty());
		CHECK(process == nullptr);
	};

	TEST_CASE("a detached start leaves a job that lets it, and outlives that job")
	{
		LeavableJob job;
		const Record record;

		REQUIRE(job.ready);
		REQUIRE(job.Start({ L"--process-child", L"detach", record.path.wstring() }));

		const Descendant grandchild(IdIn(record.FirstLine()), Process::StartTime(job.process));

		REQUIRE(grandchild.ours);

		if (grandchild.In(job.handle))
		{
			CHECK_FALSE(OwnJobsLetGo());
			return;
		};

		job.Close();

		CHECK(WaitForSingleObject(job.process, wait_limit_ms) == WAIT_OBJECT_0);
		CHECK_FALSE(grandchild.Ends(settle_ms));
	};

	TEST_CASE("a detached start from a job that forbids leaving still starts, and ends with that job")
	{
		const Record record;
		Child child({ "--process-child", "detach", record.path.string() });

		REQUIRE(child.started);

		const Descendant grandchild(IdIn(record.FirstLine()), Process::StartTime(child.launched.process));

		REQUIRE(grandchild.ours);
		CHECK(grandchild.In(child.launched.job));

		child.Close(child.launched.job);

		CHECK(grandchild.Ends(wait_limit_ms));
	};

	TEST_CASE("without a guard a console a launched child opens shows on its desktop")
	{
		const Desktop desktop;
		REQUIRE(desktop.handle != nullptr);

		Child child({ "--process-child", "conhost", std::string(to_output) }, desktop.name);

		REQUIRE(child.started);
		REQUIRE(Process::WriteAll(child.launched.input, "\n"));
		REQUIRE(IdIn(ReadLine(child.launched.output)) != 0);

		CHECK(Eventually([&desktop]() { return desktop.Consoles(true) == 1; }));
	};

	TEST_CASE("a guard on a launched child's job hides every console the child opens")
	{
		const Desktop desktop;
		REQUIRE(desktop.handle != nullptr);

		Child child({ "--process-child", "conhost", std::string(to_output) }, desktop.name);

		REQUIRE(child.started);

		const Process::ConsoleGuard guard(child.launched.job, desktop.handle);

		for (size_t opened = 1; opened <= guarded_consoles; opened++)
		{
			REQUIRE(Process::WriteAll(child.launched.input, "\n"));
			REQUIRE(IdIn(ReadLine(child.launched.output)) != 0);
			REQUIRE(Eventually([&desktop, opened]() { return desktop.Consoles(false) == opened; }));

			CHECK(Eventually([&desktop]() { return desktop.Consoles(true) == 0; }));
			CHECK(desktop.StaysHidden());
		};
	};

	TEST_CASE("a guard leaves visible the console of a child in another job")
	{
		const Desktop desktop;
		REQUIRE(desktop.handle != nullptr);

		Child guarded({ "--hang" }, desktop.name);
		Child other({ "--process-child", "conhost", std::string(to_output) }, desktop.name);

		REQUIRE(guarded.started);
		REQUIRE(other.started);

		const Process::ConsoleGuard guard(guarded.launched.job, desktop.handle);

		REQUIRE(Process::WriteAll(other.launched.input, "\n"));
		REQUIRE(IdIn(ReadLine(other.launched.output)) != 0);
		REQUIRE(Eventually([&desktop]() { return desktop.Consoles(true) == 1; }));

		std::this_thread::sleep_for(settle);

		CHECK(desktop.Consoles(true) == 1);
	};

	TEST_CASE("once its guard ends, a console the job opens shows again")
	{
		const Desktop desktop;
		REQUIRE(desktop.handle != nullptr);

		Child child({ "--process-child", "conhost", std::string(to_output) }, desktop.name);

		REQUIRE(child.started);

		{
			const Process::ConsoleGuard guard(child.launched.job, desktop.handle);

			REQUIRE(Process::WriteAll(child.launched.input, "\n"));
			REQUIRE(IdIn(ReadLine(child.launched.output)) != 0);
			REQUIRE(Eventually([&desktop]() { return desktop.Consoles(false) == 1; }));
			REQUIRE(Eventually([&desktop]() { return desktop.Consoles(true) == 0; }));
		};

		REQUIRE(Process::WriteAll(child.launched.input, "\n"));
		REQUIRE(IdIn(ReadLine(child.launched.output)) != 0);

		CHECK(Eventually([&desktop]() { return desktop.Consoles(true) == 1; }));
	};
};
