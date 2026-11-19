#include "doctest.h"

#include "KeyedStore.h"
#include "Testing/TempFolder.h"

#include <chrono>
#include <filesystem>
#include <string>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

using Loom::Testing::ReadBytes;
using Loom::Testing::TempFolder;
using Loom::Testing::WriteBytes;

using namespace Loom;

#pragma region Helpers

namespace
{
	constexpr const char* store_name = "store.json";
	constexpr std::chrono::minutes past_delay{ 1 };

	std::chrono::steady_clock::time_point Later()
	{
		return std::chrono::steady_clock::now() + past_delay;
	};

	std::string PartialOf(const std::filesystem::path& file)
	{
		return file.filename().string() + "." + std::to_string(GetCurrentProcessId()) + ".tmp";
	};

	HANDLE Hold(const std::filesystem::path& file)
	{
		return CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	};
};

#pragma endregion

TEST_SUITE("Loom KeyedStore")
{
	TEST_CASE("a store is saved as tab indented JSON in key order")
	{
		const TempFolder scratch;
		const std::filesystem::path file = scratch.folder / store_name;

		{
			KeyedStore store(file);
			store.Set("b", "bee");
			store.Set("a", "ay");
		};

		CHECK(ReadBytes(file) == "{\n\t\"a\": \"ay\",\n\t\"b\": \"bee\"\n}");
	};

	TEST_CASE("erasing the last key saves an empty object")
	{
		const TempFolder scratch;
		const std::filesystem::path file = scratch.folder / store_name;

		WriteBytes(file, "{\"a\": \"ay\"}");

		{
			KeyedStore store(file);
			store.Set("a", "");
		};

		CHECK(ReadBytes(file) == "{}");
	};

	TEST_CASE("loading keeps only keys with non-empty string values")
	{
		const TempFolder scratch;
		const std::filesystem::path file = scratch.folder / store_name;

		WriteBytes(file, "{\"text\": \"kept\", \"empty\": \"\", \"number\": 3, \"list\": [\"x\"], \"nothing\": null}");

		const KeyedStore store(file);

		CHECK(store.Get("text") == "kept");
		CHECK(store.Get("empty").empty());
		CHECK(store.Get("number").empty());
		CHECK(store.Get("list").empty());
		CHECK(store.Get("nothing").empty());
	};

	TEST_CASE("a file holding JSON that is not an object loads as none")
	{
		const TempFolder scratch;
		const std::filesystem::path file = scratch.folder / store_name;

		WriteBytes(file, "[\"a\", \"b\"]");

		const KeyedStore store(file);

		CHECK(store.Get("0").empty());
	};

	TEST_CASE("saving creates the file's folder and leaves no partial")
	{
		const TempFolder scratch;
		const std::filesystem::path folder = scratch.folder / "nested" / "deeper";
		const std::filesystem::path file = folder / store_name;

		KeyedStore store(file);
		store.Set("a", "ay");
		store.Flush();

		CHECK(std::filesystem::exists(file));
		CHECK_FALSE(std::filesystem::exists(folder / PartialOf(file)));
	};

	TEST_CASE("text that is not UTF-8 is saved with replacement characters rather than lost")
	{
		const TempFolder scratch;
		const std::filesystem::path file = scratch.folder / store_name;

		{
			KeyedStore store(file);
			store.Set("a", "bad \xFF byte");
		};

		const KeyedStore reloaded(file);

		CHECK(reloaded.Get("a") == "bad \xEF\xBF\xBD byte");
	};

	TEST_CASE("a save blocked by a held file stays owed and lands once the file is let go")
	{
		const TempFolder scratch;
		const std::filesystem::path file = scratch.folder / store_name;

		WriteBytes(file, "{}");

		KeyedStore store(file);
		store.Set("a", "ay");

		const HANDLE held = Hold(file);
		REQUIRE(held != INVALID_HANDLE_VALUE);

		store.Update(Later());
		CloseHandle(held);

		CHECK(ReadBytes(file) == "{}");
		CHECK_FALSE(std::filesystem::exists(scratch.folder / PartialOf(file)));

		store.Flush();

		CHECK(ReadBytes(file) == "{\n\t\"a\": \"ay\"\n}");
	};

	TEST_CASE("a failed update waits a full delay before trying again")
	{
		const TempFolder scratch;
		const std::filesystem::path file = scratch.folder / store_name;

		WriteBytes(file, "{}");

		KeyedStore store(file);
		store.Set("a", "ay");

		const HANDLE held = Hold(file);
		REQUIRE(held != INVALID_HANDLE_VALUE);

		const auto failed = Later();
		store.Update(failed);
		CloseHandle(held);

		store.Update(failed);

		CHECK(ReadBytes(file) == "{}");

		store.Update(failed + past_delay);

		CHECK(ReadBytes(file) == "{\n\t\"a\": \"ay\"\n}");
	};
}
