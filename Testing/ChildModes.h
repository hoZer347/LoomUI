#pragma once

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>


namespace Loom::Testing
{
	/**
	* Loom::Testing::RunChildMode
	* - The small jobs a test executable does when a test starts it again
	*   with a mode as its first argument: --print <text> <exit code>, --echo
	*   (each line read is written back), --threads and --hang (idle for a
	*   minute), --flood <lines> and --desktop (the name of its desktop)
	* - Gives the exit code, or nothing when the mode is not one of these
	*/
	namespace ChildDetail
	{
		inline constexpr auto hang = std::chrono::seconds(60);
		inline constexpr size_t name_capacity = 256;

		inline int PrintDesktop()
		{
			wchar_t name[name_capacity]{ };
			DWORD length = 0;

			if (!GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()), UOI_NAME, name, sizeof(name), &length))
				return 1;

			const int size = WideCharToMultiByte(CP_UTF8, 0, name, -1, nullptr, 0, nullptr, nullptr);

			if (size <= 0)
				return 1;

			std::string narrow(size, '\0');
			WideCharToMultiByte(CP_UTF8, 0, name, -1, narrow.data(), size, nullptr, nullptr);
			narrow.resize(size - 1);

			std::cout << narrow << std::endl;

			return 0;
		};
	};

	inline std::optional<int> RunChildMode(
		const int count,
		char** const arguments)
	{
		const std::string_view mode = count > 1 ? arguments[1] : "";

		if (mode == "--desktop")
			return ChildDetail::PrintDesktop();

		if (mode == "--print")
		{
			std::cout << arguments[2] << std::endl;
			return std::atoi(arguments[3]);
		};

		if (mode == "--echo")
		{
			for (std::string line; std::getline(std::cin, line); )
				std::cout << line << std::endl;

			return 0;
		};

		if (mode == "--threads")
		{
			std::thread second([]() { std::this_thread::sleep_for(ChildDetail::hang); });
			std::this_thread::sleep_for(ChildDetail::hang);
			second.join();

			return 0;
		};

		if (mode == "--flood")
		{
			const int lines = std::atoi(arguments[2]);

			for (int line = 0; line < lines; line++)
				std::cout << "{\"line\":" << line << "}\n";

			return 0;
		};

		if (mode == "--hang")
		{
			std::this_thread::sleep_for(ChildDetail::hang);
			return 0;
		};

		return std::nullopt;
	};
};
