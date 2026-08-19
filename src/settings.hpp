// SPDX-FileCopyrightText: 2026 Funkrusha
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace Settings {
inline constexpr char file_name[] = "settings.json";
inline constexpr char temporary_extension[] = "tmp";
inline constexpr char backup_extension[] = "bak";

namespace Field {
inline constexpr char audio_source[] = "audio_source";
inline constexpr char bpm_text_source[] = "bpm_text_source";
inline constexpr char bpm_text_source_uuid[] = "bpm_text_source_uuid";
inline constexpr char bpm_text_format[] = "bpm_text_format";
inline constexpr char bpm_decimal_places[] = "bpm_decimal_places";
inline constexpr char websocket_port[] = "websocket_port";
inline constexpr char websocket_messages_per_second[] = "websocket_messages_per_second";
inline constexpr char transport_mode[] = "transport_mode";
inline constexpr char fft_size[] = "fft_size";
inline constexpr char beat_sensitivity[] = "beat_sensitivity";
inline constexpr char beat_cooldown_ms[] = "beat_cooldown_ms";
inline constexpr char transient_sensitivity[] = "transient_sensitivity";
inline constexpr char transient_cooldown_ms[] = "transient_cooldown_ms";
inline constexpr char debug_logging[] = "debug_logging";
} // namespace Field
} // namespace Settings
