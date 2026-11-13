#include "doctest.h"

#include "AppPacing.h"
#include "Placement.h"

#include <filesystem>
#include <fstream>
#include <process.h>

using namespace Loom;

#pragma region Helpers

namespace
{
	struct TempFolder final
	{
		TempFolder() :
			path(std::filesystem::temp_directory_path() / ("Loom UI AppPlacement Tests " + std::to_string(_getpid())))
		{
			std::filesystem::remove_all(path);
			std::filesystem::create_directories(path);
		};

		~TempFolder()
		{
			std::filesystem::remove_all(path);
		};

		const std::filesystem::path path;

		std::filesystem::path File() const
		{
			return path / "window.txt";
		};
	};
}

#pragma endregion

#pragma region Reading and writing

TEST_CASE("a missing placement file reads as none")
{
	const TempFolder folder;

	CHECK_FALSE(Placement::Read(folder.File()).has_value());
}

TEST_CASE("a placement written is read back")
{
	const TempFolder folder;
	const Placement placement{ 100, 50, 900, 1050, false };

	Placement::Write(folder.File(), placement);

	CHECK(Placement::Read(folder.File()) == placement);
}

TEST_CASE("a maximized placement on a monitor left of the primary survives the round trip")
{
	const TempFolder folder;
	const Placement placement{ -1080, 229, -508, 1235, true };

	Placement::Write(folder.File(), placement);

	CHECK(Placement::Read(folder.File()) == placement);
}

TEST_CASE("writing again replaces the old placement")
{
	const TempFolder folder;
	const Placement placement{ 1, 2, 3, 4, false };

	Placement::Write(folder.File(), { -100000, -100000, 100000, 100000, true });
	Placement::Write(folder.File(), placement);

	CHECK(Placement::Read(folder.File()) == placement);
}

TEST_CASE("a truncated placement file reads as none")
{
	const TempFolder folder;

	std::ofstream(folder.File(), std::ios::binary) << "10 20 300";

	CHECK_FALSE(Placement::Read(folder.File()).has_value());
}

TEST_CASE("an empty or inverted rectangle reads as none")
{
	const TempFolder folder;

	std::ofstream(folder.File(), std::ios::binary) << "10 20 10 400 0\n";
	CHECK_FALSE(Placement::Read(folder.File()).has_value());

	std::ofstream(folder.File(), std::ios::binary | std::ios::trunc) << "10 400 300 20 0\n";
	CHECK_FALSE(Placement::Read(folder.File()).has_value());
}

TEST_CASE("text in the placement file reads as none")
{
	const TempFolder folder;

	std::ofstream(folder.File(), std::ios::binary) << "left top right bottom\n";

	CHECK_FALSE(Placement::Read(folder.File()).has_value());
}

#pragma endregion

#pragma region Pacing

TEST_CASE("frames come quickly while busy, held or just after input")
{
	CHECK(FrameWait(true, false, 100) == active_wait);
	CHECK(FrameWait(false, true, 100) == active_wait);
	CHECK(FrameWait(false, false, 0.2) == active_wait);
}

TEST_CASE("frames slow down once idle for a second")
{
	CHECK(FrameWait(false, false, input_linger) == animated_wait);
	CHECK(FrameWait(false, false, 100) == animated_wait);
	CHECK(FrameWait(false, false, 100, true) == idle_wait);
}

#pragma endregion

#pragma region Minimised

TEST_CASE("a minimised window waits long unless work is running, whatever the input")
{
	CHECK(FrameWait(false, true, 0, true) == idle_wait);
	CHECK(FrameWait(true, false, 100, true) == active_wait);
}

#pragma endregion
