#include "Placement.h"

#include <GLFW/glfw3.h>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <fstream>


namespace Loom
{
	std::optional<Placement> Placement::Read(const std::filesystem::path& file)
	{
		std::ifstream stream(file, std::ios::binary);
		Placement placement;
		int maximized = 0;

		if (!(stream >> placement.left >> placement.top >> placement.right >> placement.bottom >> maximized)
			|| placement.right <= placement.left
			|| placement.bottom <= placement.top)
			return std::nullopt;

		placement.maximized = maximized != 0;

		return placement;
	};

	void Placement::Write(
		const std::filesystem::path& file,
		const Placement& placement)
	{
		std::ofstream stream(file, std::ios::binary | std::ios::trunc);
		stream << placement.left << ' ' << placement.top << ' ' << placement.right << ' ' << placement.bottom << ' ' << (placement.maximized ? 1 : 0) << '\n';
	};

	std::optional<Placement> Placement::Take(
		GLFWwindow* const window,
		const std::optional<Placement>& previous)
	{
		const HWND handle = glfwGetWin32Window(window);

		if (IsIconic(handle))
			return previous;

		if (IsZoomed(handle))
		{
			WINDOWPLACEMENT restored{ };
			restored.length = sizeof(restored);

			if (!previous
				&& !GetWindowPlacement(handle, &restored))
				return std::nullopt;

			const RECT& normal = restored.rcNormalPosition;
			Placement maximized = previous ? *previous : Placement{ normal.left, normal.top, normal.right, normal.bottom };
			maximized.maximized = true;

			return maximized;
		};

		RECT shown{ };

		if (!GetWindowRect(handle, &shown))
			return previous;

		return Placement{ shown.left, shown.top, shown.right, shown.bottom, false };
	};

	void Placement::Show(
		GLFWwindow* const window,
		const std::optional<Placement>& placement)
	{
		const HWND handle = glfwGetWin32Window(window);
		const RECT shown = placement ? RECT{ placement->left, placement->top, placement->right, placement->bottom } : RECT{ };

		if (placement && MonitorFromRect(&shown, MONITOR_DEFAULTTONULL) != nullptr)
			SetWindowPos(handle, nullptr, shown.left, shown.top, shown.right - shown.left, shown.bottom - shown.top, SWP_NOZORDER | SWP_NOACTIVATE);

		ShowWindow(handle, placement && placement->maximized ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL);
	};
};
