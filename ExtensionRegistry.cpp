#include "ExtensionRegistry.h"

#include <utility>

#ifdef _WIN32
	#define WIN32_LEAN_AND_MEAN
	#define NOMINMAX
	#include <Windows.h>
	#include <intrin.h>
	#define LOOM_CALLER_ADDRESS() _ReturnAddress()
#elif defined(__linux__)
	#include <dlfcn.h>
	#define LOOM_CALLER_ADDRESS() __builtin_return_address(0)
#else
	#define LOOM_CALLER_ADDRESS() nullptr
#endif


namespace Loom
{
	namespace
	{
		const void* ModuleOf(const void* address)
		{
#ifdef _WIN32
			HMODULE module = nullptr;

			if (!GetModuleHandleExW(
				GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				(LPCWSTR)address,
				&module))
				return nullptr;

			return module;
#elif defined(__linux__)
			Dl_info info{ };

			if (dladdr(address, &info) == 0 || info.dli_fname == nullptr)
				return nullptr;

			void* const module = dlopen(info.dli_fname, RTLD_NOW | RTLD_NOLOAD);

			if (module != nullptr)
				dlclose(module);

			return module;
#else
			return nullptr;
#endif
		};
	};

	void ExtensionRegistry::AddPanel(
		const std::string& icon,
		const std::string& name,
		std::function<void(const std::string& project)> draw,
		std::function<void(const std::string& project)> update,
		std::function<void(const std::vector<std::string>& files)> drop)
	{
		const void* const owner = ModuleOf(LOOM_CALLER_ADDRESS());

		Panels().push_back({ icon + " " + name + "###" + name, std::move(draw), std::move(update), std::move(drop), false, owner });
	};

	void ExtensionRegistry::AddMenuItem(
		const std::string& menu,
		const std::string& name,
		std::function<void(const std::string& project)> select,
		std::function<bool(const std::string& project)> enabled,
		std::function<std::string(const std::string& project)> tooltip)
	{
		const void* const owner = ModuleOf(LOOM_CALLER_ADDRESS());

		MenuItems().push_back({ menu, name, std::move(select), std::move(enabled), std::move(tooltip), owner });
	};

	std::list<ExtensionRegistry::MenuItem>& ExtensionRegistry::MenuItems()
	{
		static std::list<MenuItem> items{ };

		return items;
	};

	std::list<ExtensionRegistry::Panel>& ExtensionRegistry::Panels()
	{
		static std::list<Panel> panels{ };

		return panels;
	};
};
