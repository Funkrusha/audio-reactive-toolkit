// SPDX-FileCopyrightText: 2026 Funkrusha
// SPDX-License-Identifier: GPL-3.0-or-later

#include "websocket-server.hpp"

#include <array>
#include <chrono>
#include <cstring>
#include <string_view>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <obs.h>
#include <plugin-support.h>

namespace {
constexpr std::string_view websocket_guid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

#ifdef _WIN32
using socket_handle = SOCKET;
constexpr socket_handle invalid_socket = INVALID_SOCKET;
#else
using socket_handle = int;
constexpr socket_handle invalid_socket = -1;
#endif

int socket_error()
{
#ifdef _WIN32
	return WSAGetLastError();
#else
	return errno;
#endif
}

void close_socket(socket_handle socket)
{
#ifdef _WIN32
	closesocket(socket);
#else
	close(socket);
#endif
}

bool set_socket_blocking(socket_handle socket, bool blocking)
{
#ifdef _WIN32
	u_long mode = blocking ? 0 : 1;
	return ioctlsocket(socket, FIONBIO, &mode) == 0;
#else
	const int flags = fcntl(socket, F_GETFL, 0);
	return flags >= 0 && fcntl(socket, F_SETFL, blocking ? flags & ~O_NONBLOCK : flags | O_NONBLOCK) == 0;
#endif
}

void set_socket_timeouts(socket_handle socket)
{
#ifdef _WIN32
	DWORD timeout = 2000;
	setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&timeout), sizeof(timeout));
	setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char *>(&timeout), sizeof(timeout));
#else
	timeval timeout{2, 0};
	setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
	setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#endif
}

bool send_bytes(socket_handle socket, const char *data, size_t size)
{
	size_t sent_total = 0;
	while (sent_total < size) {
#ifdef MSG_NOSIGNAL
		constexpr int send_flags = MSG_NOSIGNAL;
#else
		constexpr int send_flags = 0;
#endif
		const auto sent = send(socket, data + sent_total, static_cast<int>(size - sent_total), send_flags);
		if (sent <= 0)
			return false;
		sent_total += static_cast<size_t>(sent);
	}
	return true;
}

std::string base64_encode(const uint8_t *data, size_t size)
{
	static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string result;
	result.reserve(((size + 2) / 3) * 4);

	for (size_t offset = 0; offset < size; offset += 3) {
		const uint32_t first = data[offset];
		const uint32_t second = offset + 1 < size ? data[offset + 1] : 0;
		const uint32_t third = offset + 2 < size ? data[offset + 2] : 0;
		const uint32_t value = (first << 16) | (second << 8) | third;
		result.push_back(alphabet[(value >> 18) & 0x3f]);
		result.push_back(alphabet[(value >> 12) & 0x3f]);
		result.push_back(offset + 1 < size ? alphabet[(value >> 6) & 0x3f] : '=');
		result.push_back(offset + 2 < size ? alphabet[value & 0x3f] : '=');
	}

	return result;
}

bool sha1(std::string_view input, std::array<uint8_t, 20> &digest)
{
	auto rotate_left = [](uint32_t value, unsigned bits) {
		return (value << bits) | (value >> (32 - bits));
	};
	std::vector<uint8_t> message(input.begin(), input.end());
	const uint64_t bit_count = static_cast<uint64_t>(message.size()) * 8;
	message.push_back(0x80);
	while (message.size() % 64 != 56)
		message.push_back(0);
	for (int shift = 56; shift >= 0; shift -= 8)
		message.push_back(static_cast<uint8_t>(bit_count >> shift));

	uint32_t h0 = 0x67452301;
	uint32_t h1 = 0xefcdab89;
	uint32_t h2 = 0x98badcfe;
	uint32_t h3 = 0x10325476;
	uint32_t h4 = 0xc3d2e1f0;
	for (size_t chunk = 0; chunk < message.size(); chunk += 64) {
		std::array<uint32_t, 80> words{};
		for (size_t index = 0; index < 16; ++index) {
			const size_t offset = chunk + index * 4;
			words[index] = static_cast<uint32_t>(message[offset]) << 24 |
				       static_cast<uint32_t>(message[offset + 1]) << 16 |
				       static_cast<uint32_t>(message[offset + 2]) << 8 | message[offset + 3];
		}
		for (size_t index = 16; index < words.size(); ++index)
			words[index] = rotate_left(
				words[index - 3] ^ words[index - 8] ^ words[index - 14] ^ words[index - 16], 1);

		uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
		for (size_t index = 0; index < words.size(); ++index) {
			uint32_t function = 0;
			uint32_t constant = 0;
			if (index < 20) {
				function = (b & c) | (~b & d);
				constant = 0x5a827999;
			} else if (index < 40) {
				function = b ^ c ^ d;
				constant = 0x6ed9eba1;
			} else if (index < 60) {
				function = (b & c) | (b & d) | (c & d);
				constant = 0x8f1bbcdc;
			} else {
				function = b ^ c ^ d;
				constant = 0xca62c1d6;
			}
			const uint32_t temporary = rotate_left(a, 5) + function + e + constant + words[index];
			e = d;
			d = c;
			c = rotate_left(b, 30);
			b = a;
			a = temporary;
		}
		h0 += a;
		h1 += b;
		h2 += c;
		h3 += d;
		h4 += e;
	}

	const std::array<uint32_t, 5> hash{h0, h1, h2, h3, h4};
	for (size_t index = 0; index < hash.size(); ++index)
		for (size_t byte = 0; byte < 4; ++byte)
			digest[index * 4 + byte] = static_cast<uint8_t>(hash[index] >> (24 - byte * 8));
	return true;
}

std::string trim(std::string value)
{
	const auto first = value.find_first_not_of(" \t\r\n");
	const auto last = value.find_last_not_of(" \t\r\n");
	return first == std::string::npos ? std::string{} : value.substr(first, last - first + 1);
}

bool perform_handshake(socket_handle client)
{
	set_socket_timeouts(client);

	std::array<char, 4096> buffer{};
	const int received = recv(client, buffer.data(), static_cast<int>(buffer.size() - 1), 0);
	if (received <= 0)
		return false;

	const std::string request(buffer.data(), static_cast<size_t>(received));
	constexpr std::string_view header = "Sec-WebSocket-Key:";
	const size_t key_start = request.find(header);
	if (key_start == std::string::npos)
		return false;
	const size_t value_start = key_start + header.size();
	const size_t value_end = request.find("\r\n", value_start);
	if (value_end == std::string::npos)
		return false;

	const std::string key = trim(request.substr(value_start, value_end - value_start));
	const std::string challenge = key + std::string(websocket_guid);
	std::array<uint8_t, 20> digest{};
	if (!sha1(challenge, digest))
		return false;

	const std::string accept = base64_encode(digest.data(), digest.size());
	const std::string response = "HTTP/1.1 101 Switching Protocols\r\n"
				     "Upgrade: websocket\r\n"
				     "Connection: Upgrade\r\n"
				     "Sec-WebSocket-Accept: " +
				     accept + "\r\n\r\n";
	return send_bytes(client, response.data(), response.size());
}

bool send_text_frame(socket_handle client, const std::string &message)
{
	if (message.size() > 65535)
		return false;

	std::array<uint8_t, 4> header{};
	header[0] = 0x81;
	size_t header_size = 2;
	if (message.size() < 126) {
		header[1] = static_cast<uint8_t>(message.size());
	} else {
		header[1] = 126;
		header[2] = static_cast<uint8_t>((message.size() >> 8) & 0xff);
		header[3] = static_cast<uint8_t>(message.size() & 0xff);
		header_size = 4;
	}

	if (!send_bytes(client, reinterpret_cast<const char *>(header.data()), header_size))
		return false;
	return send_bytes(client, message.data(), message.size());
}
} // namespace

WebSocketServer::~WebSocketServer()
{
	stop();
}

bool WebSocketServer::start(uint16_t port)
{
	if (running_.exchange(true))
		return true;
	thread_ = std::thread(&WebSocketServer::run, this, port);
	return true;
}

void WebSocketServer::stop()
{
	if (!running_.exchange(false))
		return;
	if (thread_.joinable())
		thread_.join();
}

void WebSocketServer::publish(std::string message)
{
	std::lock_guard lock(message_mutex_);
	latest_message_ = std::move(message);
	++generation_;
}

void WebSocketServer::set_debug_logging(bool enabled)
{
	debug_logging_.store(enabled, std::memory_order_relaxed);
}

void WebSocketServer::run(uint16_t port)
{
#ifdef _WIN32
	WSADATA winsock_data{};
	if (WSAStartup(MAKEWORD(2, 2), &winsock_data) != 0) {
		obs_log(LOG_ERROR, "WebSocket: WSAStartup failed");
		running_.store(false);
		return;
	}
#endif

	socket_handle listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (listener == invalid_socket) {
		obs_log(LOG_ERROR, "WebSocket: socket creation failed (%d)", socket_error());
#ifdef _WIN32
		WSACleanup();
#endif
		running_.store(false);
		return;
	}

	set_socket_blocking(listener, false);
	int reuse = 1;
#ifdef _WIN32
	setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&reuse), sizeof(reuse));
#else
	setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif

	sockaddr_in address{};
	address.sin_family = AF_INET;
	address.sin_port = htons(port);
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0 || listen(listener, 4) != 0) {
		obs_log(LOG_ERROR, "WebSocket: could not listen on 127.0.0.1:%u (%d)", port, socket_error());
		close_socket(listener);
#ifdef _WIN32
		WSACleanup();
#endif
		running_.store(false);
		return;
	}

	if (debug_logging_.load(std::memory_order_relaxed))
		obs_log(LOG_INFO, "WebSocket listening on ws://127.0.0.1:%u", port);
	struct ClientConnection {
		socket_handle socket;
		uint64_t sent_generation;
	};
	std::vector<ClientConnection> clients;

	while (running_.load()) {
		socket_handle accepted = accept(listener, nullptr, nullptr);
		if (accepted != invalid_socket) {
			set_socket_blocking(accepted, true);
#if defined(SO_NOSIGPIPE)
			int no_sigpipe = 1;
			setsockopt(accepted, SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe, sizeof(no_sigpipe));
#endif
			if (!perform_handshake(accepted)) {
				close_socket(accepted);
			} else {
				if (debug_logging_.load(std::memory_order_relaxed))
					obs_log(LOG_INFO, "WebSocket browser client connected");
				clients.push_back({accepted, 0});
			}
		}

		std::string message;
		uint64_t generation = 0;
		{
			std::lock_guard lock(message_mutex_);
			generation = generation_;
			message = latest_message_;
		}

		for (auto client = clients.begin(); client != clients.end();) {
			if (generation != client->sent_generation && !message.empty()) {
				if (!send_text_frame(client->socket, message)) {
					if (debug_logging_.load(std::memory_order_relaxed))
						obs_log(LOG_INFO, "WebSocket browser client disconnected");
					close_socket(client->socket);
					client = clients.erase(client);
					continue;
				} else {
					client->sent_generation = generation;
				}
			}
			++client;
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}

	for (const auto &client : clients)
		close_socket(client.socket);
	close_socket(listener);
#ifdef _WIN32
	WSACleanup();
#endif
	if (debug_logging_.load(std::memory_order_relaxed))
		obs_log(LOG_INFO, "WebSocket stopped");
}
