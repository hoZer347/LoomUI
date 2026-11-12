#include "ExtensionRegistry.h"

#include <utility>


namespace Loom
{
	void ExtensionRegistry::AddPanel(
		const std::string& icon,
		const std::string& name,
		std::function<void(const std::string& project)> draw,
		std::function<void(const std::string& project)> update,
		std::function<void(const std::vector<std::string>& files)> drop)
	{
		Panels().push_back({ icon + " " + name + "###" + name, std::move(draw), std::move(update), std::move(drop) });
	};

	void ExtensionRegistry::AddMenuItem(
		const std::string& menu,
		const std::string& name,
		std::function<void(const std::string& project)> select,
		std::function<bool(const std::string& project)> enabled,
		std::function<std::string(const std::string& project)> tooltip)
	{
		MenuItems().push_back({ menu, name, std::move(select), std::move(enabled), std::move(tooltip) });
	};

	std::deque<ExtensionRegistry::MenuItem>& ExtensionRegistry::MenuItems()
	{
		static std::deque<MenuItem> items{ };

		return items;
	};

	std::deque<ExtensionRegistry::Panel>& ExtensionRegistry::Panels()
	{
		static std::deque<Panel> panels{ };

		return panels;
	};
};
