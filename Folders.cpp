#include "Folders.h"

#include <array>

#include <windows.h>


namespace Loom
{
	std::filesystem::path ModuleFolder()
	{
		HMODULE module = nullptr;

		if (!GetModuleHandleExW(
			GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			(LPCWSTR)&ModuleFolder,
			&module))
			return { };

		std::array<wchar_t, MAX_PATH> path{ };

		if (GetModuleFileNameW(module, path.data(), (DWORD)path.size()) == 0)
			return { };

		return std::filesystem::path(path.data()).parent_path();
	};
};
