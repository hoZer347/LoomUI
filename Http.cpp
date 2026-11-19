#include "Http.h"

#include "Text.h"

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

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
		constexpr DWORD switching_protocols = 101;
		constexpr DWORD no_timeout = 0;
		constexpr size_t receive_chunk = 16 * 1024;
		constexpr std::string_view websocket_secure = "wss://";
		constexpr std::string_view websocket_plain = "ws://";
		constexpr std::string_view http_secure = "https";
		constexpr std::string_view http_plain = "http";
		constexpr size_t secure_scheme_cut = 3;
		constexpr size_t plain_scheme_cut = 2;

		struct CloseInternet
		{
			void operator()(HINTERNET handle) const { WinHttpCloseHandle(handle); };
		};

		using Internet = std::unique_ptr<void, CloseInternet>;

		struct Address final
		{
			std::wstring host;
			std::wstring path;
			INTERNET_PORT port = 0;
			bool secure = false;
		};

		std::optional<Address> Crack(const std::string& url)
		{
			const std::wstring wide = Text::Wide(url);
			URL_COMPONENTS parts{ };
			parts.dwStructSize = sizeof(parts);
			parts.dwSchemeLength = static_cast<DWORD>(-1);
			parts.dwHostNameLength = static_cast<DWORD>(-1);
			parts.dwUrlPathLength = static_cast<DWORD>(-1);
			parts.dwExtraInfoLength = static_cast<DWORD>(-1);

			if (!WinHttpCrackUrl(wide.c_str(), static_cast<DWORD>(wide.size()), 0, &parts))
				return std::nullopt;

			Address address;
			address.host.assign(parts.lpszHostName, parts.dwHostNameLength);
			address.path.assign(parts.lpszUrlPath, parts.dwUrlPathLength);
			address.path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
			address.port = parts.nPort;
			address.secure = parts.nScheme == INTERNET_SCHEME_HTTPS;

			return address;
		};

		std::map<std::string, std::string> ParseHeaders(const std::wstring& raw)
		{
			std::map<std::string, std::string> headers;
			std::istringstream lines(Text::Narrow(raw));

			for (std::string line; std::getline(lines, line);)
			{
				const size_t colon = line.find(':');

				if (colon != std::string::npos && colon > 0)
					headers[Text::Lower(Text::Trim(line.substr(0, colon), " \t\r"))] = Text::Trim(line.substr(colon + 1), " \t\r");
			};

			return headers;
		};
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

	Reply Send(const Call& call)
	{
		const std::optional<Address> address = Crack(call.url);

		if (!address)
			return { };

		const Internet session(WinHttpOpen(
			call.agent.c_str(),
			WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
			WINHTTP_NO_PROXY_NAME,
			WINHTTP_NO_PROXY_BYPASS,
			0));

		if (!session || !WinHttpSetTimeouts(session.get(), call.connectTimeoutMs, call.connectTimeoutMs, call.sendTimeoutMs, call.receiveTimeoutMs))
			return { };

		const Internet connection(WinHttpConnect(session.get(), address->host.c_str(), address->port, 0));

		if (!connection)
			return { };

		const Internet request(WinHttpOpenRequest(
			connection.get(),
			call.method.c_str(),
			address->path.c_str(),
			nullptr,
			WINHTTP_NO_REFERER,
			WINHTTP_DEFAULT_ACCEPT_TYPES,
			address->secure ? WINHTTP_FLAG_SECURE : 0));

		if (!request)
			return { };

		DWORD redirects = no_redirects;

		if (!WinHttpSetOption(request.get(), WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof(redirects)))
			return { };

		const DWORD bodySize = static_cast<DWORD>(call.body.size());

		if (!WinHttpSendRequest(
				request.get(),
				call.headers.c_str(),
				static_cast<DWORD>(-1),
				call.body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(call.body.data()),
				bodySize,
				bodySize,
				0)
			|| !WinHttpReceiveResponse(request.get(), nullptr))
			return { };

		DWORD status = 0;
		DWORD statusSize = sizeof(status);

		if (!WinHttpQueryHeaders(
			request.get(),
			WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
			WINHTTP_HEADER_NAME_BY_INDEX,
			&status,
			&statusSize,
			WINHTTP_NO_HEADER_INDEX))
			return { };

		Reply reply{ static_cast<int>(status) };
		DWORD rawSize = 0;

		WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, WINHTTP_NO_OUTPUT_BUFFER, &rawSize, WINHTTP_NO_HEADER_INDEX);

		if (rawSize > 0)
		{
			std::wstring raw(rawSize / sizeof(wchar_t), L'\0');

			if (WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, raw.data(), &rawSize, WINHTTP_NO_HEADER_INDEX))
				reply.headers = ParseHeaders(raw);
		};

		DWORD available = 0;

		while (reply.body.size() < call.replyLimit && WinHttpQueryDataAvailable(request.get(), &available) && available > 0)
		{
			const size_t start = reply.body.size();
			DWORD read = 0;

			reply.body.resize(start + available);

			if (!WinHttpReadData(request.get(), reply.body.data() + start, available, &read))
				break;

			reply.body.resize(start + read);
		};

		return reply;
	};

	#pragma region WebSocket

	struct WebSocket::State final
	{
		explicit State(SocketOptions given) :
			options(std::move(given))
		{
		};

		bool Open(const std::string& url);
		void Read();
		void Fail(int code);
		bool Write(const std::string& text);
		void Close();

		SocketOptions options;
		Internet session{ };
		Internet connection{ };
		Internet socket{ };
		std::thread reader{ };
		mutable std::mutex lock{ };
		std::condition_variable wake{ };
		std::deque<std::string> messages{ };
		bool failed = true;
		int closeCode = 0;
	};

	bool WebSocket::State::Open(const std::string& url)
	{
		Close();

		std::string text = url;

		if (text.rfind(websocket_secure, 0) == 0)
			text.replace(0, secure_scheme_cut, http_secure);
		else if (text.rfind(websocket_plain, 0) == 0)
			text.replace(0, plain_scheme_cut, http_plain);

		const std::optional<Address> address = Crack(text);

		if (!address)
			return false;

		const std::wstring path = address->path.empty() ? L"/" : address->path;

		session.reset(WinHttpOpen(
			options.agent.c_str(),
			WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
			WINHTTP_NO_PROXY_NAME,
			WINHTTP_NO_PROXY_BYPASS,
			0));

		if (!session
			|| !WinHttpSetTimeouts(session.get(), 0, options.connectTimeoutMs, options.sendTimeoutMs, options.handshakeTimeoutMs))
			return false;

		connection.reset(WinHttpConnect(session.get(), address->host.c_str(), address->port, 0));

		if (!connection)
			return false;

		Internet request(WinHttpOpenRequest(
			connection.get(),
			L"GET",
			path.c_str(),
			nullptr,
			WINHTTP_NO_REFERER,
			WINHTTP_DEFAULT_ACCEPT_TYPES,
			address->secure ? WINHTTP_FLAG_SECURE : 0));

		if (!request
			|| !WinHttpSetOption(request.get(), WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0)
			|| !WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
			|| !WinHttpReceiveResponse(request.get(), nullptr))
			return false;

		DWORD status = 0;
		DWORD statusSize = sizeof(status);

		if (!WinHttpQueryHeaders(
				request.get(),
				WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
				WINHTTP_HEADER_NAME_BY_INDEX,
				&status,
				&statusSize,
				WINHTTP_NO_HEADER_INDEX)
			|| status != switching_protocols)
			return false;

		socket.reset(WinHttpWebSocketCompleteUpgrade(request.get(), 0));

		if (!socket)
			return false;

		DWORD forever = no_timeout;
		WinHttpSetOption(socket.get(), WINHTTP_OPTION_RECEIVE_TIMEOUT, &forever, sizeof(forever));

		{
			const std::lock_guard<std::mutex> guard(lock);
			failed = false;
			closeCode = 0;
			messages.clear();
		};

		reader = std::thread([this]() { Read(); });

		return true;
	};

	void WebSocket::State::Fail(int code)
	{
		{
			const std::lock_guard<std::mutex> guard(lock);

			if (code != 0)
				closeCode = code;

			failed = true;
		};

		wake.notify_all();
	};

	void WebSocket::State::Read()
	{
		std::vector<char> chunk(receive_chunk);
		std::string message;

		while (true)
		{
			DWORD read = 0;
			WINHTTP_WEB_SOCKET_BUFFER_TYPE type = WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE;

			if (WinHttpWebSocketReceive(socket.get(), chunk.data(), static_cast<DWORD>(chunk.size()), &read, &type) != NO_ERROR)
				return Fail(0);

			if (type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE)
			{
				USHORT status = 0;
				DWORD reasonSize = 0;
				WinHttpWebSocketQueryCloseStatus(socket.get(), &status, nullptr, 0, &reasonSize);

				return Fail(status);
			};

			if (type != WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE
				&& type != WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE)
				continue;

			message.append(chunk.data(), read);

			if (message.size() > options.messageLimit)
				return Fail(0);

			if (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE)
			{
				{
					const std::lock_guard<std::mutex> guard(lock);
					messages.push_back(std::move(message));
				};

				message.clear();
				wake.notify_all();
			};
		};
	};

	bool WebSocket::State::Write(const std::string& text)
	{
		{
			const std::lock_guard<std::mutex> guard(lock);

			if (failed || !socket)
				return false;
		};

		return WinHttpWebSocketSend(
			socket.get(),
			WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
			const_cast<char*>(text.data()),
			static_cast<DWORD>(text.size())) == NO_ERROR;
	};

	void WebSocket::State::Close()
	{
		{
			const std::lock_guard<std::mutex> guard(lock);
			failed = true;
		};

		if (socket)
			WinHttpWebSocketClose(socket.get(), static_cast<USHORT>(options.closeStatus), nullptr, 0);

		socket.reset();
		connection.reset();
		session.reset();

		if (reader.joinable())
			reader.join();

		wake.notify_all();
	};

	WebSocket::WebSocket(SocketOptions options) :
		m_state(std::make_unique<State>(std::move(options)))
	{
	};

	WebSocket::~WebSocket()
	{
		m_state->Close();
	};

	bool WebSocket::Open(const std::string& url)
	{
		return m_state->Open(url);
	};

	std::optional<std::string> WebSocket::Receive(std::chrono::milliseconds timeout)
	{
		std::unique_lock<std::mutex> lock(m_state->lock);

		m_state->wake.wait_for(lock, timeout, [this]() { return !m_state->messages.empty() || m_state->failed; });

		if (!m_state->messages.empty())
		{
			std::string message = std::move(m_state->messages.front());
			m_state->messages.pop_front();

			return message;
		};

		if (m_state->failed)
			return std::nullopt;

		return std::string{ };
	};

	bool WebSocket::Write(const std::string& text)
	{
		return m_state->Write(text);
	};

	void WebSocket::Close()
	{
		m_state->Close();
	};

	int WebSocket::CloseCode() const
	{
		const std::lock_guard<std::mutex> guard(m_state->lock);

		return m_state->closeCode;
	};

	#pragma endregion
};
