// SPDX-FileCopyrightText: 2026 Funkrusha
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct ObsTextSourceOption {
	std::string name;
	std::string uuid;
};

struct SettingsDialogResult {
	bool accepted = false;
	std::string source_name;
	std::string bpm_text_source_name;
	std::string bpm_text_source_uuid;
	std::string bpm_text_format = "{bpm} BPM";
	uint32_t bpm_decimal_places = 1;
	uint16_t websocket_port = 8765;
	uint32_t websocket_messages_per_second = 30;
	uint32_t fft_size = 8192;
	uint32_t beat_sensitivity = 100;
	uint32_t beat_cooldown_ms = 240;
	uint32_t transient_sensitivity = 100;
	uint32_t transient_cooldown_ms = 70;
	bool debug_logging = false;
};

SettingsDialogResult
show_settings_dialog(void *parent, const std::vector<std::string> &audio_sources,
		     const std::vector<ObsTextSourceOption> &text_sources, const std::string &selected_source,
		     const std::string &selected_bpm_text_source, const std::string &selected_bpm_text_source_uuid,
		     const std::string &bpm_text_format, uint32_t bpm_decimal_places, uint16_t websocket_port,
		     uint32_t websocket_messages_per_second, uint32_t fft_size, uint32_t beat_sensitivity,
		     uint32_t beat_cooldown_ms, uint32_t transient_sensitivity, uint32_t transient_cooldown_ms,
		     bool debug_logging);
