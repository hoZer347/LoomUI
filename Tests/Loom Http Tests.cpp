#include "doctest.h"

#include "Http.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <chrono>
#include <string>
#include <string_view>
#include <thread>

#pragma comment(lib, "ws2_32.lib")

using namespace Loom;

#pragma region Helpers

namespace
{
	constexpr const char* unresolvable_url = "http://corrin-code-tests.invalid/";
	constexpr const char* unresolvable_socket_url = "ws://corrin-code-tests.invalid/";
	constexpr const char* probe_body = "probe-body";
	constexpr const char* reply_body = "pong";
	constexpr int reply_status = 200;
	constexpr int request_buffer_size = 4096;
	constexpr int backlog = 1;
	constexpr const char* loopback_address = "127.0.0.1";
	constexpr std::string_view header_end = "\r\n\r\n";
	constexpr std::string_view length_prefix = "Content-Length: ";
	constexpr std::string_view reply_text =
		"HTTP/1.1 200 OK\r\nX-Answer: yes\r\nContent-Length: 4\r\nConnection: close\r\n\r\npong";

	class Loopback final
	{
	public:
		Loopback()
		{
			WSADATA data;
			WSAStartup(MAKEWORD(2, 2), &data);

			m_listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

			sockaddr_in address = {};
			address.sin_family = AF_INET;
			inet_pton(AF_INET, loopback_address, &address.sin_addr);
			bind(m_listener, (const sockaddr*)&address, sizeof(address));
			listen(m_listener, backlog);

			int length = sizeof(address);
			getsockname(m_listener, (sockaddr*)&address, &length);
			m_port = ntohs(address.sin_port);

			m_thread = std::thread([this]() { Serve(); });
		};

		~Loopback()
		{
			closesocket(m_listener);
			m_thread.join();
			WSACleanup();
		};

		Loopback(const Loopback&) = delete;
		Loopback& operator=(const Loopback&) = delete;

		std::string Url() const
		{
			return std::string("http://") + loopback_address + ":" + std::to_string(m_port) + "/probe";
		};

		std::string Request() const
		{
			return m_request;
		};

	private:
		void Serve()
		{
			const SOCKET client = accept(m_listener, nullptr, nullptr);

			if (client == INVALID_SOCKET)
				return;

			char buffer[request_buffer_size];

			while (!Complete())
			{
				const int received = recv(client, buffer, sizeof(buffer), 0);

				if (received <= 0)
					break;

				m_request.append(buffer, (size_t)received);
			};

			send(client, reply_text.data(), (int)reply_text.size(), 0);
			shutdown(client, SD_BOTH);
			closesocket(client);
		};

		bool Complete() const
		{
			const size_t end = m_request.find(header_end);

			if (end == std::string::npos)
				return false;

			const size_t at = m_request.find(length_prefix);
			const size_t length = at == std::string::npos ? 0 : (size_t)std::stoul(m_request.substr(at + length_prefix.size()));

			return m_request.size() >= end + header_end.size() + length;
		};

		SOCKET m_listener = INVALID_SOCKET;
		int m_port = 0;
		std::string m_request;
		std::thread m_thread;
	};

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

	TEST_CASE("a send to a host that does not resolve has no status")
	{
		Http::Call call;
		call.url = unresolvable_url;
		call.connectTimeoutMs = short_timeout_ms;
		call.sendTimeoutMs = short_timeout_ms;
		call.receiveTimeoutMs = short_timeout_ms;

		const Http::Reply reply = Http::Send(call);

		CHECK(reply.status == 0);
		CHECK(reply.body.empty());
	};

	TEST_CASE("a send reads the status, headers and body of the reply, and sends its body and headers")
	{
		const Loopback server;
		Http::Call call;
		call.method = L"POST";
		call.url = server.Url();
		call.headers = L"X-Probe: seen\r\n";
		call.body = probe_body;
		call.agent = test_agent;
		call.connectTimeoutMs = short_timeout_ms;
		call.sendTimeoutMs = short_timeout_ms;
		call.receiveTimeoutMs = short_timeout_ms;

		const Http::Reply reply = Http::Send(call);
		const std::string request = server.Request();

		CHECK(reply.status == reply_status);
		CHECK(reply.body == reply_body);
		CHECK(reply.headers.at("x-answer") == "yes");
		CHECK(request.starts_with("POST /probe HTTP/1.1"));
		CHECK(request.find("X-Probe: seen") != std::string::npos);
		CHECK(request.ends_with(probe_body));
	};

	TEST_CASE("a send stops reading the reply at its limit")
	{
		const Loopback server;
		Http::Call call;
		call.url = server.Url();
		call.connectTimeoutMs = short_timeout_ms;
		call.sendTimeoutMs = short_timeout_ms;
		call.receiveTimeoutMs = short_timeout_ms;
		call.replyLimit = 2;

		const Http::Reply reply = Http::Send(call);

		CHECK(reply.status == reply_status);
		CHECK(reply.body.size() <= std::string_view(reply_body).size());
	};

	TEST_CASE("a web socket that is not open reads and writes nothing")
	{
		Http::WebSocket socket({ .agent = test_agent, .connectTimeoutMs = short_timeout_ms, .handshakeTimeoutMs = short_timeout_ms });

		CHECK(!socket.Write("hello"));
		CHECK(!socket.Receive(std::chrono::milliseconds(0)).has_value());
		CHECK(socket.CloseCode() == 0);
		socket.Close();
	};

	TEST_CASE("a web socket that cannot reach its host does not open")
	{
		Http::WebSocket socket({ .agent = test_agent, .connectTimeoutMs = short_timeout_ms, .handshakeTimeoutMs = short_timeout_ms });

		CHECK(!socket.Open(unresolvable_socket_url));
		CHECK(!socket.Open(""));
		CHECK(!socket.Receive(std::chrono::milliseconds(0)).has_value());
	};
}
