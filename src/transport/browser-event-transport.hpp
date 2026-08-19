// SPDX-FileCopyrightText: 2026 Funkrusha
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <obs.h>

class BrowserEventTransport {
public:
	void initialize();
	void shutdown();
	bool emit(const char *event_name, obs_data_t *event_data);
	void set_debug_logging(bool enabled);

private:
	proc_handler_t *obs_websocket_proc_handler_ = nullptr;
	bool debug_logging_ = false;
	bool unavailable_logged_ = false;
};
