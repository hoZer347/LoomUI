#pragma once

#include <string>


namespace Loom::Http
{
	/**
	* Loom::Http
	* - A blocking HTTPS GET over WinHTTP
	*/

	struct Request final
	{
		std::wstring host;
		std::wstring path;

		// Lines ending in "\r\n", as WinHTTP takes them.
		std::wstring headers;
		std::wstring agent;
		int timeoutMs = 5000;
	};

	// The reply body of a 200, with redirects refused. Empty on any failure or other status.
	std::string Get(const Request& request);
};
