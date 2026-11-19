#pragma once

#include <filesystem>
#include <optional>

struct GLFWwindow;


namespace Loom
{
	/**
	* Loom::Placement
	* - Where a window sat and whether it was maximised, kept in one line of a file
	*   as left top right bottom and 0 or 1, so the next run opens it there
	* - Taken from a live window with Take, which keeps the restored rectangle of a
	*   maximised window and says nothing new while it is minimised; put back with Show
	*/
	struct Placement final
	{
		long left = 0;
		long top = 0;
		long right = 0;
		long bottom = 0;
		bool maximized = false;

		bool operator==(const Placement&) const = default;

		// A file that is missing, unreadable or holds an empty rectangle reads as none.
		static std::optional<Placement> Read(const std::filesystem::path& file);

		static void Write(
			const std::filesystem::path& file,
			const Placement& placement);

		// previous is what is kept while the window is minimised, and the rectangle kept while it is maximised.
		static std::optional<Placement> Take(
			GLFWwindow* window,
			const std::optional<Placement>& previous);

		// Moves the window onto the placement when a monitor still shows it, then shows it, maximised or not.
		static void Show(
			GLFWwindow* window,
			const std::optional<Placement>& placement);
	};
};
