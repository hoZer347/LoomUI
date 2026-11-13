#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>


namespace Loom::Testing
{
	struct ProcessEntry final
	{
		DWORD id;
		DWORD parent;
		uint64_t created;
	};

	// Ids are reused and a process keeps a dead parent's id, so a child must be created after its parent and before the snapshot.
	inline std::vector<ProcessEntry> Descendants(
		DWORD root,
		const std::vector<ProcessEntry>& entries,
		uint64_t snapshot_taken)
	{
		const auto start = std::find_if(entries.begin(), entries.end(), [root](const ProcessEntry& entry) { return entry.id == root; });

		if (start == entries.end() || start->created == 0 || start->created > snapshot_taken)
			return { };

		std::vector<ProcessEntry> found{ *start };

		for (size_t at = 0; at < found.size(); at++)
		{
			const ProcessEntry parent = found[at];

			for (const ProcessEntry& entry : entries)
				if (entry.parent == parent.id &&
					entry.created > parent.created &&
					entry.created <= snapshot_taken &&
					std::none_of(found.begin(), found.end(), [&entry](const ProcessEntry& seen) { return seen.id == entry.id; }))
					found.push_back(entry);
		};

		found.erase(found.begin());

		return found;
	};
};
