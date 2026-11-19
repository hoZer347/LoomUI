#include "doctest.h"

#include "AppSettings.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <process.h>
#include <string>
#include <vector>

#include <windows.h>

using namespace Loom;

#pragma region Helpers

namespace
{
	struct TempFolder final
	{
		TempFolder() :
			path(std::filesystem::temp_directory_path() / ("Loom UI Folders Tests " + std::to_string(_getpid())))
		{
			std::filesystem::remove_all(path);
			std::filesystem::create_directories(path / "Game");
			std::filesystem::create_directories(path / "Tools");
		};

		~TempFolder()
		{
			std::filesystem::remove_all(path);
		};

		const std::filesystem::path path;

		std::string Game() const
		{
			return (path / "Game").string();
		};

		std::string Tools() const
		{
			return (path / "Tools").string();
		};
	};
};

#pragma endregion

#pragma region FolderTitle

TEST_CASE("FolderTitle is the folder's last name")
{
	CHECK(FolderTitle("C:\\Users\\me\\Desktop\\SHIFT") == "SHIFT");
	CHECK(FolderTitle("C:/Projects/Corrin Code") == "Corrin Code");
}

TEST_CASE("FolderTitle ignores a trailing separator")
{
	CHECK(FolderTitle("C:\\Projects\\Game\\") == "Game");
	CHECK(FolderTitle("C:/Projects/Game/") == "Game");
}

TEST_CASE("FolderTitle of a drive is the drive")
{
	CHECK(FolderTitle("C:\\") == "C:\\");
}

TEST_CASE("FolderTitle keeps a name that is not ASCII")
{
	CHECK(FolderTitle("C:\\Projects\\\xC3\xA9t\xC3\xA9") == "\xC3\xA9t\xC3\xA9");
}

TEST_CASE("FolderTitle of nothing is nothing")
{
	CHECK(FolderTitle("").empty());
}

#pragma endregion

#pragma region SameFolder

TEST_CASE("SameFolder matches equal text without touching the disk")
{
	CHECK(SameFolder("Z:\\Nowhere\\Game", "Z:\\Nowhere\\Game"));
}

TEST_CASE("SameFolder matches one folder written two ways")
{
	const TempFolder temp;

	CHECK(SameFolder(temp.Game(), (temp.path / "Tools" / ".." / "Game").string()));
	CHECK(SameFolder(temp.Game(), temp.Game() + "\\"));
}

TEST_CASE("SameFolder tells two folders apart")
{
	const TempFolder temp;

	CHECK_FALSE(SameFolder(temp.Game(), temp.Tools()));
	CHECK_FALSE(SameFolder("Z:\\Nowhere\\Game", "Z:\\Nowhere\\Tools"));
}

#pragma endregion

#pragma region FindFolder

TEST_CASE("FindFolder finds the tab already open on the folder")
{
	const TempFolder temp;
	const std::vector<std::string> open{ temp.Game(), temp.Tools() };

	CHECK(FindFolder(open, temp.Tools()) == 1u);
	CHECK(FindFolder(open, temp.Game() + "\\") == 0u);
}

TEST_CASE("FindFolder finds nothing for a folder not open")
{
	const TempFolder temp;

	CHECK_FALSE(FindFolder({ temp.Game() }, temp.Tools()).has_value());
	CHECK_FALSE(FindFolder({ }, temp.Game()).has_value());
}

TEST_CASE("FindFolder takes the first of two tabs on one folder")
{
	const TempFolder temp;

	CHECK(FindFolder({ temp.Tools(), temp.Game(), temp.Game() }, temp.Game()) == 1u);
}

#pragma endregion

#pragma region MigrateAppFolder

TEST_CASE("MigrateAppFolder copies the old folder once")
{
	const TempFolder temp;
	const std::filesystem::path old = temp.path / "old";
	const std::filesystem::path fresh = temp.path / "fresh";

	std::filesystem::create_directories(old / "sub");
	std::ofstream(old / "a.txt") << "one";
	std::ofstream(old / "sub" / "b.txt") << "two";

	MigrateAppFolder(old, fresh);

	CHECK(std::filesystem::exists(fresh / "a.txt"));
	CHECK(std::filesystem::exists(fresh / "sub" / "b.txt"));
	CHECK(std::filesystem::exists(old / "a.txt"));

	std::ofstream(old / "c.txt") << "three";
	MigrateAppFolder(old, fresh);

	CHECK_FALSE(std::filesystem::exists(fresh / "c.txt"));
}

TEST_CASE("MigrateAppFolder never overwrites an existing new folder")
{
	const TempFolder temp;
	const std::filesystem::path old = temp.path / "old";
	const std::filesystem::path fresh = temp.path / "fresh";

	std::filesystem::create_directories(old);
	std::filesystem::create_directories(fresh);
	std::ofstream(old / "a.txt") << "old";
	std::ofstream(fresh / "a.txt") << "new";

	MigrateAppFolder(old, fresh);

	std::ifstream read(fresh / "a.txt");
	std::string text;
	read >> text;

	CHECK(text == "new");
}

TEST_CASE("MigrateAppFolder does nothing without the old folder")
{
	const TempFolder temp;
	const std::filesystem::path fresh = temp.path / "fresh";

	MigrateAppFolder(temp.path / "old", fresh);

	CHECK_FALSE(std::filesystem::exists(fresh));
}

#pragma endregion

#pragma region RepoRoot

TEST_CASE("RepoRoot is the nearest folder above with a .git folder or file")
{
	const TempFolder temp;
	const std::filesystem::path deep = std::filesystem::path(temp.Game()) / "Build" / "Release";
	std::filesystem::create_directories(deep);
	std::filesystem::create_directories(std::filesystem::path(temp.Game()) / ".git");
	std::ofstream(std::filesystem::path(temp.Tools()) / ".git") << "gitdir: elsewhere";

	CHECK(RepoRoot(deep) == std::filesystem::path(temp.Game()));
	CHECK(RepoRoot(temp.Game()) == std::filesystem::path(temp.Game()));
	CHECK(RepoRoot(temp.Tools()) == std::filesystem::path(temp.Tools()));
}

TEST_CASE("RepoRoot is nothing outside a repo and for nothing")
{
	const TempFolder temp;

	CHECK_FALSE(RepoRoot(temp.path).has_value());
	CHECK_FALSE(RepoRoot({ }).has_value());
}

TEST_CASE("ModuleFolder is the folder of the exe these tests run in, inside this repo")
{
	std::array<wchar_t, MAX_PATH> exe{ };
	REQUIRE(GetModuleFileNameW(nullptr, exe.data(), (DWORD)exe.size()) != 0);

	CHECK(ModuleFolder() == std::filesystem::path(exe.data()).parent_path());
	CHECK(RepoRoot(ModuleFolder()).has_value());
}

#pragma endregion

#pragma region FolderMatch

TEST_CASE("FolderMatch knows the home folder written another way")
{
	const TempFolder temp;
	FolderMatch match;

	CHECK(match.Home(temp.Game(), temp.Game() + "\\"));
	CHECK_FALSE(match.Home(temp.Game(), temp.Tools()));
}

TEST_CASE("FolderMatch keeps an answer after the folder is gone")
{
	std::string game;
	FolderMatch match;

	{
		const TempFolder temp;
		game = temp.Game();

		CHECK(match.Home(game, game + "\\"));
	}

	CHECK(match.Home(game, game + "\\"));
}

TEST_CASE("FolderMatch asks again for a new home")
{
	const TempFolder temp;
	FolderMatch match;

	CHECK_FALSE(match.Home(temp.Game(), temp.Tools()));
	CHECK(match.Home(temp.Tools(), temp.Tools() + "\\"));
	CHECK_FALSE(match.Home(temp.Tools(), temp.Game()));
}

#pragma endregion
