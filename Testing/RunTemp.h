#pragma once

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace Loom::Testing
{
	inline constexpr std::string_view run_temp_prefix = "cct";

	inline std::filesystem::path RunTempFolder(
		const std::filesystem::path& base,
		DWORD process)
	{
		return base / (std::string(run_temp_prefix) + std::to_string(process));
	};

	namespace RunTempDetail
	{
		inline constexpr std::string_view legacy_prefixes[] = { "Corrin Code Tests ", "Corrin Code Lead Tests " };
		inline constexpr std::chrono::hours legacy_age(1);

		inline bool IsLive(DWORD process)
		{
			const HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process);

			if (handle == nullptr)
				return GetLastError() == ERROR_ACCESS_DENIED;

			DWORD code = 0;
			const bool live = GetExitCodeProcess(handle, &code) && code == STILL_ACTIVE;
			CloseHandle(handle);

			return live;
		};

		inline std::optional<DWORD> ProcessOf(const std::string& name)
		{
			if (name.size() <= run_temp_prefix.size() || name.compare(0, run_temp_prefix.size(), run_temp_prefix) != 0)
				return std::nullopt;

			const std::string digits = name.substr(run_temp_prefix.size());

			if (!std::all_of(digits.begin(), digits.end(), [](char character) { return character >= '0' && character <= '9'; }))
				return std::nullopt;

			return (DWORD)std::strtoul(digits.c_str(), nullptr, 10);
		};

		inline bool IsOldLegacy(
			const std::filesystem::directory_entry& entry,
			const std::string& name)
		{
			const bool legacy = std::any_of(
				std::begin(legacy_prefixes),
				std::end(legacy_prefixes),
				[&name](std::string_view prefix) { return name.compare(0, prefix.size(), prefix) == 0; });

			if (!legacy)
				return false;

			std::error_code error;
			const auto written = entry.last_write_time(error);

			return !error && std::filesystem::file_time_type::clock::now() - written > legacy_age;
		};
	};

	inline void SweepRunTemps(const std::filesystem::path& base)
	{
		std::error_code error;
		std::filesystem::directory_iterator entries(base, error);

		if (error)
			return;

		for (const std::filesystem::directory_entry& entry : entries)
		{
			std::error_code ignored;

			if (!entry.is_directory(ignored))
				continue;

			const std::string name = entry.path().filename().string();
			const std::optional<DWORD> process = RunTempDetail::ProcessOf(name);

			if (process ? !RunTempDetail::IsLive(*process) : RunTempDetail::IsOldLegacy(entry, name))
				std::filesystem::remove_all(entry.path(), ignored);
		};
	};

	struct RunTemp final
	{
		explicit RunTemp(const std::filesystem::path& base) :
			folder((SweepRunTemps(base), RunTempFolder(base, GetCurrentProcessId()))),
			m_temp(Variable("TEMP")),
			m_tmp(Variable("TMP"))
		{
			std::error_code ignored;
			std::filesystem::remove_all(folder, ignored);
			std::filesystem::create_directories(folder, ignored);

			_putenv_s("TEMP", folder.string().c_str());
			_putenv_s("TMP", folder.string().c_str());
		};

		RunTemp(const RunTemp&) = delete;
		RunTemp& operator=(const RunTemp&) = delete;

		~RunTemp()
		{
			_putenv_s("TEMP", m_temp.c_str());
			_putenv_s("TMP", m_tmp.c_str());

			std::error_code ignored;
			std::filesystem::remove_all(folder, ignored);
		};

		const std::filesystem::path folder;

	private:
		static std::string Variable(const char* name)
		{
			const char* const value = std::getenv(name);

			return value == nullptr ? std::string() : value;
		};

		const std::string m_temp;
		const std::string m_tmp;
	};
};
