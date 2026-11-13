#pragma once

#include "Cleanup.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <system_error>


namespace Loom::Testing
{
	/**
	* Loom::Testing::TempFolder
	* - A folder of its own under the temp folder, named after the prefix and
	*   a random number, kept for the exit cleanup and removed when it goes
	*   out of scope
	*/
	struct TempFolder final
	{
		explicit TempFolder(const std::string& prefix = "Loom UI Test ") :
			folder(std::filesystem::temp_directory_path() / (prefix + std::to_string(std::random_device{ }())))
		{
			KeepFolder(folder);
			std::filesystem::create_directories(folder);
		};

		~TempFolder()
		{
			std::error_code ignored;
			std::filesystem::remove_all(folder, ignored);
		};

		TempFolder(const TempFolder&) = delete;
		TempFolder& operator=(const TempFolder&) = delete;

		const std::filesystem::path folder;
	};

	inline void WriteBytes(
		const std::filesystem::path& file,
		const std::string& text)
	{
		std::ofstream stream(file, std::ios::binary | std::ios::trunc);
		stream << text;
	};

	inline std::string ReadBytes(const std::filesystem::path& file)
	{
		std::ifstream stream(file, std::ios::binary);

		return { std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
	};
};
