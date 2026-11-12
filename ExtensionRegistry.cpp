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

	std::deque<ExtensionRegistry::Panel>& ExtensionRegistry::Panels()
	{
		static std::deque<Panel> panels{ };

		return panels;
	};
};
