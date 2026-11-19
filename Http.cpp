#include "Http.h"

#include <memory>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <winhttp.h>


namespace Loom::Http
{
	namespace
	{
		constexpr DWORD status_ok = 200;
		constexpr DWORD no_redirects = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;

		struct CloseInternet
		{
			void operator()(HINTERNET handle) const { WinHttpCloseHandle(handle); };
		};

		using Internet = std::unique_ptr<void, CloseInternet>;
	};

	std::string Get(const Request& asked)
	{
		const Internet session(WinHttpOpen(
			asked.agent.c_str(),
			WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
			WINHTTP_NO_PROXY_NAME,
			WINHTTP_NO_PROXY_BYPASS,
			0));

		if (!session || !WinHttpSetTimeouts(session.get(), asked.timeoutMs, asked.timeoutMs, asked.timeoutMs, asked.timeoutMs))
			return { };

		const Internet connection(WinHttpConnect(session.get(), asked.host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0));

		if (!connection)
			return { };

		const Internet request(WinHttpOpenRequest(
			connection.get(),
			L"GET",
			asked.path.c_str(),
			nullptr,
			WINHTTP_NO_REFERER,
			WINHTTP_DEFAULT_ACCEPT_TYPES,
			WINHTTP_FLAG_SECURE));

		if (!request)
			return { };

		DWORD redirects = no_redirects;

		if (!WinHttpSetOption(request.get(), WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof(redirects)))
			return { };

		const std::wstring& headers = asked.headers;

		if (!WinHttpSendRequest(request.get(), headers.c_str(), (DWORD)headers.size(), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
			!WinHttpReceiveResponse(request.get(), nullptr))
			return { };

		DWORD status = 0;
		DWORD status_size = sizeof(status);

		if (!WinHttpQueryHeaders(
				request.get(),
				WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
				WINHTTP_HEADER_NAME_BY_INDEX,
				&status,
				&status_size,
				WINHTTP_NO_HEADER_INDEX) ||
			status != status_ok)
			return { };

		std::string reply;
		DWORD available = 0;

		while (WinHttpQueryDataAvailable(request.get(), &available) && available > 0)
		{
			const size_t start = reply.size();
			DWORD read = 0;

			reply.resize(start + available);

			if (!WinHttpReadData(request.get(), reply.data() + start, available, &read))
				return { };

			reply.resize(start + read);
		};

		return reply;
	};
};
