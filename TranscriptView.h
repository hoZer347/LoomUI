#pragma once

#include "ImageCache.h"
#include "TextLayout.h"

#include "imgui.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>


namespace Loom
{
	/**
	* Loom::TranscriptView
	* - A scrolling list of rows whose text can be selected across rows with the mouse
	*   and copied, and that sticks to the bottom while new rows arrive
	* - The rows are the caller's, handed in through a Source: marked down text, plain
	*   text or a row with a header that opens to show detail, each with figures
	* - Only the rows in view are drawn, and text is laid out again only when its row
	*   or the width changes
	*/
	struct TranscriptView final
	{
		struct Row
		{
			enum class Kind : uint8_t
			{
				Markdown,
				Plain,
				Folding,
			};

			Kind kind = Kind::Plain;
			uint32_t revision = 0;
			bool open = false;
			bool boxed = false;
			bool link = false;
			bool diff = false;
			// A hidden row takes no room, is not drawn and is left out of what is copied.
			bool hidden = false;
			// How far the body of a row that is not boxed is indented, and how far it is set down, for what the source draws in front of it.
			float lead = 0;
			float pad = 0;

			// The body, or what follows the header of a folding row.
			std::string_view text;
			// What a folding row shows when open.
			std::string_view detail;
			ImU32 colour = 0;
			ImU32 headerColour = 0;
			const std::vector<std::string>* images = nullptr;
			// Figure paths in the text that are shown from another file.
			const std::map<std::string, std::string>* kept = nullptr;
		};

		struct Source
		{
			virtual ~Source() = default;

			virtual size_t Count() const = 0;
			virtual Row At(size_t index) const = 0;
			virtual std::string Header(size_t index) const = 0;
			virtual void SetOpen(size_t index, bool open) = 0;
			virtual void Activate(size_t index) = 0;
			// Draws in front of the body of a row shown that is not folding, at its top left, in the room its lead and pad leave.
			virtual void DrawLead(size_t index, ImVec2 at) const { };
		};

		// Lays out and draws the rows in a child window leaving footer below it. Working shows a moving label after the last row.
		void Draw(Source& source, ImageCache& images, float footer, bool working);

		// Lays rows out again that hold figures, once the cache has sizes or pictures that were missing.
		void ImagesChanged(const Source& source);

		// Forgets the layout and the selection, and follows the bottom.
		void Reset();

		void Stick() { m_stick = true; };

		static ImVec2 Fit(const ImageCache::Entry& entry, ImVec2 space);

	private:
		enum class Part : uint8_t
		{
			Body,
			Detail,
		};

		struct Caret
		{
			size_t block = 0;
			Part part = Part::Body;
			size_t offset = 0;

			auto operator<=>(const Caret&) const = default;
		};

		struct Laid
		{
			Row::Kind kind = Row::Kind::Plain;
			float width = -1;
			uint32_t revision = UINT32_MAX;
			bool open = false;
			float top = 0;
			float height = 0;
			bool hidden = false;
			float lead = 0;
			float pad = 0;
			TextLayout::Layout body;
			TextLayout::Layout detail;
			std::vector<TextLayout::Paragraph> paragraphs;
			uint32_t parsedRevision = UINT32_MAX;
			std::vector<TextLayout::Paragraph> lines;
			uint32_t linesRevision = UINT32_MAX;
		};

		struct ShownFigure
		{
			ImVec2 min{ };
			ImVec2 max{ };
			std::string path;
		};

		// The height of the transcript, up to and with the gap after its last shown row.
		float Relay(float width);
		void DrawBlock(size_t index, ImVec2 origin, float width);
		void DrawLayout(const TextLayout::Layout& layout, std::string_view text, size_t index, Part part, ImVec2 origin, float width);
		void DrawFigure(const std::string& path, ImVec2 at, float width);
		void DrawImages(const std::vector<std::string>& images, ImVec2 at, float width);
		float FigureSpace(const std::string& path, float width);
		float ImagesHeight(const std::vector<std::string>& images, float width);
		void HandleMouse(ImVec2 origin);
		size_t BlockAt(float y) const;
		Caret CaretAt(ImVec2 point) const;
		bool LinkAt(ImVec2 point, size_t& index) const;
		static ImVec2 BodyOffset(const Row& row);
		static float BodyWidth(const Row& row, float width);
		std::pair<size_t, size_t> SelectedIn(size_t index, Part part, size_t size) const;
		std::string SelectedText() const;
		void CopyHovered();

		Source* m_source = nullptr;
		ImageCache* m_images = nullptr;
		std::vector<Laid> m_laid{ };
		std::vector<ShownFigure> m_figures{ };

		Caret m_anchor{ };
		Caret m_focus{ };
		bool m_selecting = false;
		size_t m_hovered = 0;
		bool m_stick = true;
	};
};
