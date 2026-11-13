#include "doctest.h"

#include "LayoutIni.h"

#include <string>
#include <vector>

using namespace Loom::LayoutIni;

#pragma region Helpers

namespace
{
	constexpr const char* lf_layout = "[A]\nx=1\n\n[B]\ny=2\n";
	constexpr const char* crlf_layout = "[A]\r\nx=1\r\n\r\n[B]\r\ny=2\r\n";
	constexpr const char* lf = "\n";
	constexpr const char* crlf = "\r\n";
};

#pragma endregion

TEST_SUITE("Loom LayoutIni")
{
	TEST_CASE("empty text has no sections")
	{
		CHECK(Split("").empty());
	};

	TEST_CASE("each bracketed line starts a section, and lines before the first make one with no header")
	{
		const std::vector<Section> sections = Split("; note\n[Window][A]\nPos=1,2\n\n[Window][B]\n");

		REQUIRE(sections.size() == 3);
		CHECK(sections[0].Header().empty());
		CHECK(sections[0].lines == Lines{ "; note" });
		CHECK(sections[1].Header() == "[Window][A]");
		CHECK(sections[1].lines == Lines{ "[Window][A]", "Pos=1,2", "" });
		CHECK(sections[2].Header() == "[Window][B]");
		CHECK(sections[2].lines == Lines{ "[Window][B]" });
	};

	TEST_CASE("an indented bracket is not a header")
	{
		const std::vector<Section> sections = Split("[A]\n [B]\n");

		REQUIRE(sections.size() == 1);
		CHECK(sections[0].lines == Lines{ "[A]", " [B]" });
	};

	TEST_CASE("line ends are dropped, CRLF or not, and a last line with none is kept")
	{
		CHECK(Split("[A]\r\nx=1\r\n").at(0).lines == Lines{ "[A]", "x=1" });
		CHECK(Split("[A]\nx=1").at(0).lines == Lines{ "[A]", "x=1" });
	};

	TEST_CASE("the ending is CRLF when the text holds one, else LF")
	{
		CHECK(EndingOf(crlf_layout) == crlf);
		CHECK(EndingOf("a\r\nb\n") == crlf);
		CHECK(EndingOf(lf_layout) == lf);
		CHECK(EndingOf("") == lf);
	};

	TEST_CASE("merging what was split gives the text back")
	{
		CHECK(Merge(Split(lf_layout), EndingOf(lf_layout)) == lf_layout);
		CHECK(Merge(Split(crlf_layout), EndingOf(crlf_layout)) == crlf_layout);
		CHECK(Merge(Split(lf_layout), crlf) == crlf_layout);
	};

	TEST_CASE("merging ends every line, the last one too, and no sections merge to nothing")
	{
		CHECK(Merge(Split("[A]\nx=1"), lf) == "[A]\nx=1\n");
		CHECK(Merge({ }, lf).empty());
	};

	TEST_CASE("a header is found only whole")
	{
		const std::vector<Section> sections = Split(lf_layout);

		CHECK(Named(sections, "[A]"));
		CHECK(Named(sections, "[B]"));
		CHECK(Named(sections, "[C]") == false);
		CHECK(Named(sections, "[A") == false);
	};

	TEST_CASE("reheading copies the body and leaves one blank line under it")
	{
		const Section padded = Split("[A]\nx=1\n\n\n").at(0);
		const Section tight = Split("[A]\nx=1\n").at(0);
		const Section bare = Split("[A]\n").at(0);

		CHECK(Rehead(padded, "[C]").lines == Lines{ "[C]", "x=1", "" });
		CHECK(Rehead(tight, "[C]").lines == Lines{ "[C]", "x=1", "" });
		CHECK(Rehead(bare, "[C]").lines == Lines{ "[C]", "" });
		CHECK(padded.lines == Lines{ "[A]", "x=1", "", "" });
	};
};
