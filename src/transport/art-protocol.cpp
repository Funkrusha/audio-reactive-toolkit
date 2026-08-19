// SPDX-FileCopyrightText: 2026 Funkrusha
// SPDX-License-Identifier: GPL-3.0-or-later

#include "art-protocol.hpp"

#include <cstdio>

namespace {
void append_key(std::string &json, const char *key)
{
	json += '"';
	json += key;
	json += "\":";
}

void append_integer(std::string &json, int64_t value)
{
	char buffer[32];
	std::snprintf(buffer, sizeof(buffer), "%lld", static_cast<long long>(value));
	json += buffer;
}

void append_unsigned(std::string &json, uint64_t value)
{
	char buffer[32];
	std::snprintf(buffer, sizeof(buffer), "%llu", static_cast<unsigned long long>(value));
	json += buffer;
}

void append_float(std::string &json, float value, int decimal_places = 6)
{
	char buffer[32];
	std::snprintf(buffer, sizeof(buffer), "%.*f", decimal_places, value);
	json += buffer;
}

void append_bool(std::string &json, bool value)
{
	json += value ? "true" : "false";
}
} // namespace

namespace ArtProtocol {
Frame make_frame(const AnalysisSnapshot &snapshot, const TempoSnapshot &tempo, bool beat_detected,
		 bool transient_detected, int64_t timestamp, int64_t sent_at, uint64_t sequence)
{
	Frame frame;
	frame.timestamp = timestamp;
	frame.sent_at = sent_at;
	frame.sequence = sequence;
	frame.active = snapshot.active;
	frame.rms = snapshot.rms;
	frame.peak = snapshot.peak;
	frame.bass = snapshot.bass;
	frame.mid = snapshot.mid;
	frame.high = snapshot.high;
	frame.beat_detected = beat_detected;
	frame.beat_strength = snapshot.beat_strength;
	frame.transient_detected = transient_detected;
	frame.transient_strength = snapshot.transient_strength;
	frame.bpm = tempo.bpm;
	frame.bpm_confidence = tempo.confidence;
	frame.bpm_locked = tempo.locked;
	frame.fft32 = snapshot.fft_bands;
	return frame;
}

obs_data_t *serialize_native(const Frame &frame)
{
	obs_data_array_t *spectrum = obs_data_array_create();
	for (float value : frame.fft32) {
		obs_data_t *bin = obs_data_create();
		obs_data_set_double(bin, Field::native_spectrum_value, value);
		obs_data_array_push_back(spectrum, bin);
		obs_data_release(bin);
	}

	obs_data_t *data = obs_data_create();
	obs_data_set_int(data, Field::version, schema_version);
	obs_data_set_int(data, Field::timestamp, frame.timestamp);
	obs_data_set_int(data, Field::sent_at, frame.sent_at);
	obs_data_set_int(data, Field::sequence, static_cast<int64_t>(frame.sequence));
	obs_data_set_bool(data, Field::active, frame.active);
	obs_data_set_double(data, Field::rms, frame.rms);
	obs_data_set_double(data, Field::peak, frame.peak);

	obs_data_t *bands = obs_data_create();
	obs_data_set_double(bands, Field::bass, frame.bass);
	obs_data_set_double(bands, Field::mid, frame.mid);
	obs_data_set_double(bands, Field::high, frame.high);
	obs_data_set_obj(data, Field::bands, bands);
	obs_data_release(bands);

	obs_data_t *beat = obs_data_create();
	obs_data_set_bool(beat, Field::detected, frame.beat_detected);
	obs_data_set_double(beat, Field::strength, frame.beat_strength);
	obs_data_set_obj(data, Field::beat, beat);
	obs_data_release(beat);

	obs_data_t *transient = obs_data_create();
	obs_data_set_bool(transient, Field::detected, frame.transient_detected);
	obs_data_set_double(transient, Field::strength, frame.transient_strength);
	obs_data_set_obj(data, Field::transient, transient);
	obs_data_release(transient);

	obs_data_t *tempo = obs_data_create();
	obs_data_set_double(tempo, Field::bpm, frame.bpm);
	obs_data_set_double(tempo, Field::confidence, frame.bpm_confidence);
	obs_data_set_bool(tempo, Field::locked, frame.bpm_locked);
	obs_data_set_obj(data, Field::tempo, tempo);
	obs_data_release(tempo);

	obs_data_set_array(data, Field::fft32, spectrum);
	obs_data_array_release(spectrum);
	return data;
}

std::string serialize_websocket(const Frame &frame)
{
	std::string message;
	message.reserve(768);
	message += '{';
	append_key(message, Field::version);
	append_integer(message, schema_version);
	message += ',';
	append_key(message, Field::timestamp);
	append_integer(message, frame.timestamp);
	message += ',';
	append_key(message, Field::sent_at);
	append_integer(message, frame.sent_at);
	message += ',';
	append_key(message, Field::sequence);
	append_unsigned(message, frame.sequence);
	message += ',';
	append_key(message, Field::active);
	append_bool(message, frame.active);
	message += ',';
	append_key(message, Field::rms);
	append_float(message, frame.rms);
	message += ',';
	append_key(message, Field::peak);
	append_float(message, frame.peak);
	message += ',';
	append_key(message, Field::bands);
	message += '{';
	append_key(message, Field::bass);
	append_float(message, frame.bass);
	message += ',';
	append_key(message, Field::mid);
	append_float(message, frame.mid);
	message += ',';
	append_key(message, Field::high);
	append_float(message, frame.high);
	message += "},";
	append_key(message, Field::beat);
	message += '{';
	append_key(message, Field::detected);
	append_bool(message, frame.beat_detected);
	message += ',';
	append_key(message, Field::strength);
	append_float(message, frame.beat_strength);
	message += "},";
	append_key(message, Field::transient);
	message += '{';
	append_key(message, Field::detected);
	append_bool(message, frame.transient_detected);
	message += ',';
	append_key(message, Field::strength);
	append_float(message, frame.transient_strength);
	message += "},";
	append_key(message, Field::tempo);
	message += '{';
	append_key(message, Field::bpm);
	append_float(message, frame.bpm, 2);
	message += ',';
	append_key(message, Field::confidence);
	append_float(message, frame.bpm_confidence);
	message += ',';
	append_key(message, Field::locked);
	append_bool(message, frame.bpm_locked);
	message += "},";
	append_key(message, Field::fft32);
	message += '[';
	for (size_t band = 0; band < frame.fft32.size(); ++band) {
		if (band != 0)
			message += ',';
		append_float(message, frame.fft32[band]);
	}
	message += "]}";
	return message;
}
} // namespace ArtProtocol
