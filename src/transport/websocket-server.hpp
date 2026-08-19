// SPDX-FileCopyrightText: 2026 Funkrusha
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

class WebSocketServer {
public:
	WebSocketServer() = default;
	~WebSocketServer();

	WebSocketServer(const WebSocketServer &) = delete;
	WebSocketServer &operator=(const WebSocketServer &) = delete;

	bool start(uint16_t port);
	void stop();
	void publish(std::string message);
	bool has_clients() const;
	void set_debug_logging(bool enabled);

private:
	void run(uint16_t port);

	std::atomic<bool> running_{false};
	std::atomic<bool> debug_logging_{false};
	std::atomic<uint32_t> client_count_{0};
	std::thread thread_;
	std::mutex message_mutex_;
	std::condition_variable message_ready_;
	std::string latest_message_;
	uint64_t generation_ = 0;
};
