#include "doctest.h"

#include "Testing/Descendants.h"

#include <algorithm>
#include <cstdint>
#include <vector>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

using namespace Loom::Testing;

#pragma region Helpers

namespace
{
	constexpr DWORD root = 100;
	constexpr DWORD child = 200;
	constexpr DWORD grandchild = 300;
	constexpr DWORD orphan = 400;
	constexpr DWORD orphan_child = 500;
	constexpr DWORD stranger = 600;

	constexpr uint64_t before_root = 10;
	constexpr uint64_t root_created = 20;
	constexpr uint64_t after_root = 30;
	constexpr uint64_t after_child = 40;
	constexpr uint64_t snapshot_taken = 50;
	constexpr uint64_t after_snapshot = 60;

	std::vector<DWORD> Ids(const std::vector<ProcessEntry>& found)
	{
		std::vector<DWORD> ids;

		for (const ProcessEntry& entry : found)
			ids.push_back(entry.id);

		return ids;
	};

	bool Has(const std::vector<DWORD>& found, DWORD process)
	{
		return std::ranges::find(found, process) != found.end();
	};
};

#pragma endregion

TEST_SUITE("Descendants")
{
	TEST_CASE("children and grandchildren created after their parents are descendants")
	{
		const std::vector<ProcessEntry> entries = {
			{ root, stranger, root_created },
			{ child, root, after_root },
			{ grandchild, child, after_child },
			{ stranger, 0, before_root } };

		const std::vector<ProcessEntry> walked = Descendants(root, entries, snapshot_taken);
		const std::vector<DWORD> found = Ids(walked);

		CHECK(found.size() == 2);
		CHECK(Has(found, child));
		CHECK(Has(found, grandchild));
		CHECK_FALSE(Has(found, root));
		CHECK_FALSE(Has(found, stranger));
	};

	TEST_CASE("an orphan of an earlier holder of the root's id is no descendant, nor is anything under it")
	{
		const std::vector<ProcessEntry> entries = {
			{ root, stranger, root_created },
			{ orphan, root, before_root },
			{ orphan_child, orphan, after_root },
			{ child, root, after_root } };

		const std::vector<ProcessEntry> walked = Descendants(root, entries, snapshot_taken);
		const std::vector<DWORD> found = Ids(walked);

		REQUIRE(found == std::vector<DWORD>{ child });
		CHECK(walked.front().created == after_root);
	};

	TEST_CASE("an orphan of an earlier holder of a child's id is no descendant")
	{
		const std::vector<ProcessEntry> entries = {
			{ root, stranger, root_created },
			{ child, root, after_child },
			{ orphan, child, after_root } };

		const std::vector<ProcessEntry> walked = Descendants(root, entries, snapshot_taken);
		const std::vector<DWORD> found = Ids(walked);

		CHECK(found == std::vector<DWORD>{ child });
	};

	TEST_CASE("a root missing from the entries has no descendants")
	{
		const std::vector<ProcessEntry> entries = {
			{ child, root, after_root } };

		CHECK(Descendants(root, entries, snapshot_taken).empty());
	};

	TEST_CASE("a process that took a child's id after the snapshot is no descendant, nor is anything under it")
	{
		const std::vector<ProcessEntry> entries = {
			{ root, stranger, root_created },
			{ child, root, after_root },
			{ orphan, root, after_snapshot },
			{ orphan_child, orphan, after_snapshot } };

		const std::vector<ProcessEntry> walked = Descendants(root, entries, snapshot_taken);

		CHECK(Ids(walked) == std::vector<DWORD>{ child });
	};

	TEST_CASE("a process whose creation time could not be read is no descendant, nor is anything under it")
	{
		constexpr uint64_t unread = 0;

		const std::vector<ProcessEntry> entries = {
			{ root, stranger, root_created },
			{ child, root, unread },
			{ grandchild, child, after_child } };

		CHECK(Descendants(root, entries, snapshot_taken).empty());
	};
};
