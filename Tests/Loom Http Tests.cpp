#include "doctest.h"

#include "Http.h"

#include <string>

using namespace Loom;

#pragma region Helpers

namespace
{
	constexpr const wchar_t* unresolvable_host = L"corrin-code-tests.invalid";
	constexpr const wchar_t* test_path = L"/";
	constexpr const wchar_t* test_agent = L"Corrin Code Tests";
	constexpr int short_timeout_ms = 1000;

	Http::Request Asking(const wchar_t* host)
	{
		return { .host = host, .path = test_path, .agent = test_agent, .timeoutMs = short_timeout_ms };
	};
};

#pragma endregion

TEST_SUITE("Loom Http")
{
	TEST_CASE("a get with no host is empty")
	{
		CHECK(Http::Get(Asking(L"")).empty());
	};

	TEST_CASE("a get from a host that does not resolve is empty")
	{
		CHECK(Http::Get(Asking(unresolvable_host)).empty());
	};
}
