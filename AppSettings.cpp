#include "AppSettings.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <system_error>


namespace Loom
{
	namespace
	{
		constexpr const char* PROJECT_ARGUMENT = "--project";
		constexpr const char* OPEN_ARGUMENT = "--open";
		constexpr const char* OPTION_PREFIX = "--";
		constexpr const char* LOCAL_APP_DATA = "LOCALAPPDATA";

		bool IsFolder(const std::string& folder)
		{
			std::error_code code;

			return !folder.empty() && std::filesystem::is_directory(FolderPath(folder), code);
		};

		std::filesystem::path Base()
		{
			const char* const local = std::getenv(LOCAL_APP_DATA);

			return std::filesystem::path(local != nullptr ? local : ".");
		};
	};

	AppSettings::AppSettings(
		const std::string& name,
		const std::string& previousName)
	{
		const std::filesystem::path base = Base();
		m_folder = base / FolderPath(name);

		if (!previousName.empty())
			MigrateAppFolder(base / FolderPath(previousName), m_folder);

		std::error_code code;
		std::filesystem::create_directories(m_folder, code);
	};

	std::string AppSettings::ReadValue(const std::filesystem::path& file)
	{
		std::ifstream stream(file, std::ios::binary);
		std::string value;
		std::getline(stream, value);

		return value;
	};

	void AppSettings::WriteValue(
		const std::filesystem::path& file,
		const std::string& value)
	{
		if (value.empty())
		{
			std::error_code code;
			std::filesystem::remove(file, code);
			return;
		};

		std::ofstream stream(file, std::ios::binary | std::ios::trunc);
		stream << value << '\n';
	};

	std::vector<std::string> AppSettings::ReadList(const std::filesystem::path& file)
	{
		std::ifstream stream(file, std::ios::binary);
		std::vector<std::string> entries;
		std::string line;

		while (std::getline(stream, line))
		{
			if (!line.empty() && line.back() == '\r')
				line.pop_back();

			if (!line.empty())
				entries.push_back(line);
		};

		return entries;
	};

	void AppSettings::WriteList(
		const std::filesystem::path& file,
		const std::vector<std::string>& entries)
	{
		std::ofstream stream(file, std::ios::binary | std::ios::trunc);

		for (const std::string& entry : entries)
			stream << entry << '\n';
	};

	std::vector<std::string> AppSettings::Remember(
		const std::vector<std::string>& folders,
		const std::string& folder)
	{
		std::vector<std::string> remembered{ folder };

		for (const std::string& kept : folders)
			if (remembered.size() < MostFolders && !SameFolder(kept, folder))
				remembered.push_back(kept);

		return remembered;
	};

	std::vector<std::string> AppSettings::Recent(
		const std::vector<std::string>& folders,
		const std::string& current)
	{
		std::vector<std::string> recent;

		for (const std::string& folder : folders)
			if (IsFolder(folder) && !SameFolder(folder, current))
				recent.push_back(folder);

		return recent;
	};

	std::string AppSettings::Project(
		const std::vector<std::string>& arguments,
		const std::vector<std::string>& remembered,
		const std::string& fallback)
	{
		for (size_t index = 0; index + 1 < arguments.size(); ++index)
			if (arguments[index] == PROJECT_ARGUMENT)
				return arguments[index + 1];

		if (!arguments.empty() && arguments.front().rfind(OPTION_PREFIX, 0) != 0 && IsFolder(arguments.front()))
			return arguments.front();

		if (const auto found = std::find_if(remembered.begin(), remembered.end(), IsFolder); found != remembered.end())
			return *found;

		return fallback;
	};

	std::vector<std::string> AppSettings::Opened(const std::vector<std::string>& arguments)
	{
		std::vector<std::string> opened;

		for (size_t index = 0; index + 1 < arguments.size(); ++index)
			if (arguments[index] == OPEN_ARGUMENT)
				opened.push_back(arguments[index + 1]);

		return opened;
	};
};
