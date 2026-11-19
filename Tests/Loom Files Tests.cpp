#include "doctest.h"

#include "Files.h"
#include "Testing/TempFolder.h"

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

using Loom::Testing::ReadBytes;
using Loom::Testing::TempFolder;
using Loom::Testing::WriteBytes;

using namespace Loom;
using namespace std::string_view_literals;

#pragma region Helpers

namespace
{
	constexpr std::string_view binary_text = "one\r\ntwo\n\0three\x80\xFF"sv;
	constexpr const char* record_extension = ".json";
	constexpr std::chrono::milliseconds share_age{ 60000 };
	constexpr std::chrono::milliseconds no_share{ 0 };
	constexpr std::chrono::milliseconds no_settle{ 0 };
	constexpr std::chrono::milliseconds long_settle{ 600000 };
	constexpr std::chrono::milliseconds release_after{ 20 };
	constexpr std::chrono::minutes aged{ 10 };
	constexpr int patient_attempts = 2000;

	std::string PartialOf(const std::filesystem::path& file)
	{
		return file.filename().string() + "." + std::to_string(GetCurrentProcessId()) + ".tmp";
	};

	// Holds the file open without delete sharing, so nothing can be moved over it.
	HANDLE Hold(const std::filesystem::path& file)
	{
		return CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	};

	bool Takes(const std::filesystem::path& file)
	{
		return static_cast<bool>(Files::LockFile(file));
	};

	void Age(const std::filesystem::path& file)
	{
		std::filesystem::last_write_time(file, std::filesystem::file_time_type::clock::now() - aged);
	};

	void WriteAged(
		const std::filesystem::path& file,
		const std::string& text)
	{
		WriteBytes(file, text);
		Age(file);
	};

	struct Counted final
	{
		int calls = 0;

		Files::FolderCache<std::string>::Parse Parse()
		{
			return [this](const std::filesystem::path&, const std::string& text) -> std::optional<std::string>
			{
				calls++;

				if (text == "none")
					return std::nullopt;

				return text;
			};
		};
	};

	bool All(const std::string&)
	{
		return true;
	};

	using Cache = Files::FolderCache<std::string>;
};

#pragma endregion

TEST_SUITE("Loom Files")
{
	#pragma region Reading

	TEST_CASE("read whole of a missing file is empty")
	{
		const TempFolder scratch;

		CHECK(Files::ReadWhole(scratch.folder / "missing.txt").empty());
	};

	TEST_CASE("read whole keeps every byte, line endings and nulls included")
	{
		const TempFolder scratch;
		const std::filesystem::path file = scratch.folder / "binary.bin";
		const std::string text(binary_text);

		WriteBytes(file, text);

		CHECK(Files::ReadWhole(file) == text);
	};

	TEST_CASE("read lines drops each line ending and keeps a last line with none")
	{
		const TempFolder scratch;
		const std::filesystem::path file = scratch.folder / "lines.txt";

		WriteBytes(file, "first\r\nsecond\n\r\nlast");

		CHECK(Files::ReadLines(file) == std::vector<std::string>{ "first", "second", "", "last" });
	};

	TEST_CASE("read lines of a file ending in a line ending has no extra empty line")
	{
		const TempFolder scratch;
		const std::filesystem::path file = scratch.folder / "lines.txt";

		WriteBytes(file, "only\r\n");

		CHECK(Files::ReadLines(file) == std::vector<std::string>{ "only" });
	};

	TEST_CASE("read lines of a missing or empty file has no lines")
	{
		const TempFolder scratch;
		const std::filesystem::path empty = scratch.folder / "empty.txt";

		WriteBytes(empty, "");

		CHECK(Files::ReadLines(scratch.folder / "missing.txt").empty());
		CHECK(Files::ReadLines(empty).empty());
	};

	#pragma endregion

	#pragma region Writing

	TEST_CASE("write whole replaces a longer file with exactly the text, flushed or not")
	{
		const TempFolder scratch;
		const std::filesystem::path file = scratch.folder / "whole.txt";

		WriteBytes(file, "a much longer earlier text");

		CHECK(Files::WriteWhole(file, "short"));
		CHECK(ReadBytes(file) == "short");

		CHECK(Files::WriteWhole(file, "unflushed", false));
		CHECK(ReadBytes(file) == "unflushed");
	};

	TEST_CASE("write whole into a missing folder fails")
	{
		const TempFolder scratch;

		CHECK_FALSE(Files::WriteWhole(scratch.folder / "missing" / "whole.txt", "text"));
	};

	TEST_CASE("write atomic creates the folders, writes the text and leaves no partial")
	{
		const TempFolder scratch;
		const std::filesystem::path folder = scratch.folder / "deep" / "er";
		const std::filesystem::path file = folder / "atomic.json";
		const std::string text(binary_text);

		REQUIRE(Files::WriteAtomic(file, text));

		CHECK(ReadBytes(file) == text);
		CHECK_FALSE(std::filesystem::exists(folder / PartialOf(file)));
	};

	TEST_CASE("write atomic makes the folder unless told not to")
	{
		const TempFolder scratch;
		const std::filesystem::path made = scratch.folder / "made" / "atomic.json";
		const std::filesystem::path missing = scratch.folder / "missing" / "atomic.json";

		CHECK(Files::WriteAtomic(made, "text"));
		CHECK(ReadBytes(made) == "text");

		CHECK_FALSE(Files::WriteAtomic(missing, "text", { .makeFolder = false }));
		CHECK_FALSE(std::filesystem::exists(missing.parent_path()));
	};

	TEST_CASE("write atomic replaces an existing file")
	{
		const TempFolder scratch;
		const std::filesystem::path file = scratch.folder / "atomic.json";

		WriteBytes(file, "old and longer");

		REQUIRE(Files::WriteAtomic(file, "new"));

		CHECK(ReadBytes(file) == "new");
	};

	TEST_CASE("write atomic writes through the partial it is given")
	{
		const TempFolder scratch;
		const std::filesystem::path file = scratch.folder / "atomic.json";
		const std::filesystem::path partial = scratch.folder / "named.partial";

		REQUIRE(Files::WriteAtomic(file, "text", { .partial = partial, .flush = false }));

		CHECK(ReadBytes(file) == "text");
		CHECK_FALSE(std::filesystem::exists(partial));
		CHECK_FALSE(std::filesystem::exists(scratch.folder / PartialOf(file)));
	};

	TEST_CASE("write atomic over a held file fails, keeps the old text and removes the partial")
	{
		const TempFolder scratch;
		const std::filesystem::path file = scratch.folder / "atomic.json";

		WriteBytes(file, "old");

		const HANDLE held = Hold(file);
		REQUIRE(held != INVALID_HANDLE_VALUE);

		const bool written = Files::WriteAtomic(file, "new");
		CloseHandle(held);

		CHECK_FALSE(written);
		CHECK(ReadBytes(file) == "old");
		CHECK_FALSE(std::filesystem::exists(scratch.folder / PartialOf(file)));
	};

	TEST_CASE("write atomic retries until a held file is let go")
	{
		const TempFolder scratch;
		const std::filesystem::path file = scratch.folder / "atomic.json";

		WriteBytes(file, "old");

		const HANDLE held = Hold(file);
		REQUIRE(held != INVALID_HANDLE_VALUE);

		std::thread release([held]()
		{
			std::this_thread::sleep_for(release_after);
			CloseHandle(held);
		});

		const bool written = Files::WriteAtomic(file, "new", { .attempts = patient_attempts });
		release.join();

		CHECK(written);
		CHECK(ReadBytes(file) == "new");
	};

	TEST_CASE("write atomic fails without touching the file when its partial cannot be written")
	{
		const TempFolder scratch;
		const std::filesystem::path file = scratch.folder / "atomic.json";

		WriteBytes(file, "old");

		CHECK_FALSE(Files::WriteAtomic(file, "new", { .partial = scratch.folder / "missing" / "named.partial" }));
		CHECK(ReadBytes(file) == "old");
	};

	#pragma endregion

	#pragma region EpochSeconds

	TEST_CASE("epoch seconds counts whole seconds since 1970")
	{
		using namespace std::chrono;

		const system_clock::time_point epoch{ };
		const system_clock::time_point known = sys_days(year(2025) / January / 31) + hours(23) + minutes(59) + seconds(59) + milliseconds(999);

		CHECK(Files::EpochSeconds(epoch) == 0);
		CHECK(Files::EpochSeconds(known) == 1738367999);
	};

	TEST_CASE("epoch milliseconds counts whole milliseconds since 1970")
	{
		using namespace std::chrono;

		const system_clock::time_point epoch{ };
		const system_clock::time_point known = sys_days(year(2025) / January / 31) + hours(23) + minutes(59) + seconds(59) + milliseconds(999);

		CHECK(Files::EpochMilliseconds(epoch) == 0);
		CHECK(Files::EpochMilliseconds(known) == 1738367999999);
	};

	#pragma endregion

	#pragma region LockFile

	TEST_CASE("an empty lock holds nothing")
	{
		const Files::LockFile lock;

		CHECK_FALSE(static_cast<bool>(lock));
	};

	TEST_CASE("a lock creates its file, refuses a second lock and deletes the file when it ends")
	{
		const TempFolder scratch;
		const std::filesystem::path file = scratch.folder / "task.lock";

		{
			const Files::LockFile first(file);

			REQUIRE(static_cast<bool>(first));
			CHECK(std::filesystem::exists(file));

			const Files::LockFile second(file);

			CHECK_FALSE(static_cast<bool>(second));
		};

		CHECK_FALSE(std::filesystem::exists(file));

		const Files::LockFile again(file);

		CHECK(static_cast<bool>(again));
	};

	TEST_CASE("a lock in a missing folder holds nothing")
	{
		const TempFolder scratch;
		const Files::LockFile lock(scratch.folder / "missing" / "task.lock");

		CHECK_FALSE(static_cast<bool>(lock));
	};

	TEST_CASE("a moved lock keeps the file held until its new owner ends")
	{
		const TempFolder scratch;
		const std::filesystem::path file = scratch.folder / "task.lock";

		{
			Files::LockFile first(file);
			const Files::LockFile moved(std::move(first));

			CHECK_FALSE(static_cast<bool>(first));
			CHECK(static_cast<bool>(moved));
			CHECK(std::filesystem::exists(file));
		};

		CHECK_FALSE(std::filesystem::exists(file));
	};

	TEST_CASE("move assigning swaps the locks so each file is still let go")
	{
		const TempFolder scratch;
		const std::filesystem::path one = scratch.folder / "one.lock";
		const std::filesystem::path two = scratch.folder / "two.lock";

		{
			Files::LockFile first(one);
			Files::LockFile second(two);

			first = std::move(second);

			CHECK(static_cast<bool>(first));
			CHECK(static_cast<bool>(second));
			CHECK_FALSE(Takes(one));
			CHECK_FALSE(Takes(two));
		};

		CHECK_FALSE(std::filesystem::exists(one));
		CHECK_FALSE(std::filesystem::exists(two));
	};

	#pragma endregion

	#pragma region FolderCache

	TEST_CASE("a folder cache of a missing folder has nothing")
	{
		const TempFolder scratch;
		Cache cache(record_extension, share_age, no_settle);
		Counted counted;

		CHECK(cache.Scan(scratch.folder / "missing", All, counted.Parse(), false).empty());
		CHECK(counted.calls == 0);
	};

	TEST_CASE("a folder cache lists only its extension, in name order, and only wanted names")
	{
		const TempFolder scratch;
		Cache cache(record_extension, share_age, no_settle);
		Counted counted;

		WriteAged(scratch.folder / "b.json", "bee");
		WriteAged(scratch.folder / "a.json", "ay");
		WriteAged(scratch.folder / "c.txt", "sea");
		WriteAged(scratch.folder / "skip.json", "skipped");

		const auto wanted = [](const std::string& name) { return name != "skip"; };

		CHECK(cache.Scan(scratch.folder, wanted, counted.Parse(), false) == std::vector<std::string>{ "ay", "bee" });
		CHECK(counted.calls == 2);
	};

	TEST_CASE("a folder cache passes the file and its text to the parse")
	{
		const TempFolder scratch;
		Cache cache(record_extension, share_age, no_settle);
		const std::filesystem::path file = scratch.folder / "one.json";
		std::filesystem::path seen;
		std::string read;

		WriteAged(file, "text");

		const auto parse = [&seen, &read](const std::filesystem::path& path, const std::string& text) -> std::optional<std::string>
		{
			seen = path;
			read = text;

			return text;
		};

		cache.Scan(scratch.folder, All, parse, false);

		CHECK(seen == file);
		CHECK(read == "text");
	};

	TEST_CASE("a folder cache parses a settled file once until it changes")
	{
		const TempFolder scratch;
		Cache cache(record_extension, share_age, no_settle);
		Counted counted;
		const std::filesystem::path file = scratch.folder / "one.json";

		WriteAged(file, "first");

		CHECK(cache.Scan(scratch.folder, All, counted.Parse(), false) == std::vector<std::string>{ "first" });
		CHECK(cache.Scan(scratch.folder, All, counted.Parse(), false) == std::vector<std::string>{ "first" });
		CHECK(counted.calls == 1);

		WriteAged(file, "second, longer");

		CHECK(cache.Scan(scratch.folder, All, counted.Parse(), false) == std::vector<std::string>{ "second, longer" });
		CHECK(counted.calls == 2);
	};

	TEST_CASE("a folder cache parses a file again while it is within the settle time")
	{
		const TempFolder scratch;
		Cache cache(record_extension, share_age, long_settle);
		Counted counted;

		WriteBytes(scratch.folder / "one.json", "fresh");

		cache.Scan(scratch.folder, All, counted.Parse(), false);
		cache.Scan(scratch.folder, All, counted.Parse(), false);

		CHECK(counted.calls == 2);
	};

	TEST_CASE("a folder cache keeps the last value when the file reads empty")
	{
		const TempFolder scratch;
		Cache cache(record_extension, share_age, no_settle);
		Counted counted;
		const std::filesystem::path file = scratch.folder / "one.json";

		WriteAged(file, "kept");
		cache.Scan(scratch.folder, All, counted.Parse(), false);

		WriteAged(file, "");

		CHECK(cache.Scan(scratch.folder, All, counted.Parse(), false) == std::vector<std::string>{ "kept" });
		CHECK(cache.Scan(scratch.folder, All, counted.Parse(), false) == std::vector<std::string>{ "kept" });
		CHECK(counted.calls == 3);
	};

	TEST_CASE("a folder cache leaves out a file that parses to nothing")
	{
		const TempFolder scratch;
		Cache cache(record_extension, share_age, no_settle);
		Counted counted;

		WriteAged(scratch.folder / "one.json", "none");

		CHECK(cache.Scan(scratch.folder, All, counted.Parse(), false).empty());
	};

	TEST_CASE("a folder cache drops a deleted file")
	{
		const TempFolder scratch;
		Cache cache(record_extension, share_age, no_settle);
		Counted counted;
		const std::filesystem::path file = scratch.folder / "one.json";

		WriteAged(file, "gone soon");
		cache.Scan(scratch.folder, All, counted.Parse(), false);

		std::filesystem::remove(file);

		CHECK(cache.Scan(scratch.folder, All, counted.Parse(), false).empty());
	};

	TEST_CASE("a shared scan reuses a fresh listing and a forgotten folder is listed again")
	{
		const TempFolder scratch;
		Cache cache(record_extension, share_age, no_settle);
		Counted counted;

		WriteAged(scratch.folder / "a.json", "ay");
		cache.Scan(scratch.folder, All, counted.Parse(), true);

		WriteAged(scratch.folder / "b.json", "bee");

		CHECK(cache.Scan(scratch.folder, All, counted.Parse(), true) == std::vector<std::string>{ "ay" });
		CHECK(cache.Scan(scratch.folder, All, counted.Parse(), false) == std::vector<std::string>{ "ay", "bee" });

		WriteAged(scratch.folder / "c.json", "sea");
		cache.Forget(scratch.folder);

		CHECK(cache.Scan(scratch.folder, All, counted.Parse(), true) == std::vector<std::string>{ "ay", "bee", "sea" });
	};

	TEST_CASE("a shared scan lists again once the listing is older than the share age")
	{
		const TempFolder scratch;
		Cache cache(record_extension, no_share, no_settle);
		Counted counted;

		WriteAged(scratch.folder / "a.json", "ay");
		cache.Scan(scratch.folder, All, counted.Parse(), true);

		WriteAged(scratch.folder / "b.json", "bee");
		std::this_thread::sleep_for(release_after);

		CHECK(cache.Scan(scratch.folder, All, counted.Parse(), true) == std::vector<std::string>{ "ay", "bee" });
	};

	TEST_CASE("a written file the listing lacks makes the next shared scan list again")
	{
		const TempFolder scratch;
		Cache cache(record_extension, share_age, no_settle);
		Counted counted;
		const std::filesystem::path added = scratch.folder / "b.json";

		WriteAged(scratch.folder / "a.json", "ay");
		cache.Scan(scratch.folder, All, counted.Parse(), true);

		WriteAged(added, "bee");
		cache.Written(scratch.folder, added);

		CHECK(cache.Scan(scratch.folder, All, counted.Parse(), true) == std::vector<std::string>{ "ay", "bee" });
	};

	TEST_CASE("a written file the listing has is parsed again on the next shared scan")
	{
		const TempFolder scratch;
		Cache cache(record_extension, share_age, no_settle);
		Counted counted;
		const std::filesystem::path file = scratch.folder / "a.json";

		WriteAged(file, "ay");
		cache.Scan(scratch.folder, All, counted.Parse(), true);

		cache.Written(scratch.folder, file);
		cache.Scan(scratch.folder, All, counted.Parse(), true);

		CHECK(counted.calls == 2);
	};

	TEST_CASE("each folder has a cache of its own")
	{
		const TempFolder one;
		const TempFolder two;
		Cache cache(record_extension, share_age, no_settle);
		Counted counted;

		WriteAged(one.folder / "a.json", "one");
		WriteAged(two.folder / "a.json", "two");

		CHECK(cache.Scan(one.folder, All, counted.Parse(), true) == std::vector<std::string>{ "one" });
		CHECK(cache.Scan(two.folder, All, counted.Parse(), true) == std::vector<std::string>{ "two" });
	};

	#pragma endregion
}
