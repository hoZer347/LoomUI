#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>


namespace Loom::Files
{
	/**
	* Loom::Files
	* - Whole-file reads and writes that never throw, and a write that moves
	*   the finished file into place, so a crash leaves the last good one
	* - A cache of the files of a folder, parsed once and parsed again only
	*   when one changes
	* - A lock file Windows gives up however the process ends
	*/

	// A file that cannot be opened reads as empty.
	std::string ReadWhole(const std::filesystem::path& file);

	// Each line without its line ending, the last one even with none. A file that cannot be opened has no lines.
	std::vector<std::string> ReadLines(const std::filesystem::path& file);

	// Replaces the file's contents, held open alone while written. False when it could not be written whole.
	bool WriteWhole(
		const std::filesystem::path& file,
		const std::string& text,
		bool flush = true);

	struct AtomicWrite final
	{
		// Written beside the file as "<file>.<process id>.tmp" when empty.
		std::filesystem::path partial{ };
		bool flush = true;

		// Moving the partial into place is tried this often, a millisecond apart, as readers may hold the file.
		int attempts = 1;
	};

	// Creates the file's folder, writes a partial file and moves it over the file. The partial does not outlive a failure.
	bool WriteAtomic(
		const std::filesystem::path& file,
		const std::string& text,
		const AtomicWrite& how = { });

	int64_t EpochSeconds(std::chrono::system_clock::time_point time);

	/**
	* Loom::Files::LockFile
	* - Holds a file open, and deletes it on close, for as long as it lives
	* - A file already held by another lock leaves this one false
	*/
	struct LockFile final
	{
		LockFile() = default;
		explicit LockFile(const std::filesystem::path& file);
		LockFile(const LockFile&) = delete;
		LockFile& operator=(const LockFile&) = delete;
		LockFile(LockFile&& other) noexcept;
		LockFile& operator=(LockFile&& other) noexcept;
		~LockFile();

		explicit operator bool() const { return m_file != nullptr; };

	private:
		void* m_file = nullptr;
	};

	/**
	* Loom::Files::FolderCache
	* - Lists the files of one extension in each folder it is asked about, and
	*   keeps what the caller parsed from each, until the file changes size or
	*   time
	* - A listing taken a moment ago may be reused when the caller shares
	* - A file written within the settle time is read again next scan, as it may
	*   still be growing; one that is empty keeps what was parsed before
	* - Safe to use from any thread
	*/
	template<typename Value>
	class FolderCache final
	{
	public:
		// Gets the file and the text read from it. Nothing means the file holds no value.
		using Parse = std::function<std::optional<Value>(const std::filesystem::path&, const std::string&)>;

		// Gets the file's name without its extension.
		using Wanted = std::function<bool(const std::string&)>;

		FolderCache(
			std::string extension,
			std::chrono::milliseconds shareAge,
			std::chrono::milliseconds settle) :
			m_extension(std::move(extension)),
			m_shareAge(shareAge),
			m_settle(settle)
		{
		};

		FolderCache(const FolderCache&) = delete;
		FolderCache& operator=(const FolderCache&) = delete;

		std::vector<Value> Scan(
			const std::filesystem::path& folder,
			const Wanted& wanted,
			const Parse& parse,
			bool share)
		{
			const std::scoped_lock lock(m_mutex);
			Folder& scan = m_folders[folder];

			if (!share || !scan.listed || std::chrono::steady_clock::now() - scan.taken > m_shareAge)
				Relist(folder, scan);

			std::vector<Value> values;

			for (const Listed& each : scan.listing)
			{
				if (!wanted(each.file.stem().string()))
					continue;

				Parsed& parsed = scan.parsed[each.file];

				if (!parsed.known || parsed.written != each.written || parsed.size != each.size)
				{
					const std::string text = ReadWhole(each.file);
					const bool readable = !text.empty();
					std::optional<Value> fresh = parse(each.file, text);

					if (readable || !parsed.value)
						parsed.value = std::move(fresh);

					parsed.known = readable && std::filesystem::file_time_type::clock::now() - each.written > m_settle;
					parsed.written = each.written;
					parsed.size = each.size;
				};

				if (parsed.value)
					values.push_back(*parsed.value);
			};

			return values;
		};

		// The next shared scan lists the folder again.
		void Forget(const std::filesystem::path& folder)
		{
			const std::scoped_lock lock(m_mutex);

			if (const auto found = m_folders.find(folder); found != m_folders.end())
				found->second.listed = false;
		};

		// Tells it the file was just written: its parse is dropped, and a file the listing did not have makes the next shared scan list again.
		void Written(
			const std::filesystem::path& folder,
			const std::filesystem::path& file)
		{
			const std::scoped_lock lock(m_mutex);
			const auto found = m_folders.find(folder);

			if (found == m_folders.end() || !found->second.listed)
				return;

			Folder& scan = found->second;
			bool present = false;

			for (const Listed& each : scan.listing)
				present = present || each.file == file;

			scan.parsed.erase(file);

			if (!present)
				scan.listed = false;
		};

	private:
		struct Listed final
		{
			std::filesystem::path file;
			std::filesystem::file_time_type written;
			uintmax_t size = 0;
		};

		struct Parsed final
		{
			std::filesystem::file_time_type written;
			uintmax_t size = 0;
			bool known = false;
			std::optional<Value> value;
		};

		struct Folder final
		{
			std::chrono::steady_clock::time_point taken{ };
			bool listed = false;
			std::vector<Listed> listing;
			std::map<std::filesystem::path, Parsed> parsed;
		};

		void Relist(
			const std::filesystem::path& folder,
			Folder& scan) const
		{
			std::error_code code;

			scan.listing.clear();

			for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(folder, code))
				if (entry.path().extension() == m_extension)
					scan.listing.push_back({ entry.path(), entry.last_write_time(code), entry.file_size(code) });

			std::set<std::filesystem::path> present;

			for (const Listed& each : scan.listing)
				present.insert(each.file);

			std::erase_if(scan.parsed, [&present](const auto& each) { return !present.contains(each.first); });

			scan.taken = std::chrono::steady_clock::now();
			scan.listed = true;
		};

		std::string m_extension;
		std::chrono::milliseconds m_shareAge;
		std::chrono::milliseconds m_settle;
		std::mutex m_mutex;
		std::map<std::filesystem::path, Folder> m_folders{ };
	};
};
