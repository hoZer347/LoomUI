#include "doctest.h"

#include "Testing/HiddenChild.h"

#include "AppSettings.h"
#include "Placement.h"

#include <GLFW/glfw3.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <process.h>
#include <sstream>
#include <string>

using namespace Loom;

#pragma region Helpers

namespace
{
	constexpr const char* local_app_data = "LOCALAPPDATA";
	constexpr const char* placement_mode = "--loom-placement";
	constexpr int window_width = 400;
	constexpr int window_height = 300;
	constexpr int placement_failed = 2;
	constexpr Placement on_screen{ 100, 100, 740, 580, false };
	constexpr Placement elsewhere{ 120, 140, 620, 540, false };
	constexpr Placement off_screen{ -100000, -100000, -99360, -99520, false };

	struct TempFolder final
	{
		TempFolder() :
			path(std::filesystem::temp_directory_path() / ("Loom UI AppSettings Tests " + std::to_string(_getpid())))
		{
			std::filesystem::remove_all(path);
			std::filesystem::create_directories(path);
		};

		~TempFolder()
		{
			std::filesystem::remove_all(path);
		};

		const std::filesystem::path path;
	};

	// Points LOCALAPPDATA at a folder while it lives, then back.
	struct LocalAppData final
	{
		explicit LocalAppData(const std::filesystem::path& folder)
		{
			if (const char* const old = std::getenv(local_app_data); old != nullptr)
				previous = old;

			_putenv_s(local_app_data, folder.string().c_str());
		};

		~LocalAppData()
		{
			_putenv_s(local_app_data, previous ? previous->c_str() : "");
		};

		LocalAppData(const LocalAppData&) = delete;
		LocalAppData& operator=(const LocalAppData&) = delete;

		std::optional<std::string> previous;
	};

	std::string Bytes(const std::filesystem::path& file)
	{
		std::ifstream stream(file, std::ios::binary);
		std::ostringstream text;
		text << stream.rdbuf();

		return text.str();
	};

	std::string Line(const std::optional<Placement>& placement)
	{
		if (!placement)
			return "none";

		std::ostringstream text;
		text << placement->left << ' ' << placement->top << ' ' << placement->right << ' ' << placement->bottom << ' ' << (placement->maximized ? 1 : 0);

		return text.str();
	};

	Placement Maximized(Placement placement)
	{
		placement.maximized = true;

		return placement;
	};
}

#pragma endregion

#pragma region Child

// Opens a window and prints what Take makes of it after each Show, one name=placement a line.
int LoomPlacementWindow(int, char**)
{
	if (!glfwInit())
		return placement_failed;

	glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

	GLFWwindow* const window = glfwCreateWindow(window_width, window_height, "Loom Placement", nullptr, nullptr);

	if (window == nullptr)
	{
		glfwTerminate();
		return placement_failed;
	};

	const auto print = [](const char* const name, const std::optional<Placement>& placement)
	{
		std::cout << name << '=' << Line(placement) << '\n';
	};

	Placement::Show(window, on_screen);
	print("shown", Placement::Take(window, std::nullopt));

	Placement::Show(window, off_screen);
	print("offScreen", Placement::Take(window, std::nullopt));

	Placement::Show(window, Maximized(on_screen));
	print("maximized", Placement::Take(window, std::nullopt));
	print("maximizedKept", Placement::Take(window, elsewhere));

	glfwIconifyWindow(window);
	print("iconified", Placement::Take(window, std::nullopt));
	print("iconifiedKept", Placement::Take(window, elsewhere));

	Placement::Show(window, std::nullopt);
	print("restored", Placement::Take(window, std::nullopt));
	std::cout << "visible=" << glfwGetWindowAttrib(window, GLFW_VISIBLE) << '\n';
	std::cout.flush();

	glfwDestroyWindow(window);
	glfwTerminate();

	return 0;
}

#pragma endregion

#pragma region Settings folder

TEST_CASE("the settings folder is the name under LOCALAPPDATA, made when missing, and files sit in it")
{
	const TempFolder base;
	const LocalAppData local(base.path);

	const AppSettings settings("Loom App");

	CHECK(settings.Folder() == base.path / "Loom App");
	CHECK(std::filesystem::is_directory(settings.Folder()));
	CHECK(settings.File("window.txt") == base.path / "Loom App" / "window.txt");
}

TEST_CASE("an earlier version's folder is copied to the new one once, and never over it")
{
	const TempFolder base;
	const LocalAppData local(base.path);

	std::filesystem::create_directories(base.path / "Old App" / "nested");
	std::ofstream(base.path / "Old App" / "imgui.ini", std::ios::binary) << "old layout\n";
	std::ofstream(base.path / "Old App" / "nested" / "kept.txt", std::ios::binary) << "kept\n";

	const AppSettings first("New App", "Old App");

	CHECK(Bytes(first.File("imgui.ini")) == "old layout\n");
	CHECK(Bytes(first.Folder() / "nested" / "kept.txt") == "kept\n");
	CHECK(std::filesystem::exists(base.path / "Old App" / "imgui.ini"));

	std::ofstream(base.path / "Old App" / "imgui.ini", std::ios::binary | std::ios::trunc) << "changed\n";

	const AppSettings second("New App", "Old App");

	CHECK(Bytes(second.File("imgui.ini")) == "old layout\n");
}

TEST_CASE("with no earlier folder, or none named, the new folder starts empty")
{
	const TempFolder base;
	const LocalAppData local(base.path);

	const AppSettings missing("New App", "Never There");
	const AppSettings unnamed("Other App");

	CHECK(std::filesystem::is_empty(missing.Folder()));
	CHECK(std::filesystem::is_empty(unnamed.Folder()));
}

#pragma endregion

#pragma region File formats

TEST_CASE("a value is written as its text and one newline, and an empty one removes the file")
{
	const TempFolder folder;
	const std::filesystem::path file = folder.path / "background.txt";

	AppSettings::WriteValue(file, "Corrin.webp");

	CHECK(Bytes(file) == "Corrin.webp\n");
	CHECK(AppSettings::ReadValue(file) == "Corrin.webp");

	AppSettings::WriteValue(file, "");

	CHECK_FALSE(std::filesystem::exists(file));
	CHECK(AppSettings::ReadValue(file).empty());
}

TEST_CASE("a list is written one entry a line with newlines alone")
{
	const TempFolder folder;
	const std::filesystem::path file = folder.path / "folders.txt";

	AppSettings::WriteList(file, { "C:\\One", "C:\\Two" });

	CHECK(Bytes(file) == "C:\\One\nC:\\Two\n");
}

TEST_CASE("a placement is written as left top right bottom and 0 or 1 on one line")
{
	const TempFolder folder;
	const std::filesystem::path file = folder.path / "window.txt";

	Placement::Write(file, { 1, 2, 3, 4, false });

	CHECK(Bytes(file) == "1 2 3 4 0\n");

	Placement::Write(file, { -1080, 229, -508, 1235, true });

	CHECK(Bytes(file) == "-1080 229 -508 1235 1\n");
}

#pragma endregion

#pragma region Live window

TEST_CASE("a window is put where its placement says while a monitor shows it, and taken back from where it sits")
{
	Finished finished = RunHidden({ placement_mode });
	std::erase(finished.output, '\r');

	REQUIRE(finished.exit == 0);

	const std::string expected =
		"shown=" + Line(on_screen) + "\n"
		+ "offScreen=" + Line(on_screen) + "\n"
		+ "maximized=" + Line(Maximized(on_screen)) + "\n"
		+ "maximizedKept=" + Line(Maximized(elsewhere)) + "\n"
		+ "iconified=none\n"
		+ "iconifiedKept=" + Line(elsewhere) + "\n"
		+ "restored=" + Line(on_screen) + "\n"
		+ "visible=1\n";

	CHECK(finished.output == expected);
}

#pragma endregion
