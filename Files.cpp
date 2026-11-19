#include "Files.h"

#include <fstream>
#include <iterator>
#include <system_error>
#include <thread>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>


namespace Loom::Files
{
	namespace
	{
		constexpr std::chrono::milliseconds replace_retry{ 1 };
		constexpr const char* partial_extension = ".tmp";
		constexpr char carriage_return = '\r';
	};

	std::string ReadWhole(const std::filesystem::path& file)
	{
		std::ifstream stream(file, std::ios::binary);

		return { std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
	};

	std::vector<std::string> ReadLines(const std::filesystem::path& file)
	{
		std::ifstream stream(file, std::ios::binary);
		std::vector<std::string> lines;
		std::string line;

		while (std::getline(stream, line))
		{
			if (!line.empty() && line.back() == carriage_return)
				line.pop_back();

			lines.push_back(line);
		};

		return lines;
	};

	bool WriteWhole(
		const std::filesystem::path& file,
		const std::string& text,
		bool flush)
	{
		const HANDLE handle = CreateFileW(
			file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

		if (handle == INVALID_HANDLE_VALUE)
			return false;

		DWORD written = 0;
		const bool whole = WriteFile(handle, text.data(), (DWORD)text.size(), &written, nullptr)
			&& written == text.size()
			&& (!flush || FlushFileBuffers(handle));

		CloseHandle(handle);

		return whole;
	};

	bool WriteAtomic(
		const std::filesystem::path& file,
		const std::string& text,
		const AtomicWrite& how)
	{
		std::error_code code;
		std::filesystem::create_directories(file.parent_path(), code);

		std::filesystem::path partial = how.partial;

		if (partial.empty())
		{
			partial = file;
			partial += "." + std::to_string(GetCurrentProcessId()) + partial_extension;
		};

		if (!WriteWhole(partial, text, how.flush))
			return false;

		for (int attempt = 0; attempt < how.attempts; attempt++)
		{
			if (MoveFileExW(partial.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
				return true;

			if (attempt + 1 < how.attempts)
				std::this_thread::sleep_for(replace_retry);
		};

		std::filesystem::remove(partial, code);

		return false;
	};

	int64_t EpochSeconds(std::chrono::system_clock::time_point time)
	{
		return std::chrono::duration_cast<std::chrono::seconds>(time.time_since_epoch()).count();
	};

	LockFile::LockFile(const std::filesystem::path& file)
	{
		const HANDLE handle = CreateFileW(
			file.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE, nullptr);

		if (handle != INVALID_HANDLE_VALUE)
			m_file = handle;
	};

	LockFile::LockFile(LockFile&& other) noexcept :
		m_file(std::exchange(other.m_file, nullptr))
	{
	};

	LockFile& LockFile::operator=(LockFile&& other) noexcept
	{
		std::swap(m_file, other.m_file);

		return *this;
	};

	LockFile::~LockFile()
	{
		if (m_file != nullptr)
			CloseHandle(m_file);
	};
};
