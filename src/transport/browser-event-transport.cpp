// SPDX-FileCopyrightText: 2026 Funkrusha
// SPDX-License-Identifier: GPL-3.0-or-later

#include "browser-event-transport.hpp"

#include <plugin-support.h>
#include <util/bmem.h>

namespace {
constexpr unsigned int request_success = 100;

// Public ABI returned by obs-websocket's call_request procedure.
struct ObsWebsocketRequestResponse {
	unsigned int status_code;
	char *comment;
	char *response_data;
};

void free_response(ObsWebsocketRequestResponse *response)
{
	if (!response)
		return;
	bfree(response->comment);
	bfree(response->response_data);
	bfree(response);
}
} // namespace

void BrowserEventTransport::initialize()
{
	calldata_t calldata{};
	if (proc_handler_call(obs_get_proc_handler(), "obs_websocket_api_get_ph", &calldata))
		obs_websocket_proc_handler_ = static_cast<proc_handler_t *>(calldata_ptr(&calldata, "ph"));
	calldata_free(&calldata);

	if (!obs_websocket_proc_handler_)
		obs_log(LOG_INFO, "native browser events unavailable: obs-websocket API was not found");
	else if (debug_logging_)
		obs_log(LOG_INFO, "native browser event transport initialized");
}

void BrowserEventTransport::shutdown()
{
	obs_websocket_proc_handler_ = nullptr;
}

bool BrowserEventTransport::emit(const char *event_name, obs_data_t *event_data)
{
	if (!obs_websocket_proc_handler_ || !event_name || !event_data)
		return false;

	obs_data_t *request_data = obs_data_create();
	obs_data_set_string(request_data, "vendorName", "obs-browser");
	obs_data_set_string(request_data, "requestType", "emit_event");
	obs_data_t *vendor_request = obs_data_create();
	obs_data_set_string(vendor_request, "event_name", event_name);
	obs_data_set_obj(vendor_request, "event_data", event_data);
	obs_data_set_obj(request_data, "requestData", vendor_request);

	calldata_t calldata{};
	calldata_set_string(&calldata, "request_type", "CallVendorRequest");
	calldata_set_string(&calldata, "request_data", obs_data_get_json(request_data));
	const bool called = proc_handler_call(obs_websocket_proc_handler_, "call_request", &calldata);
	auto *response = static_cast<ObsWebsocketRequestResponse *>(calldata_ptr(&calldata, "response"));
	const bool success = called && response && response->status_code == request_success;
	if (!success && !unavailable_logged_) {
		obs_log(LOG_WARNING, "native browser event '%s' failed%s%s", event_name,
			response && response->comment ? ": " : "",
			response && response->comment ? response->comment : "");
		unavailable_logged_ = true;
	} else if (success) {
		unavailable_logged_ = false;
	}

	free_response(response);
	calldata_free(&calldata);
	obs_data_release(vendor_request);
	obs_data_release(request_data);
	return success;
}

void BrowserEventTransport::set_debug_logging(bool enabled)
{
	debug_logging_ = enabled;
}
