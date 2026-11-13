#pragma once

#include "Folders.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>


namespace Loom
{
	/**
	* Loom::AppSettings
	* - An app's own folder under %LOCALAPPDATA%, made when it is not there, holding the app's small
	*   settings files: one value as a file's first line, or a list as one entry a line
	* - A folder an earlier version of the app used is copied to the new one once
	* - The recent folders list, and the project an app opens on, from its arguments
	*/
	struct AppSettings final
	{
		static constexpr size_t MostFolders = 10;

		explicit AppSettings(
			const std::string& name,
			const std::string& previousName = { });

		const std::filesystem::path& Folder() const { return m_folder; };

		std::filesystem::path File(const std::string& name) const { return m_folder / name; };

		// The first line of the file, or nothing when there is none.
		static std::string ReadValue(const std::filesystem::path& file);

		// An empty value deletes the file.
		static void WriteValue(
			const std::filesystem::path& file,
			const std::string& value);

		static std::vector<std::string> ReadList(const std::filesystem::path& file);

		static void WriteList(
			const std::filesystem::path& file,
			const std::vector<std::string>& entries);

		// folder first, then the others in order without it, up to MostFolders.
		static std::vector<std::string> Remember(
			const std::vector<std::string>& folders,
			const std::string& folder);

		// The remembered folders that exist and are not the current one.
		static std::vector<std::string> Recent(
			const std::vector<std::string>& folders,
			const std::string& current);

		// --project, else the first argument when it is a folder, else the first remembered folder that exists, else the fallback.
		static std::string Project(
			const std::vector<std::string>& arguments,
			const std::vector<std::string>& remembered,
			const std::string& fallback);

		// The folder after each --open: further tabs to open beside the project's, in order.
		static std::vector<std::string> Opened(const std::vector<std::string>& arguments);

	private:
		std::filesystem::path m_folder;
	};
};
