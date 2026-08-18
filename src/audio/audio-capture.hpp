// SPDX-FileCopyrightText: 2026 Funkrusha
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <atomic>
#include <array>

#include <obs.h>

struct AnalysisSnapshot {
	static constexpr size_t fft_band_count = 32;

	float rms;
	float peak;
	float bass;
	float mid;
	float high;
	uint32_t channels;
	uint32_t frames;
	bool active;
	std::array<float, fft_band_count> fft_bands;
	uint64_t beat_counter;
	float beat_strength;
	uint64_t beat_timestamp;
	uint64_t transient_counter;
	float transient_strength;
};

class AudioCapture {
public:
	bool attach_first_output_source();
	bool attach_source_by_name(const char *name);
	void detach();
	void tick(float seconds);
	void configure_detection(uint32_t beat_sensitivity, uint32_t beat_cooldown_ms, uint32_t transient_sensitivity,
				 uint32_t transient_cooldown_ms);
	void set_debug_logging(bool enabled);
	void set_fft_size(uint32_t size);
	AnalysisSnapshot snapshot() const;

private:
	static void audio_received(void *context, obs_source_t *source, const struct audio_data *audio, bool muted);
	void analyze_fft();
	void detect_events(float rms, float bass, uint64_t timestamp);
	bool attach_source(obs_source_t *source, const char *selection_description);

	static constexpr size_t maximum_fft_size = 16384;
	static constexpr size_t ring_buffer_size = 32768;

	obs_source_t *source_ = nullptr;
	std::atomic<float> rms_{0.0F};
	std::atomic<float> peak_{0.0F};
	std::atomic<float> bass_{0.0F};
	std::atomic<float> mid_{0.0F};
	std::atomic<float> high_{0.0F};
	std::atomic<uint32_t> channels_{0};
	std::atomic<uint32_t> frames_{0};
	std::atomic<bool> received_audio_{false};
	std::atomic<bool> debug_logging_{false};
	std::atomic<uint64_t> beat_counter_{0};
	std::atomic<float> beat_strength_{0.0F};
	std::atomic<uint64_t> beat_timestamp_{0};
	std::atomic<uint64_t> transient_counter_{0};
	std::atomic<float> transient_strength_{0.0F};
	std::atomic<float> beat_threshold_deviations_{1.5F};
	std::atomic<uint64_t> beat_cooldown_ns_{240000000ULL};
	std::atomic<float> transient_threshold_deviations_{1.3F};
	std::atomic<uint64_t> transient_cooldown_ns_{70000000ULL};
	float previous_bass_ = 0.0F;
	float bass_flux_mean_ = 0.0F;
	float bass_flux_variance_ = 0.0F;
	float previous_rms_ = 0.0F;
	float transient_flux_mean_ = 0.0F;
	float transient_flux_variance_ = 0.0F;
	uint64_t last_beat_timestamp_ = 0;
	uint64_t last_transient_timestamp_ = 0;
	bool detection_initialized_ = false;
	std::array<std::atomic<float>, AnalysisSnapshot::fft_band_count> fft_bands_{};
	std::atomic<uint32_t> fft_size_{8192};
	std::array<float, ring_buffer_size> mono_ring_buffer_{};
	std::atomic<uint64_t> mono_write_index_{0};
	uint64_t last_fft_write_index_ = 0;
	std::array<float, 8> bass_filter_state_{};
	std::array<float, 8> high_cut_filter_state_{};
	float bass_filter_alpha_ = 0.0F;
	float high_cut_filter_alpha_ = 0.0F;
	uint32_t sample_rate_ = 0;
	float log_timer_ = 0.0F;
};
