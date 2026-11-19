#include "doctest.h"

#include "Testing/RunTemp.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

extern std::filesystem::path test_executable;

using namespace Loom::Testing;

#pragma region Helpers

namespace
{
	constexpr DWORD first_run = 1;
	constexpr DWORD second_run = 2;
	constexpr auto legacy_old_age = std::chrono::hours(2);

	std::filesystem::path Leaf(const std::filesystem::path& folder)
	{
		return (folder / "").parent_path().filename();
	};

	std::string Variable(const char* name)
	{
		const char* const value = std::getenv(name);

		return value == nullptr ? std::string() : value;
	};

	struct Scratch final
	{
		Scratch() :
			base(std::filesystem::temp_directory_path() / "RunTemp Tests")
		{
			std::filesystem::remove_all(base);
			std::filesystem::create_directories(base);
		};

		~Scratch()
		{
			std::error_code ignored;
			std::filesystem::remove_all(base, ignored);
		};

		const std::filesystem::path base;

		std::filesystem::path Folder(const std::string& name) const
		{
			const std::filesystem::path folder = base / name;
			std::filesystem::create_directories(folder);
			std::ofstream(folder / "left.txt") << "left";

			return folder;
		};
	};

	struct EndedProcess final
	{
		EndedProcess()
		{
			std::wstring command = L"\"" + test_executable.wstring() + L"\" --print x 0";
			STARTUPINFOW startup{ };
			startup.cb = sizeof(startup);
			PROCESS_INFORMATION information{ };

			if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &information))
				return;

			CloseHandle(information.hThread);
			WaitForSingleObject(information.hProcess, INFINITE);
			handle = information.hProcess;
			id = information.dwProcessId;
		};

		EndedProcess(const EndedProcess&) = delete;
		EndedProcess& operator=(const EndedProcess&) = delete;

		~EndedProcess()
		{
			if (handle != nullptr)
				CloseHandle(handle);
		};

		HANDLE handle = nullptr;
		DWORD id = 0;
	};
};

#pragma endregion

TEST_SUITE("RunTemp")
{
	#pragma region Run folder

	TEST_CASE("the temp folder during a run is this run's own")
	{
		const std::filesystem::path temp = std::filesystem::temp_directory_path();

		CHECK(Leaf(temp) == RunTempFolder("", GetCurrentProcessId()));
		CHECK(Leaf(temp).string().rfind(run_temp_prefix, 0) == 0);
		CHECK(std::filesystem::is_directory(temp));
	};

	TEST_CASE("two runs get different temp folders under the same base")
	{
		const std::filesystem::path base = "base";

		CHECK(RunTempFolder(base, first_run) == base / "cct1");
		CHECK(RunTempFolder(base, second_run) == base / "cct2");
		CHECK(RunTempFolder(base, first_run) != RunTempFolder(base, second_run));
	};

	TEST_CASE("a run temp points TEMP and TMP at its folder, then puts them back and removes it")
	{
		const Scratch scratch;
		const std::string temp = Variable("TEMP");
		const std::string tmp = Variable("TMP");
		std::filesystem::path folder;

		{
			const RunTemp run(scratch.base);
			folder = run.folder;

			CHECK(folder == RunTempFolder(scratch.base, GetCurrentProcessId()));
			CHECK(std::filesystem::is_directory(folder));
			CHECK(Variable("TEMP") == folder.string());
			CHECK(Variable("TMP") == folder.string());
			CHECK(Leaf(std::filesystem::temp_directory_path()) == Leaf(folder));

			std::ofstream(folder / "left.txt") << "left";
		};

		CHECK(Variable("TEMP") == temp);
		CHECK(Variable("TMP") == tmp);
		CHECK_FALSE(std::filesystem::exists(folder));
	};

	#pragma endregion

	#pragma region Sweep

	TEST_CASE("a sweep removes the folder of an ended run and leaves a live run's and unrelated ones")
	{
		const Scratch scratch;
		const EndedProcess ended;

		REQUIRE(ended.handle != nullptr);

		const std::filesystem::path dead = scratch.Folder(RunTempFolder("", ended.id).string());
		const std::filesystem::path live = scratch.Folder(RunTempFolder("", GetCurrentProcessId()).string());
		const std::filesystem::path unrelated = scratch.Folder("Unrelated");
		const std::filesystem::path unnumbered = scratch.Folder(std::string(run_temp_prefix) + "x");

		SweepRunTemps(scratch.base);

		CHECK_FALSE(std::filesystem::exists(dead));
		CHECK(std::filesystem::exists(live / "left.txt"));
		CHECK(std::filesystem::exists(unrelated / "left.txt"));
		CHECK(std::filesystem::exists(unnumbered / "left.txt"));
	};

	TEST_CASE("a sweep removes old legacy test folders and leaves fresh ones")
	{
		const Scratch scratch;
		const std::filesystem::path old = scratch.Folder("Corrin Code Tests 123");
		const std::filesystem::path oldLead = scratch.Folder("Corrin Code Lead Tests 456");
		const std::filesystem::path fresh = scratch.Folder("Corrin Code Tests 789");
		const auto stale = std::filesystem::file_time_type::clock::now() - legacy_old_age;

		std::filesystem::last_write_time(old, stale);
		std::filesystem::last_write_time(oldLead, stale);

		SweepRunTemps(scratch.base);

		CHECK_FALSE(std::filesystem::exists(old));
		CHECK_FALSE(std::filesystem::exists(oldLead));
		CHECK(std::filesystem::exists(fresh / "left.txt"));
	};

	TEST_CASE("a sweep of a missing base does nothing")
	{
		const Scratch scratch;

		CHECK_NOTHROW(SweepRunTemps(scratch.base / "missing"));
	};

	#pragma endregion
};
