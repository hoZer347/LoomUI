#include "Widgets.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iterator>


namespace Loom
{
	namespace
	{
		constexpr float Percent = 100;
		constexpr float Half = 0.5f;
		constexpr float SlimBarScale = 0.4f;
		constexpr int64_t Minute = 60;
		constexpr int64_t Hour = 3600;
		constexpr float Opaque = 1.0f;
	};

	std::string Widgets::WholePercent(const double share)
	{
		return std::to_string(std::lround(share * Percent)) + "%";
	};

	std::string Widgets::ShortAge(const int64_t seconds)
	{
		constexpr struct
		{
			int64_t size;
			const char* name;
		} units[] =
		{
			{ 1, "s" },
			{ Minute, "m" },
			{ Hour, "h" },
		};

		const int64_t whole = std::max<int64_t>(0, seconds);
		size_t index = 0;

		while (index + 1 < std::size(units) && whole >= units[index + 1].size)
			++index;

		int64_t value = (whole + units[index].size / 2) / units[index].size;

		if (index + 1 < std::size(units) && value * units[index].size >= units[index + 1].size)
		{
			++index;
			value = (whole + units[index].size / 2) / units[index].size;
		};

		return std::to_string(value) + units[index].name;
	};

	std::string Widgets::Age(
		const std::chrono::system_clock::time_point now,
		const std::chrono::system_clock::time_point time)
	{
		if (time == std::chrono::system_clock::time_point{ })
			return { };

		return ShortAge(std::chrono::duration_cast<std::chrono::seconds>(now - time).count());
	};

	void Widgets::Flip(
		std::set<std::string>& ids,
		const std::string& id)
	{
		if (!ids.erase(id))
			ids.insert(id);
	};

	std::string_view Widgets::FirstLine(const std::string_view text)
	{
		return text.substr(0, text.find_first_of("\r\n"));
	};

	ImVec4 Widgets::ThresholdTone(
		const double share,
		const double low,
		const double critical,
		const ImVec4& ok,
		const ImVec4& warn,
		const ImVec4& bad)
	{
		return share >= critical ? bad : share >= low ? warn : ok;
	};

	bool Widgets::CrossButton(
		const char* const id,
		const ImVec4& hover,
		const float inset,
		const float thickness)
	{
		const float side = ImGui::GetTextLineHeight();
		const bool pressed = ImGui::InvisibleButton(id, { side, side });
		const ImU32 colour = ImGui::GetColorU32(ImGui::IsItemHovered() ? hover : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
		const ImVec2 min = ImGui::GetItemRectMin();
		const ImVec2 max = ImGui::GetItemRectMax();
		const float away = side * inset;
		ImDrawList* const draw = ImGui::GetWindowDrawList();

		draw->AddLine({ min.x + away, min.y + away }, { max.x - away, max.y - away }, colour, thickness);
		draw->AddLine({ max.x - away, min.y + away }, { min.x + away, max.y - away }, colour, thickness);
		return pressed;
	};

	bool Widgets::ConfirmPopup(
		const char* const id,
		const char* const question,
		const char* const confirm,
		const char* const cancel)
	{
		if (!ImGui::BeginPopup(id))
			return false;

		ImGui::TextUnformatted(question);

		const bool confirmed = ImGui::Button(confirm);
		ImGui::SameLine();

		if (confirmed || ImGui::Button(cancel))
			ImGui::CloseCurrentPopup();

		ImGui::EndPopup();
		return confirmed;
	};

	void Widgets::SlimBar(
		const char* const label,
		const float labelWidth,
		const double share,
		const ImVec4& from,
		const ImVec4& to)
	{
		const float fill = std::clamp((float)share, 0.0f, 1.0f);
		const float height = ImGui::GetTextLineHeight() * SlimBarScale;
		const float left = ImGui::GetCursorPosX();

		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(label);
		ImGui::SameLine(left + labelWidth + ImGui::GetStyle().ItemSpacing.x);

		const ImVec2 at = ImGui::GetCursorScreenPos();
		const float width = ImGui::GetContentRegionAvail().x;
		const float top = at.y + (ImGui::GetFrameHeight() - height) * Half;
		const ImVec2 min{ at.x, top };
		const ImVec2 max{ at.x + width, top + height };
		const ImVec2 split{ at.x + width * fill, max.y };
		const ImU32 start = ImGui::GetColorU32(from);
		const ImU32 end = ImGui::GetColorU32(to);

		ImGui::InvisibleButton(label, { width, ImGui::GetFrameHeight() });

		ImDrawList* const draw = ImGui::GetWindowDrawList();

		draw->AddRectFilled(min, max, ImGui::GetColorU32(ImGuiCol_FrameBg), height * Half);
		draw->AddRectFilledMultiColor(min, split, start, end, end, start);
	};

	void Widgets::DashedLine(
		ImDrawList* const draw,
		const std::vector<ImVec2>& points,
		const ImU32 colour,
		const float length,
		const float gap,
		const float thickness)
	{
		float into = 0;

		for (size_t index = 1; index < points.size(); ++index)
		{
			const ImVec2 from = points[index - 1];
			const ImVec2 step{ points[index].x - from.x, points[index].y - from.y };
			const float span = std::hypot(step.x, step.y);

			for (float start = -into; start < span; start += length + gap)
			{
				const float on = std::max(start, 0.0f);
				const float off = std::min(start + length, span);

				if (off > on)
					draw->AddLine(
						{ from.x + step.x * on / span, from.y + step.y * on / span },
						{ from.x + step.x * off / span, from.y + step.y * off / span },
						colour,
						thickness);
			};

			into = std::fmod(into + span, length + gap);
		};
	};

	void Widgets::SectionRow(
		const char* const name,
		const float inset,
		const float tint)
	{
		ImGui::TableNextRow();
		ImVec4 solid = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
		solid.w = Opaque;

		ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(solid));
		ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, ImGui::GetColorU32(ImGuiCol_Header, tint));
		ImGui::TableSetColumnIndex(0);
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetTextLineHeight() * inset);
		ImGui::TextUnformatted(name);
	};
};
