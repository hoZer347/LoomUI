#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"

#include "Testing/Cleanup.h"
#include "Testing/ChildModes.h"
#include "Testing/RunTemp.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>


std::filesystem::path test_executable;

int FakeConsoleWindows(int count, char** arguments);
int ProcessChild(int count, char** arguments);
int LoomAppRun(int count, char** arguments);
int LoomPlacementWindow(int count, char** arguments);

// The executable doubles as the child the process, app and console tests start.
int main(int count, char** arguments)
{
	constexpr const char* folder_prefix = "Loom UI ";
	constexpr const char* scratch_name = "Loom UI Tests App Data ";

	test_executable = std::filesystem::absolute(arguments[0]);

	const std::string_view mode = count > 1 ? arguments[1] : "";

	if (mode == "--fake-console")
		return FakeConsoleWindows(count, arguments);

	if (mode == "--process-child")
		return ProcessChild(count, arguments);

	if (mode == "--loom-app")
		return LoomAppRun(count, arguments);

	if (mode == "--loom-placement")
		return LoomPlacementWindow(count, arguments);

	if (const std::optional<int> done = Loom::Testing::RunChildMode(count, arguments))
		return *done;

	const std::filesystem::path scratch = std::filesystem::temp_directory_path() / (scratch_name + std::to_string(GetCurrentProcessId()));

	std::filesystem::create_directories(scratch);
	_putenv_s("LOCALAPPDATA", scratch.string().c_str());

	const Loom::Testing::RunTemp runTemp(std::filesystem::temp_directory_path());

	const int result = doctest::Context(count, arguments).run();

	Loom::Testing::EndChildren();
	Loom::Testing::RemoveKept(folder_prefix);

	std::error_code ignored;
	std::filesystem::remove_all(scratch, ignored);

	return result;
};
