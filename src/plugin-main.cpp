// SPDX-FileCopyrightText: 2026 Funkrusha
// SPDX-License-Identifier: GPL-3.0-or-later

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>
#include <util/platform.h>

#include "audio/audio-capture.hpp"
#include "analysis/tempo-estimator.hpp"
#include "settings.hpp"
#include "transport/art-protocol.hpp"
#include "transport/browser-event-transport.hpp"
#include "transport/websocket-server.hpp"
#include "ui/settings-dialog.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

namespace {
AudioCapture audio_capture;
WebSocketServer websocket_server;
BrowserEventTransport browser_event_transport;
TempoEstimator tempo_estimator;
float websocket_publish_timer = 0.0F;
std::string selected_source_name;
uint16_t websocket_port = 8765;
uint32_t websocket_messages_per_second = 30;
TransportMode transport_mode = TransportMode::WebSocketOnly;
uint32_t fft_size = 8192;
uint32_t beat_sensitivity = 100;
uint32_t beat_cooldown_ms = 240;
uint32_t transient_sensitivity = 100;
uint32_t transient_cooldown_ms = 70;
bool debug_logging = false;
uint64_t last_published_beat = 0;
uint64_t last_published_transient = 0;
bool pending_beat = false;
bool pending_transient = false;
std::string selected_bpm_text_source_name = "ART_BPM";
std::string selected_bpm_text_source_uuid;
std::string bpm_text_format = "{bpm} BPM";
uint32_t bpm_decimal_places = 1;
std::string last_bpm_text;
float bpm_text_update_timer = 1.0F;
bool bpm_text_source_warning_logged = false;
uint64_t websocket_sequence = 0;
uint64_t native_frame_sequence = 0;

bool native_transport_enabled(TransportMode mode)
{
	return mode != TransportMode::WebSocketOnly;
}

bool websocket_transport_enabled(TransportMode mode)
{
	return mode != TransportMode::NativeOnly;
}

void publish_native_events(const AnalysisSnapshot &snapshot, const TempoSnapshot &tempo, bool beat_detected,
			   bool transient_detected, long long timestamp, long long sent_at)
{
	const ArtProtocol::Frame protocol_frame = ArtProtocol::make_frame(
		snapshot, tempo, beat_detected, transient_detected, timestamp, sent_at, ++native_frame_sequence);
	obs_data_t *frame = ArtProtocol::serialize_native(protocol_frame);
	browser_event_transport.emit(ArtProtocol::Event::frame, frame);
	obs_data_release(frame);
}

void update_bpm_text_source(const TempoSnapshot &tempo)
{
	if (selected_bpm_text_source_name.empty())
		return;
	if (bpm_text_update_timer < 0.25F)
		return;
	bpm_text_update_timer = 0.0F;

	char bpm_value[32];
	if (tempo.bpm > 0.0F)
		std::snprintf(bpm_value, sizeof(bpm_value), "%.*f", static_cast<int>(bpm_decimal_places), tempo.bpm);
	else
		std::snprintf(bpm_value, sizeof(bpm_value), "--");
	std::string text = bpm_text_format.empty() ? "{bpm} BPM" : bpm_text_format;
	const std::string placeholder = "{bpm}";
	size_t position = 0;
	while ((position = text.find(placeholder, position)) != std::string::npos) {
		text.replace(position, placeholder.size(), bpm_value);
		position += std::char_traits<char>::length(bpm_value);
	}
	if (last_bpm_text == text)
		return;

	obs_source_t *source = selected_bpm_text_source_uuid.empty()
				       ? obs_get_source_by_name(selected_bpm_text_source_name.c_str())
				       : obs_get_source_by_uuid(selected_bpm_text_source_uuid.c_str());
	if (!source) {
		if (debug_logging && !bpm_text_source_warning_logged)
			obs_log(LOG_INFO, "BPM text source '%s' not found", selected_bpm_text_source_name.c_str());
		bpm_text_source_warning_logged = true;
		return;
	}
	if (selected_bpm_text_source_uuid.empty())
		selected_bpm_text_source_uuid = obs_source_get_uuid(source);

	const char *source_id = obs_source_get_unversioned_id(source);
	const bool is_text_source =
		source_id && (std::string(source_id) == "text_gdiplus" || std::string(source_id) == "text_ft2_source");
	if (!is_text_source) {
		if (!bpm_text_source_warning_logged)
			obs_log(LOG_WARNING, "source '%s' is not an OBS text source",
				selected_bpm_text_source_name.c_str());
		bpm_text_source_warning_logged = true;
		obs_source_release(source);
		return;
	}

	obs_data_t *settings = obs_source_get_settings(source);
	obs_data_set_string(settings, "text", text.c_str());
	obs_source_update(source, settings);
	obs_data_release(settings);
	obs_source_release(source);
	last_bpm_text = text;
	bpm_text_source_warning_logged = false;
}

void load_settings()
{
	char *path = obs_module_config_path(Settings::file_name);
	obs_data_t *settings = obs_data_create_from_json_file_safe(path, Settings::backup_extension);
	if (settings) {
		selected_source_name = obs_data_get_string(settings, Settings::Field::audio_source);
		if (obs_data_has_user_value(settings, Settings::Field::bpm_text_source))
			selected_bpm_text_source_name = obs_data_get_string(settings, Settings::Field::bpm_text_source);
		if (obs_data_has_user_value(settings, Settings::Field::bpm_text_source_uuid))
			selected_bpm_text_source_uuid =
				obs_data_get_string(settings, Settings::Field::bpm_text_source_uuid);
		if (obs_data_has_user_value(settings, Settings::Field::bpm_text_format)) {
			bpm_text_format = obs_data_get_string(settings, Settings::Field::bpm_text_format);
			if (bpm_text_format.empty())
				bpm_text_format = "{bpm} BPM";
		}
		if (obs_data_has_user_value(settings, Settings::Field::bpm_decimal_places)) {
			const int64_t saved_bpm_decimal_places =
				obs_data_get_int(settings, Settings::Field::bpm_decimal_places);
			if (saved_bpm_decimal_places >= 0 && saved_bpm_decimal_places <= 2)
				bpm_decimal_places = static_cast<uint32_t>(saved_bpm_decimal_places);
		}
		const int64_t saved_port = obs_data_get_int(settings, Settings::Field::websocket_port);
		if (saved_port >= 1024 && saved_port <= 65535)
			websocket_port = static_cast<uint16_t>(saved_port);
		const int64_t saved_message_rate =
			obs_data_get_int(settings, Settings::Field::websocket_messages_per_second);
		const int64_t saved_fft_size = obs_data_get_int(settings, Settings::Field::fft_size);
		if (saved_message_rate >= 1 && saved_message_rate <= 60)
			websocket_messages_per_second = static_cast<uint32_t>(saved_message_rate);
		if (obs_data_has_user_value(settings, Settings::Field::transport_mode)) {
			const int64_t saved_transport_mode =
				obs_data_get_int(settings, Settings::Field::transport_mode);
			if (saved_transport_mode >= static_cast<int64_t>(TransportMode::Both) &&
			    saved_transport_mode <= static_cast<int64_t>(TransportMode::WebSocketOnly))
				transport_mode = static_cast<TransportMode>(saved_transport_mode);
		}
		if (saved_fft_size == 2048 || saved_fft_size == 4096 || saved_fft_size == 8192 ||
		    saved_fft_size == 16384)
			fft_size = static_cast<uint32_t>(saved_fft_size);
		const int64_t saved_beat_sensitivity = obs_data_get_int(settings, Settings::Field::beat_sensitivity);
		const int64_t saved_beat_cooldown = obs_data_get_int(settings, Settings::Field::beat_cooldown_ms);
		const int64_t saved_transient_sensitivity =
			obs_data_get_int(settings, Settings::Field::transient_sensitivity);
		const int64_t saved_transient_cooldown =
			obs_data_get_int(settings, Settings::Field::transient_cooldown_ms);
		if (saved_beat_sensitivity >= 25 && saved_beat_sensitivity <= 200)
			beat_sensitivity = static_cast<uint32_t>(saved_beat_sensitivity);
		if (saved_beat_cooldown >= 80 && saved_beat_cooldown <= 1000)
			beat_cooldown_ms = static_cast<uint32_t>(saved_beat_cooldown);
		if (saved_transient_sensitivity >= 25 && saved_transient_sensitivity <= 200)
			transient_sensitivity = static_cast<uint32_t>(saved_transient_sensitivity);
		if (saved_transient_cooldown >= 20 && saved_transient_cooldown <= 500)
			transient_cooldown_ms = static_cast<uint32_t>(saved_transient_cooldown);
		debug_logging = obs_data_get_bool(settings, Settings::Field::debug_logging);
		obs_data_release(settings);
	}
	bfree(path);
}

void save_settings()
{
	char *directory = obs_module_config_path("");
	os_mkdirs(directory);
	bfree(directory);
	char *path = obs_module_config_path(Settings::file_name);
	obs_data_t *settings = obs_data_create();
	obs_data_set_string(settings, Settings::Field::audio_source, selected_source_name.c_str());
	obs_data_set_string(settings, Settings::Field::bpm_text_source, selected_bpm_text_source_name.c_str());
	obs_data_set_string(settings, Settings::Field::bpm_text_source_uuid, selected_bpm_text_source_uuid.c_str());
	obs_data_set_string(settings, Settings::Field::bpm_text_format, bpm_text_format.c_str());
	obs_data_set_int(settings, Settings::Field::bpm_decimal_places, bpm_decimal_places);
	obs_data_set_int(settings, Settings::Field::websocket_port, websocket_port);
	obs_data_set_int(settings, Settings::Field::websocket_messages_per_second, websocket_messages_per_second);
	obs_data_set_int(settings, Settings::Field::transport_mode, static_cast<int64_t>(transport_mode));
	obs_data_set_int(settings, Settings::Field::fft_size, fft_size);
	obs_data_set_int(settings, Settings::Field::beat_sensitivity, beat_sensitivity);
	obs_data_set_int(settings, Settings::Field::beat_cooldown_ms, beat_cooldown_ms);
	obs_data_set_int(settings, Settings::Field::transient_sensitivity, transient_sensitivity);
	obs_data_set_int(settings, Settings::Field::transient_cooldown_ms, transient_cooldown_ms);
	obs_data_set_bool(settings, Settings::Field::debug_logging, debug_logging);
	if (!obs_data_save_json_safe(settings, path, Settings::temporary_extension, Settings::backup_extension))
		obs_log(LOG_WARNING, "could not save settings to '%s'", path);
	obs_data_release(settings);
	bfree(path);
}

void attach_configured_source()
{
	if (selected_source_name.empty() || !audio_capture.attach_source_by_name(selected_source_name.c_str()))
		audio_capture.attach_first_output_source();
}

bool collect_audio_sources(void *context, obs_source_t *source)
{
	if ((obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO) != 0)
		static_cast<std::vector<std::string> *>(context)->emplace_back(obs_source_get_name(source));
	return true;
}

bool collect_text_sources(void *context, obs_source_t *source)
{
	const char *source_id = obs_source_get_unversioned_id(source);
	if (source_id && (std::string(source_id) == "text_gdiplus" || std::string(source_id) == "text_ft2_source"))
		static_cast<std::vector<ObsTextSourceOption> *>(context)->push_back(
			{obs_source_get_name(source), obs_source_get_uuid(source)});
	return true;
}

void show_settings(void *)
{
	std::vector<std::string> sources;
	std::vector<ObsTextSourceOption> text_sources;
	obs_enum_sources(collect_audio_sources, &sources);
	obs_enum_sources(collect_text_sources, &text_sources);
	std::sort(sources.begin(), sources.end());
	std::sort(text_sources.begin(), text_sources.end(),
		  [](const ObsTextSourceOption &left, const ObsTextSourceOption &right) {
			  return left.name < right.name;
		  });

	const SettingsDialogResult result = show_settings_dialog(
		obs_frontend_get_main_window(), sources, text_sources, selected_source_name,
		selected_bpm_text_source_name, selected_bpm_text_source_uuid, bpm_text_format, bpm_decimal_places,
		websocket_port, websocket_messages_per_second, transport_mode, fft_size, beat_sensitivity,
		beat_cooldown_ms, transient_sensitivity, transient_cooldown_ms, debug_logging);
	if (!result.accepted)
		return;
	debug_logging = result.debug_logging;
	audio_capture.set_debug_logging(debug_logging);
	websocket_server.set_debug_logging(debug_logging);
	browser_event_transport.set_debug_logging(debug_logging);
	if (selected_bpm_text_source_uuid != result.bpm_text_source_uuid ||
	    selected_bpm_text_source_name != result.bpm_text_source_name) {
		selected_bpm_text_source_name = result.bpm_text_source_name;
		selected_bpm_text_source_uuid = result.bpm_text_source_uuid;
		last_bpm_text.clear();
		bpm_text_update_timer = 1.0F;
		bpm_text_source_warning_logged = false;
	}
	if (bpm_text_format != result.bpm_text_format || bpm_decimal_places != result.bpm_decimal_places) {
		bpm_text_format = result.bpm_text_format;
		bpm_decimal_places = result.bpm_decimal_places;
		last_bpm_text.clear();
		bpm_text_update_timer = 1.0F;
	}

	if (result.source_name.empty()) {
		selected_source_name.clear();
		audio_capture.attach_first_output_source();
	} else if (audio_capture.attach_source_by_name(result.source_name.c_str())) {
		selected_source_name = result.source_name;
	}

	const bool websocket_was_enabled = websocket_transport_enabled(transport_mode);
	const bool websocket_will_be_enabled = websocket_transport_enabled(result.transport_mode);
	const bool websocket_port_changed = result.websocket_port != websocket_port;
	if (websocket_was_enabled && (!websocket_will_be_enabled || websocket_port_changed))
		websocket_server.stop();
	websocket_port = result.websocket_port;
	transport_mode = result.transport_mode;
	if (websocket_will_be_enabled && (!websocket_was_enabled || websocket_port_changed))
		websocket_server.start(websocket_port);
	websocket_messages_per_second = result.websocket_messages_per_second;
	websocket_publish_timer = 0.0F;
	fft_size = result.fft_size;
	audio_capture.set_fft_size(fft_size);
	beat_sensitivity = result.beat_sensitivity;
	beat_cooldown_ms = result.beat_cooldown_ms;
	transient_sensitivity = result.transient_sensitivity;
	transient_cooldown_ms = result.transient_cooldown_ms;
	audio_capture.configure_detection(beat_sensitivity, beat_cooldown_ms, transient_sensitivity,
					  transient_cooldown_ms);
	save_settings();
}

void frontend_event(enum obs_frontend_event event, void *)
{
	if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING || event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED)
		attach_configured_source();
	else if (event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP)
		audio_capture.detach();
}

void plugin_tick(void *, float seconds)
{
	audio_capture.tick(seconds);
	tempo_estimator.update(seconds);
	bpm_text_update_timer += seconds;
	const AnalysisSnapshot snapshot = audio_capture.snapshot();
	if (snapshot.beat_counter != last_published_beat) {
		tempo_estimator.add_beat(snapshot.beat_timestamp);
		pending_beat = true;
		last_published_beat = snapshot.beat_counter;
	}
	if (snapshot.transient_counter != last_published_transient) {
		pending_transient = true;
		last_published_transient = snapshot.transient_counter;
	}
	websocket_publish_timer += seconds;
	const float websocket_publish_interval = 1.0F / static_cast<float>(websocket_messages_per_second);
	if (websocket_publish_timer < websocket_publish_interval)
		return;
	websocket_publish_timer = std::max(0.0F, websocket_publish_timer - websocket_publish_interval);

	const bool beat_detected = pending_beat;
	const bool transient_detected = pending_transient;
	pending_beat = false;
	pending_transient = false;
	const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
				       std::chrono::steady_clock::now().time_since_epoch())
				       .count();
	const TempoSnapshot tempo = tempo_estimator.snapshot();
	update_bpm_text_source(tempo);
	if (native_transport_enabled(transport_mode)) {
		const auto native_sent_at = std::chrono::duration_cast<std::chrono::milliseconds>(
						    std::chrono::system_clock::now().time_since_epoch())
						    .count();
		publish_native_events(snapshot, tempo, beat_detected, transient_detected,
				      static_cast<long long>(timestamp), static_cast<long long>(native_sent_at));
	}
	if (!websocket_transport_enabled(transport_mode) || !websocket_server.has_clients())
		return;
	const auto websocket_timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
						 std::chrono::system_clock::now().time_since_epoch())
						 .count();
	const ArtProtocol::Frame protocol_frame = ArtProtocol::make_frame(snapshot, tempo, beat_detected,
									  transient_detected, timestamp,
									  websocket_timestamp, ++websocket_sequence);
	websocket_server.publish(ArtProtocol::serialize_websocket(protocol_frame));
}
} // namespace

MODULE_EXPORT const char *obs_module_description(void)
{
	return obs_module_text("PluginDescription");
}

bool obs_module_load(void)
{
	load_settings();
	audio_capture.set_debug_logging(debug_logging);
	audio_capture.set_fft_size(fft_size);
	websocket_server.set_debug_logging(debug_logging);
	browser_event_transport.set_debug_logging(debug_logging);
	audio_capture.configure_detection(beat_sensitivity, beat_cooldown_ms, transient_sensitivity,
					  transient_cooldown_ms);
	if (websocket_transport_enabled(transport_mode))
		websocket_server.start(websocket_port);
	obs_frontend_add_event_callback(frontend_event, nullptr);
	obs_frontend_add_tools_menu_item(obs_module_text("Tools.Settings"), show_settings, nullptr);
	obs_add_tick_callback(plugin_tick, nullptr);
	obs_log(LOG_INFO, "loaded successfully (version %s)", PLUGIN_VERSION);
	return true;
}

MODULE_EXPORT void obs_module_post_load(void)
{
	browser_event_transport.initialize();
}

void obs_module_unload(void)
{
	obs_remove_tick_callback(plugin_tick, nullptr);
	obs_frontend_remove_event_callback(frontend_event, nullptr);
	audio_capture.detach();
	browser_event_transport.shutdown();
	websocket_server.stop();
	if (debug_logging)
		obs_log(LOG_INFO, "unloaded");
}
