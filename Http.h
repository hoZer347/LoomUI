#pragma once

#include <chrono>
#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>


namespace Loom::Http
{
	/**
	* Loom::Http
	* - A blocking HTTPS GET over WinHTTP
	* - A blocking request of any method to an http or https address
	* - A WebSocket client that reads on a thread of its own
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

	struct Call final
	{
		std::wstring method = L"GET";

		// The whole address, such as "https://host/path" or "http://127.0.0.1:8080/path?query".
		std::string url;

		// Lines separated by "\r\n", as WinHTTP takes them.
		std::wstring headers;
		std::string body;
		std::wstring agent;
		int connectTimeoutMs = 10000;
		int sendTimeoutMs = 60000;
		int receiveTimeoutMs = 30000;

		// Reading the reply stops once this much has arrived.
		size_t replyLimit = 256 * 1024;
	};

	struct Reply final
	{
		// 0 when there was no answer.
		int status = 0;
		std::string body;

		// Names in lower case.
		std::map<std::string, std::string> headers;
	};

	// Redirects are refused, so a 3xx is the reply.
	Reply Send(const Call& call);

	struct SocketOptions final
	{
		std::wstring agent;
		int connectTimeoutMs = 10000;
		int sendTimeoutMs = 60000;
		int handshakeTimeoutMs = 30000;

		// A message longer than this ends the socket.
		size_t messageLimit = 16 * 1024 * 1024;

		// The status of the close frame sent when this side closes.
		int closeStatus = 1000;
	};

	/**
	* Loom::Http::WebSocket
	* - Text messages over a ws or wss address, each read whole off a thread of its own
	* - Safe to use from two threads, one reading and one writing
	*/
	class WebSocket final
	{
	public:
		explicit WebSocket(SocketOptions options);
		~WebSocket();

		WebSocket(const WebSocket&) = delete;
		WebSocket& operator=(const WebSocket&) = delete;

		// url is the full address, query included. Closes what was open before.
		bool Open(const std::string& url);

		// The next message. Nothing once the socket is closed or failed, an empty string when timeout passed with nothing to read.
		std::optional<std::string> Receive(std::chrono::milliseconds timeout);

		bool Write(const std::string& text);
		void Close();

		// The status of the close frame the server sent, 0 when there was none.
		int CloseCode() const;

	private:
		struct State;

		std::unique_ptr<State> m_state;
	};
};
