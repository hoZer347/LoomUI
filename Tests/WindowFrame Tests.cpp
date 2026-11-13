#include "doctest.h"

#include "WindowFrame.h"

TEST_CASE("IconSide is 24 pixels at 100% and grows with the scale")
{
	CHECK(Loom::WindowFrame::IconSide(96) == 24);
	CHECK(Loom::WindowFrame::IconSide(120) == 30);
	CHECK(Loom::WindowFrame::IconSide(144) == 36);
	CHECK(Loom::WindowFrame::IconSide(168) == 42);
	CHECK(Loom::WindowFrame::IconSide(192) == 48);
}
