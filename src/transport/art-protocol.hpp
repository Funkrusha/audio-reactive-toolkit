// SPDX-FileCopyrightText: 2026 Funkrusha
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "analysis/tempo-estimator.hpp"
#include "audio/audio-capture.hpp"

#include <array>
#include <cstdint>
#include <string>

#include <obs.h>

namespace ArtProtocol {
inline constexpr int64_t schema_version = 1;

namespace Event {
inline constexpr char frame[] = "artFrame";
} // namespace Event

namespace Field {
inline constexpr char version[] = "version";
inline constexpr char timestamp[] = "timestamp";
inline constexpr char sent_at[] = "sentAt";
inline constexpr char sequence[] = "sequence";
inline constexpr char active[] = "active";
inline constexpr char rms[] = "rms";
inline constexpr char peak[] = "peak";
inline constexpr char bands[] = "bands";
inline constexpr char bass[] = "bass";
inline constexpr char mid[] = "mid";
inline constexpr char high[] = "high";
inline constexpr char beat[] = "beat";
inline constexpr char transient[] = "transient";
inline constexpr char tempo[] = "tempo";
inline constexpr char fft32[] = "fft32";
inline constexpr char detected[] = "detected";
inline constexpr char strength[] = "strength";
inline constexpr char bpm[] = "bpm";
inline constexpr char confidence[] = "confidence";
inline constexpr char locked[] = "locked";
inline constexpr char native_spectrum_value[] = "v";
} // namespace Field

struct Frame {
	int64_t timestamp = 0;
	int64_t sent_at = 0;
	uint64_t sequence = 0;
	bool active = false;
	float rms = 0.0F;
	float peak = 0.0F;
	float bass = 0.0F;
	float mid = 0.0F;
	float high = 0.0F;
	bool beat_detected = false;
	float beat_strength = 0.0F;
	bool transient_detected = false;
	float transient_strength = 0.0F;
	float bpm = 0.0F;
	float bpm_confidence = 0.0F;
	bool bpm_locked = false;
	std::array<float, AnalysisSnapshot::fft_band_count> fft32{};
};

Frame make_frame(const AnalysisSnapshot &snapshot, const TempoSnapshot &tempo, bool beat_detected,
		 bool transient_detected, int64_t timestamp, int64_t sent_at, uint64_t sequence);
obs_data_t *serialize_native(const Frame &frame);
std::string serialize_websocket(const Frame &frame);
} // namespace ArtProtocol
