#include "doctest.h"

#include "Testing/HiddenChild.h"

#include "Images.h"

#include "App.h"
#include "ExtensionRegistry.h"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <process.h>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using namespace Loom;

#pragma region Helpers

namespace
{
	constexpr const char* app_mode = "--loom-app";
	constexpr const char* migrate_flag = "--loom-migrate";
	constexpr const char* screenshot_argument = "--screenshot";
	constexpr const char* project_argument = "--project";
	constexpr const char* settings_folder = "Loom App Tests";
	constexpr const char* previous_folder = "Loom App Tests Old";
	constexpr const char* settings_type = "LoomAppTests";
	constexpr const char* capture_frames = "3";
	constexpr int frames = 3;
	constexpr int settle_builds = 4;
	constexpr int builds = frames * settle_builds;
	constexpr int capture_width = 1280;
	constexpr int capture_height = 800;
	constexpr int png_width_offset = 16;
	constexpr int png_height_offset = 20;
	constexpr std::string_view png_signature{ "\x89PNG\r\n\x1a\n", 8 };
	constexpr std::string_view png_header{ "IHDR", 4 };
	constexpr int png_header_offset = 12;
	constexpr int child_failed = 2;
	constexpr int capture_failed = 1;

	struct TempFolder final
	{
		TempFolder() :
			path(std::filesystem::temp_directory_path() / ("Loom UI App Tests " + std::to_string(_getpid())))
		{
			std::filesystem::remove_all(path);
			std::filesystem::create_directories(path / "Project");
		};

		~TempFolder()
		{
			std::filesystem::remove_all(path);
		};

		const std::filesystem::path path;

		std::filesystem::path Settings() const { return path / settings_folder; };
		std::filesystem::path Project() const { return path / "Project"; };
		std::filesystem::path Capture() const { return path / "capture.png"; };
	};

	struct Run final
	{
		DWORD exit = 0;
		std::map<std::string, std::string> values;

		int Count(const std::string& key) const
		{
			const auto found = values.find(key);

			return found == values.end() ? -1 : std::atoi(found->second.c_str());
		};

		std::string Text(const std::string& key) const
		{
			const auto found = values.find(key);

			return found == values.end() ? std::string() : found->second;
		};
	};

	Run RunApp(
		const TempFolder& folder,
		const std::filesystem::path& capture,
		const bool migrate = false)
	{
		std::vector<std::string> arguments{ app_mode, folder.path.string() };

		if (migrate)
			arguments.push_back(migrate_flag);

		arguments.insert(arguments.end(), { screenshot_argument, capture.string(), capture_frames, project_argument, folder.Project().string() });

		const Finished finished = RunHidden(arguments);
		Run run;
		run.exit = finished.exit;

		std::istringstream lines(finished.output);
		std::string line;

		while (std::getline(lines, line))
		{
			if (!line.empty() && line.back() == '\r')
				line.pop_back();

			if (const size_t equals = line.find('='); equals != std::string::npos)
				run.values[line.substr(0, equals)] = line.substr(equals + 1);
		};

		return run;
	};

	std::string Bytes(const std::filesystem::path& file)
	{
		std::ifstream stream(file, std::ios::binary);

		return { std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
	};

	// imgui.ini is written in text mode, so with CRLF on Windows.
	std::string LayoutText(const std::filesystem::path& file)
	{
		std::string text = Bytes(file);
		std::erase(text, '\r');

		return text;
	};

	int BigEndian(
		const std::string& bytes,
		const size_t offset)
	{
		uint32_t value = 0;

		for (size_t index = offset; index < offset + sizeof(value); ++index)
			value = (value << 8) | (uint8_t)bytes[index];

		return (int)value;
	};
}

#pragma endregion

#pragma region Child

namespace
{
	struct Counts final
	{
		std::map<std::string, int> calls;
		std::vector<std::string> order;
		std::string project;
		bool docked = false;

		void Add(const std::string& name)
		{
			if (calls[name]++ == 0)
				order.push_back(name);
		};
	};
}

// Runs App with a panel shown, one hidden and one unlisted, counting each callback, and prints the counts as name=value lines.
int LoomAppRun(int count, char** arguments)
{
	if (count < 3)
		return child_failed;

	_putenv_s("LOCALAPPDATA", arguments[2]);

	const bool migrate = count > 3 && std::string_view(arguments[3]) == migrate_flag;
	Counts counts;

	App::Callbacks callbacks;

	callbacks.setup = [&counts]
	{
		counts.Add("setup");

		ExtensionRegistry::AddPanel(
			"I",
			"Alpha",
			[&counts](const std::string& project)
			{
				counts.Add("drawAlpha");
				counts.project = project;
			},
			[&counts](const std::string&) { counts.Add("update"); });
		ExtensionRegistry::AddPanel(
			"I",
			"Beta",
			[&counts](const std::string&) { counts.Add("drawBeta"); });
		ExtensionRegistry::AddPanel(
			"I",
			"Unlisted",
			[&counts](const std::string&) { counts.Add("drawUnlisted"); });
		ExtensionRegistry::Panels().back().listed = false;
		ExtensionRegistry::AddPanelSettings(settings_type);

		if (App::FirstRun())
			ExtensionRegistry::Panels().front().shown = true;

		App::SetProject(AppSettings::Project(App::Arguments(), { }, ""));
	};

	callbacks.font = [&counts] { counts.Add("font"); };

	callbacks.pad = [&counts](const bool, const std::function<void()>& submit)
	{
		counts.Add("pad");
		submit();
	};

	callbacks.menus = [&counts] { counts.Add("menus"); };

	callbacks.backdrop = [&counts]
	{
		counts.Add("backdrop");
		return false;
	};

	callbacks.drawBackdrop = [&counts] { counts.Add("drawBackdrop"); };

	callbacks.layout = [&counts](const ImGuiID dockspace)
	{
		counts.Add("layout");
		counts.docked = dockspace != 0;
	};

	callbacks.busy = [&counts]
	{
		counts.Add("busy");
		return false;
	};

	callbacks.unsettled = [&counts]
	{
		counts.Add("unsettled");
		return true;
	};

	callbacks.panelsDrawn = [&counts] { counts.Add("panelsDrawn"); };
	callbacks.frameDone = [&counts] { counts.Add("frameDone"); };
	callbacks.presented = [&counts] { counts.Add("presented"); };
	callbacks.stop = [&counts] { counts.Add("stop"); };

	callbacks.migrateLayout = [migrate](const std::string& text)
	{
		return migrate ? text + "\n" : text;
	};

	const int exit = App::Run({ "Loom App Tests", settings_folder, previous_folder }, callbacks);

	for (const auto& [name, calls] : counts.calls)
		std::cout << name << '=' << calls << '\n';

	std::string order;

	for (const std::string& name : counts.order)
		order += (order.empty() ? "" : ",") + name;

	std::cout
		<< "order=" << order << '\n'
		<< "project=" << counts.project << '\n'
		<< "docked=" << counts.docked << '\n'
		<< "frames=" << App::Frames() << '\n'
		<< "hidden=" << App::Hidden() << '\n'
		<< "firstRun=" << App::FirstRun() << '\n'
		<< "migrated=" << App::Migrated() << '\n'
		<< "settings=" << App::Settings().Folder().string() << '\n';
	std::cout.flush();

	return exit;
}

#pragma endregion

#pragma region Running

TEST_CASE("a first screenshot run calls each callback in order, as often as its frames and settling passes ask, and exits")
{
	const TempFolder folder;
	const Run run = RunApp(folder, folder.Capture());

	REQUIRE(run.exit == 0);

	CHECK(run.Text("order") == "font,setup,busy,pad,backdrop,menus,layout,update,drawAlpha,panelsDrawn,unsettled,frameDone,presented,stop");
	CHECK(run.Count("font") == 1);
	CHECK(run.Count("setup") == 1);
	CHECK(run.Count("stop") == 1);
	CHECK(run.Count("layout") == 1);
	CHECK(run.Text("docked") == "1");
	CHECK(run.Count("busy") == frames);
	CHECK(run.Count("frameDone") == frames);
	CHECK(run.Count("presented") == frames - 1);
	CHECK(run.Count("unsettled") == frames * settle_builds);
	CHECK(run.Count("backdrop") == builds);
	CHECK(run.Count("menus") == builds);
	CHECK(run.Count("panelsDrawn") == builds);
	CHECK(run.Count("update") == builds);
	CHECK(run.Count("drawAlpha") == builds);
	CHECK(run.Count("pad") >= builds * 3);
	CHECK(run.Count("drawBackdrop") == -1);
	CHECK(run.Count("drawBeta") == -1);
	CHECK(run.Count("drawUnlisted") == -1);
	CHECK(run.Count("frames") == frames);
	CHECK(run.Text("hidden") == "1");
	CHECK(run.Text("firstRun") == "1");
	CHECK(run.Text("migrated") == "0");
	CHECK(run.Text("project") == folder.Project().string());
	CHECK(run.Text("settings") == folder.Settings().string());
}

TEST_CASE("a screenshot run writes the frame as a 1280 by 800 PNG and keeps no window placement")
{
	const TempFolder folder;
	const Run run = RunApp(folder, folder.Capture());

	REQUIRE(run.exit == 0);

	const std::string png = Bytes(folder.Capture());

	REQUIRE(png.size() > png_height_offset + sizeof(uint32_t));
	CHECK(std::string_view(png).substr(0, png_signature.size()) == png_signature);
	CHECK(std::string_view(png).substr(png_header_offset, png_header.size()) == png_header);
	CHECK(BigEndian(png, png_width_offset) == capture_width);
	CHECK(BigEndian(png, png_height_offset) == capture_height);

	int width = 0;
	int height = 0;
	std::string error;

	CHECK(Loom::Images::Measure(folder.Capture(), width, height, error));
	CHECK(width == capture_width);
	CHECK(height == capture_height);
	CHECK_FALSE(std::filesystem::exists(folder.Settings() / "window.txt"));
}

TEST_CASE("a screenshot that cannot be written exits with 1, after stopping")
{
	const TempFolder folder;
	const Run run = RunApp(folder, folder.path / "Missing" / "capture.png");

	CHECK(run.exit == capture_failed);
	CHECK(run.Count("stop") == 1);
	CHECK_FALSE(std::filesystem::exists(folder.path / "Missing"));
}

#pragma endregion

#pragma region Layout file

TEST_CASE("the layout file keeps each listed panel's shown state, and the next run opens them as they were without laying out")
{
	const TempFolder folder;

	REQUIRE(RunApp(folder, folder.Capture()).exit == 0);

	const std::string layout = LayoutText(folder.Settings() / "imgui.ini");

	CHECK(layout.find("[LoomAppTests][Panels]\nAlpha=1\nBeta=0\n\n") != std::string::npos);
	CHECK(layout.find("Unlisted=") == std::string::npos);
	CHECK(layout.find("[Window][Alpha]\n") != std::string::npos);
	CHECK(layout.find("I Alpha") == std::string::npos);

	const Run again = RunApp(folder, folder.Capture());

	REQUIRE(again.exit == 0);
	CHECK(again.Text("firstRun") == "0");
	CHECK(again.Text("migrated") == "0");
	CHECK(again.Count("layout") == -1);
	CHECK(again.Count("drawAlpha") == builds);
	CHECK(again.Count("drawBeta") == -1);
}

TEST_CASE("a layout the app changes on reading is written back and the run is marked migrated")
{
	const TempFolder folder;

	REQUIRE(RunApp(folder, folder.Capture()).exit == 0);

	const Run migrated = RunApp(folder, folder.Capture(), true);

	REQUIRE(migrated.exit == 0);
	CHECK(migrated.Text("migrated") == "1");
	CHECK(migrated.Text("firstRun") == "0");
}

TEST_CASE("an earlier version's settings folder carries its layout over, so the run is not a first one")
{
	const TempFolder folder;

	std::filesystem::create_directories(folder.path / previous_folder);
	std::ofstream(folder.path / previous_folder / "imgui.ini", std::ios::binary) << "[LoomAppTests][Panels]\nAlpha=0\nBeta=1\n\n";

	const Run run = RunApp(folder, folder.Capture());

	REQUIRE(run.exit == 0);
	CHECK(run.Text("firstRun") == "0");
	CHECK(run.Count("layout") == -1);
	CHECK(run.Count("drawAlpha") == -1);
	CHECK(run.Count("drawBeta") == builds);
}

#pragma endregion
