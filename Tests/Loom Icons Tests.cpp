#include "doctest.h"

#include "EditorTheme.h"

#include "Testing/Headless.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <cstdint>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace Loom;

#pragma region Fixture

namespace
{
	constexpr int channels = 4;
	constexpr float small_size = EditorTheme::DefaultFontSize;
	constexpr float large_size = EditorTheme::DefaultFontSize * 2;
	constexpr int doubled = 2;
	constexpr ImWchar ordinary = 'A';

	constexpr uint8_t red[channels] = { 255, 0, 0, 255 };
	constexpr uint8_t blue[channels] = { 0, 0, 255, 255 };

	EditorTheme::ColourIcon Solid(
		const int width,
		const int height,
		const uint8_t (&colour)[channels])
	{
		EditorTheme::ColourIcon icon{ .width = width, .height = height };

		for (int pixel = 0; pixel < width * height; ++pixel)
			icon.rgba.insert(icon.rgba.end(), colour, colour + channels);

		return icon;
	};

	const std::vector<EditorTheme::ColourIcon>& Icons()
	{
		static const std::vector<EditorTheme::ColourIcon> icons =
		{
			Solid(3, 5, red),
			Solid(7, 2, blue),
			Solid(12, 12, red),
		};

		return icons;
	};

	ImWchar Codepoint(const int index)
	{
		return (ImWchar)(EditorTheme::ColourIconFirst + (char32_t)index);
	};

	struct Fixture final
	{
		explicit Fixture(std::vector<EditorTheme::ColourIcon> icons = Icons())
		{
			font = ImGui::GetIO().Fonts->AddFontDefault();
			EditorTheme::SetColourIcons(std::move(icons));
			added = EditorTheme::AddColourIcons(*font);
		};

		explicit Fixture(EditorTheme::ColourIconSource source)
		{
			font = ImGui::GetIO().Fonts->AddFontDefault();
			EditorTheme::SetColourIconSource(std::move(source));
			added = EditorTheme::AddColourIcons(*font);
		};

		~Fixture()
		{
			EditorTheme::SetColourIcons({ });
		};

		Fixture(const Fixture&) = delete;
		Fixture& operator=(const Fixture&) = delete;

		const ImFontGlyph* Find(
			const float size,
			const ImWchar codepoint) const
		{
			return font->GetFontBaked(size)->FindGlyphNoFallback(codepoint);
		};

		const uint8_t* Pixel(const ImFontGlyph& glyph) const
		{
			ImFontAtlas* const atlas = font->OwnerAtlas;
			const ImTextureRect* const rect = ImFontAtlasPackGetRect(atlas, glyph.PackId);

			return rect == nullptr ? nullptr : (const uint8_t*)atlas->TexData->GetPixelsAt(rect->x, rect->y);
		};

		ImFont* font = nullptr;
		bool added = false;

	private:
		const Loom::Testing::Headless m_screen{ Loom::Testing::Headless::Options{ { 800, 600 }, 1.0f / 60, ImGuiBackendFlags_RendererHasTextures, false } };
	};
};

#pragma endregion

#pragma region Atlas

TEST_SUITE("Loom Icons")
{
	TEST_CASE("AddColourIcons succeeds with icons set and fails with none")
	{
		{
			const Fixture fixture;

			CHECK(fixture.added);
			CHECK(EditorTheme::HasColourIcons(*fixture.font));
		};

		const Fixture empty(std::vector<EditorTheme::ColourIcon>{ });

		CHECK_FALSE(empty.added);
		CHECK_FALSE(EditorTheme::HasColourIcons(*empty.font));
	};

	TEST_CASE("each icon is a coloured glyph at its own size, centred on the line")
	{
		const Fixture fixture;
		REQUIRE(fixture.added);

		for (int index = 0; index < (int)Icons().size(); ++index)
		{
			const EditorTheme::ColourIcon& icon = Icons()[(size_t)index];
			const ImFontGlyph* const glyph = fixture.Find(small_size, Codepoint(index));

			REQUIRE(glyph != nullptr);
			CHECK(glyph->Visible);
			CHECK(glyph->Colored);
			CHECK(glyph->X1 - glyph->X0 == (float)icon.width);
			CHECK(glyph->Y1 - glyph->Y0 == (float)icon.height);
			CHECK(glyph->AdvanceX == (float)icon.width);
			CHECK(glyph->Y0 == EditorTheme::ColourIconTop(small_size, (float)icon.height));
		};
	};

	TEST_CASE("icons double at twice the size the font is first built at")
	{
		const Fixture fixture;
		REQUIRE(fixture.added);

		for (int index = 0; index < (int)Icons().size(); ++index)
		{
			const EditorTheme::ColourIcon& icon = Icons()[(size_t)index];
			const ImFontGlyph* const glyph = fixture.Find(large_size, Codepoint(index));

			REQUIRE(glyph != nullptr);
			CHECK(glyph->Colored);
			CHECK(glyph->X1 - glyph->X0 == (float)(icon.width * doubled));
			CHECK(glyph->Y1 - glyph->Y0 == (float)(icon.height * doubled));
			CHECK(glyph->AdvanceX == (float)(icon.width * doubled));
		};
	};

	TEST_CASE("an icon's pixels are in the atlas in its own colours")
	{
		const Fixture fixture;
		REQUIRE(fixture.added);

		for (int index = 0; index < (int)Icons().size(); ++index)
		{
			const ImFontGlyph* const glyph = fixture.Find(small_size, Codepoint(index));
			REQUIRE(glyph != nullptr);

			const uint8_t* const pixel = fixture.Pixel(*glyph);
			REQUIRE(pixel != nullptr);

			for (int channel = 0; channel < channels; ++channel)
				CHECK(pixel[channel] == Icons()[(size_t)index].rgba[(size_t)channel]);
		};
	};

	TEST_CASE("a code point past the last icon is not a glyph")
	{
		const Fixture fixture;
		REQUIRE(fixture.added);

		CHECK(fixture.Find(small_size, Codepoint((int)Icons().size())) == nullptr);
	};

	TEST_CASE("ordinary text is still found and not coloured")
	{
		const Fixture fixture;
		REQUIRE(fixture.added);

		const ImFontGlyph* const glyph = fixture.Find(small_size, ordinary);

		REQUIRE(glyph != nullptr);
		CHECK_FALSE(glyph->Colored);
	};

	TEST_CASE("ColourIconText is the UTF-8 of the icon's code point")
	{
		for (int index = 0; index < (int)Icons().size(); ++index)
		{
			const std::string text = EditorTheme::ColourIconText(index);
			unsigned int decoded = 0;
			const int length = ImTextCharFromUtf8(&decoded, text.data(), text.data() + text.size());

			CHECK(length == (int)text.size());
			CHECK(decoded == (unsigned int)Codepoint(index));
		};
	};

	TEST_CASE("RemoveColourIcons takes the icons out and leaves ordinary text")
	{
		const Fixture fixture;
		REQUIRE(fixture.added);
		REQUIRE(fixture.Find(small_size, Codepoint(0)) != nullptr);

		const int sources = fixture.font->OwnerAtlas->Sources.Size;

		EditorTheme::RemoveColourIcons(*fixture.font);

		CHECK_FALSE(EditorTheme::HasColourIcons(*fixture.font));
		CHECK(fixture.font->OwnerAtlas->Sources.Size == sources - 1);
		CHECK(fixture.Find(small_size, Codepoint(0)) == nullptr);
		CHECK(fixture.Find(small_size, ordinary) != nullptr);
	};

	TEST_CASE("the icons can be added, removed and added again")
	{
		const Fixture fixture;
		REQUIRE(fixture.added);

		EditorTheme::RemoveColourIcons(*fixture.font);
		REQUIRE(EditorTheme::AddColourIcons(*fixture.font));

		CHECK(EditorTheme::HasColourIcons(*fixture.font));
		CHECK(fixture.Find(small_size, Codepoint(0)) != nullptr);
	};

	TEST_CASE("setting new icons replaces the last set at the next add")
	{
		constexpr int wide = 9;
		constexpr int tall = 4;

		const Fixture fixture;
		REQUIRE(fixture.added);

		EditorTheme::RemoveColourIcons(*fixture.font);
		EditorTheme::SetColourIcons({ Solid(wide, tall, blue) });
		REQUIRE(EditorTheme::AddColourIcons(*fixture.font));

		const ImFontGlyph* const glyph = fixture.Find(small_size, Codepoint(0));

		REQUIRE(glyph != nullptr);
		CHECK(glyph->AdvanceX == (float)wide);
		CHECK(glyph->Y1 - glyph->Y0 == (float)tall);
		CHECK(fixture.Find(small_size, Codepoint(1)) == nullptr);
	};
}

#pragma endregion

#pragma region Metrics

TEST_SUITE("Loom Icons")
{
	TEST_CASE("magnification is the whole multiple of the first size, never below 1")
	{
		CHECK(EditorTheme::ColourIconMagnification(15, 15) == 1);
		CHECK(EditorTheme::ColourIconMagnification(29, 15) == 1);
		CHECK(EditorTheme::ColourIconMagnification(30, 15) == 2);
		CHECK(EditorTheme::ColourIconMagnification(8, 15) == 1);
	};

	TEST_CASE("an icon's top centres it on the line, rounded down")
	{
		CHECK(EditorTheme::ColourIconTop(15, 9) == 3);
		CHECK(EditorTheme::ColourIconTop(15, 10) == 2);
		CHECK(EditorTheme::ColourIconTop(15, 20) == -3);
	};

	TEST_CASE("a row holds the tallest icon with a row gap above and below")
	{
		CHECK(EditorTheme::ColourIconRowHeight(10, 1) == 10 + 2 * EditorTheme::ColourIconRowGap);
		CHECK(EditorTheme::ColourIconRowHeight(10, 2) == 20 + 2 * EditorTheme::ColourIconRowGap);
	};

	TEST_CASE("an icon is centred in a wider slot, rounded down")
	{
		CHECK(EditorTheme::ColourIconCentre(20, 10, 2) == 10);
		CHECK(EditorTheme::ColourIconCentre(11, 10, 1) == 0);
		CHECK(EditorTheme::ColourIconCentre(10, 10, 3) == 0);
	};

	TEST_CASE("tab padding leaves the gap around a tall icon and never drops below the minimum")
	{
		CHECK(EditorTheme::ColourIconTabPadding(15, 20, 1, 4) == EditorTheme::ColourIconGap + 3);
		CHECK(EditorTheme::ColourIconTabPadding(15, 20, 1, 8) == 8);
		CHECK(EditorTheme::ColourIconTabPadding(15, 5, 1, 4) == 4);
	};

	TEST_CASE("menu spacing gives a tall icon its own row and never drops below the minimum")
	{
		CHECK(EditorTheme::ColourIconMenuSpacing(15, 20, 1, 2) == 8);
		CHECK(EditorTheme::ColourIconMenuSpacing(15, 20, 1, 12) == 12);
		CHECK(EditorTheme::ColourIconMenuSpacing(15, 5, 1, 4) == 4);
	};

	TEST_CASE("a scaled icon repeats each pixel across and down")
	{
		EditorTheme::ColourIcon icon{ .width = 2, .height = 1 };
		icon.rgba.insert(icon.rgba.end(), red, red + channels);
		icon.rgba.insert(icon.rgba.end(), blue, blue + channels);

		const EditorTheme::ColourIcon scaled = EditorTheme::ColourIconScaled(icon, doubled);

		REQUIRE(scaled.width == 4);
		REQUIRE(scaled.height == 2);
		REQUIRE(scaled.rgba.size() == (size_t)(4 * 2 * channels));

		for (int y = 0; y < scaled.height; ++y)
			for (int x = 0; x < scaled.width; ++x)
			{
				const uint8_t* const expected = x < doubled ? red : blue;
				const uint8_t* const pixel = scaled.rgba.data() + (size_t)((y * scaled.width + x) * channels);

				for (int channel = 0; channel < channels; ++channel)
					CHECK(pixel[channel] == expected[channel]);
			};
	};

	TEST_CASE("a scale of 1 leaves an icon as it was")
	{
		const EditorTheme::ColourIcon& icon = Icons()[0];
		const EditorTheme::ColourIcon scaled = EditorTheme::ColourIconScaled(icon, 1);

		CHECK(scaled.width == icon.width);
		CHECK(scaled.height == icon.height);
		CHECK(scaled.rgba == icon.rgba);
	};
}

#pragma endregion

#pragma region Source

namespace
{
	constexpr int unheld = 1;
	constexpr int unmade = 2;

	// A source over Icons() that does not hold unheld, cannot make unmade, and records every index it makes.
	EditorTheme::ColourIconSource Recording(std::set<int>& made)
	{
		return {
			(int)Icons().size(),
			[](const int index) { return index != unheld; },
			[&made](const int index) -> const EditorTheme::ColourIcon*
			{
				made.insert(index);

				return index == unmade ? nullptr : &Icons()[(size_t)index];
			} };
	};
};

TEST_SUITE("Loom Icons")
{
	TEST_CASE("a source makes no icon when added, and makes one only when its glyph is first needed")
	{
		std::set<int> made;
		const Fixture fixture(Recording(made));
		REQUIRE(fixture.added);

		CHECK(made.empty());

		const ImFontGlyph* const glyph = fixture.Find(small_size, Codepoint(0));

		REQUIRE(glyph != nullptr);
		CHECK(glyph->Colored);
		CHECK(glyph->AdvanceX == (float)Icons()[0].width);
		CHECK(made == std::set<int>{ 0 });
	};

	TEST_CASE("a code point the source does not hold is not a glyph, and is never made")
	{
		std::set<int> made;
		const Fixture fixture(Recording(made));
		REQUIRE(fixture.added);

		CHECK(fixture.Find(small_size, Codepoint(unheld)) == nullptr);
		CHECK(made.count(unheld) == 0);
	};

	TEST_CASE("an icon the source cannot make is not a glyph, and the others still are")
	{
		std::set<int> made;
		const Fixture fixture(Recording(made));
		REQUIRE(fixture.added);

		CHECK(fixture.Find(small_size, Codepoint(unmade)) == nullptr);
		CHECK(fixture.Find(small_size, Codepoint(0)) != nullptr);
	};

	TEST_CASE("a source with no icons is not added")
	{
		const Fixture fixture(EditorTheme::ColourIconSource{ });

		CHECK_FALSE(fixture.added);
		CHECK_FALSE(EditorTheme::HasColourIcons(*fixture.font));
	};
}

#pragma endregion
