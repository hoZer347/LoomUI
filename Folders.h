#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <vector>


namespace Loom
{
	inline std::filesystem::path FolderPath(const std::string& folder)
	{
		return std::filesystem::path(std::u8string_view((const char8_t*)folder.data(), folder.size()));
	};

	inline std::string FolderText(const std::filesystem::path& path)
	{
		const std::u8string text = path.u8string();

		return std::string((const char*)text.data(), text.size());
	};

	// Whether folders are one home folder, each asked of the disk once until the home changes.
	struct FolderMatch final
	{
		bool Home(
			const std::string& home,
			const std::string& folder);

	private:
		std::string m_home;
		std::unordered_map<std::string, bool> m_known;
	};

	inline bool SameFolder(
		const std::string& one,
		const std::string& other)
	{
		std::error_code code;

		return one == other || std::filesystem::equivalent(FolderPath(one), FolderPath(other), code);
	};

	// Copies oldFolder to newFolder once: only when oldFolder exists and newFolder does not.
	inline void MigrateAppFolder(
		const std::filesystem::path& oldFolder,
		const std::filesystem::path& newFolder)
	{
		std::error_code code;

		if (std::filesystem::exists(newFolder, code) || !std::filesystem::is_directory(oldFolder, code))
			return;

		std::filesystem::create_directories(newFolder.parent_path(), code);
		std::filesystem::copy(oldFolder, newFolder, std::filesystem::copy_options::recursive, code);
	};

	// The folder's last name, or the whole folder when it has none, as a drive does.
	inline std::string FolderTitle(const std::string& folder)
	{
		const std::filesystem::path path = FolderPath(folder);
		const std::filesystem::path name = path.filename().empty() && path.has_relative_path() ? path.parent_path().filename() : path.filename();
		const std::u8string text = (name.empty() ? path : name).u8string();

		return std::string((const char*)text.data(), text.size());
	};

	// The nearest folder at or above inside that holds a .git entry.
	inline std::optional<std::filesystem::path> RepoRoot(const std::filesystem::path& inside)
	{
		constexpr const char* git_entry = ".git";
		std::error_code code;

		for (std::filesystem::path folder = inside; !folder.empty(); folder = folder.parent_path())
		{
			if (std::filesystem::exists(folder / git_entry, code))
				return folder;

			if (folder == folder.parent_path())
				break;
		};

		return std::nullopt;
	};

	// The folder of the exe or DLL this code is built into.
	std::filesystem::path ModuleFolder();

	inline std::optional<size_t> FindFolder(
		const std::vector<std::string>& folders,
		const std::string& folder)
	{
		for (size_t index = 0; index < folders.size(); ++index)
			if (SameFolder(folders[index], folder))
				return index;

		return std::nullopt;
	};

	inline bool FolderMatch::Home(
		const std::string& home,
		const std::string& folder)
	{
		if (m_home != home)
		{
			m_home = home;
			m_known.clear();
		};

		const auto [known, added] = m_known.try_emplace(folder, false);

		if (added)
			known->second = SameFolder(home, folder);

		return known->second;
	};
};
