#pragma once

#include "imgui.h"

#include <chrono>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <vector>


namespace Loom
{
	/**
	* Loom::Widgets
	* - Small ImGui pieces and text formats that panels share
	* - Colours, thresholds and wording are passed in, so each panel keeps its own palette
	*/
	namespace Widgets
	{
		// A share of a whole as a rounded percentage, 0.456 as "46%".
		std::string WholePercent(double share);

		// A length of time as its nearest whole unit of s, m or h, rolling a rounded-up unit over
		// into the next, and a negative length as 0s.
		std::string ShortAge(int64_t seconds);

		// How long ago time was at now, empty for the default time_point.
		std::string Age(
			std::chrono::system_clock::time_point now,
			std::chrono::system_clock::time_point time);

		// Takes id out of ids when in it, and puts it in otherwise.
		void Flip(
			std::set<std::string>& ids,
			const std::string& id);

		// Text up to its first line break.
		std::string_view FirstLine(std::string_view text);

		// bad from critical up, warn from low up to it, and ok below low.
		ImVec4 ThresholdTone(
			double share,
			double low,
			double critical,
			const ImVec4& ok,
			const ImVec4& warn,
			const ImVec4& bad);

		// A line-high cross, inset by a share of a line, that is hover coloured under the mouse and TextDisabled otherwise. True when pressed.
		bool CrossButton(
			const char* id,
			const ImVec4& hover,
			float inset,
			float thickness);

		// A popup opened with ImGui::OpenPopup(id) that asks question. True on the frame confirm is pressed.
		bool ConfirmPopup(
			const char* id,
			const char* question,
			const char* confirm,
			const char* cancel);

		// A thin bar after a label, filled to share of the width left from one colour to another.
		// The bar is the last item, so a tooltip may follow. labelWidth lines the bars of several up.
		void SlimBar(
			const char* label,
			float labelWidth,
			double share,
			const ImVec4& from,
			const ImVec4& to);

		// Lines between points drawn in dashes of length with gaps, running on across corners.
		void DashedLine(
			ImDrawList* draw,
			const std::vector<ImVec2>& points,
			ImU32 colour,
			float length,
			float gap,
			float thickness);

		// A table row naming a section: solid behind its name, tinted across, the name set in by inset of a line.
		void SectionRow(
			const char* name,
			float inset,
			float tint);
	};
};
