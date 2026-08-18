// SPDX-FileCopyrightText: 2026 Funkrusha
// SPDX-License-Identifier: GPL-3.0-or-later

#include "audio-capture.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>

#include <media-io/audio-io.h>
#include <plugin-support.h>

namespace {
constexpr uint32_t first_audio_output_channel = 1;
constexpr uint32_t last_audio_output_channel = 6;
constexpr float log_interval_seconds = 1.0F;
constexpr float bass_cutoff_hz = 250.0F;
constexpr float high_cutoff_hz = 4000.0F;
constexpr float pi = 3.14159265358979323846F;
constexpr float fft_min_hz = 30.0F;
constexpr float fft_max_hz = 18000.0F;
constexpr float fft_attack_smoothing = 0.2F;
constexpr float fft_release_smoothing = 0.78F;
constexpr float adaptive_rate = 0.025F;
constexpr float default_beat_threshold_deviations = 1.5F;
constexpr float default_transient_threshold_deviations = 1.3F;
constexpr float beat_flux_floor = 0.0006F;
constexpr float transient_flux_floor = 0.0008F;
constexpr float beat_minimum_level = 0.015F;
constexpr float transient_minimum_level = 0.02F;
constexpr uint64_t nanoseconds_per_millisecond = 1000000ULL;

float low_pass_alpha(float cutoff_hz, uint32_t sample_rate)
{
	return 1.0F - std::exp(-2.0F * pi * cutoff_hz / static_cast<float>(sample_rate));
}

void fft_in_place(std::array<std::complex<float>, 16384> &values, size_t count)
{
	for (size_t index = 1, reversed = 0; index < count; ++index) {
		size_t bit = count >> 1;
		for (; reversed & bit; bit >>= 1)
			reversed ^= bit;
		reversed ^= bit;
		if (index < reversed)
			std::swap(values[index], values[reversed]);
	}

	for (size_t length = 2; length <= count; length <<= 1) {
		const float angle = -2.0F * pi / static_cast<float>(length);
		const std::complex<float> step(std::cos(angle), std::sin(angle));
		for (size_t offset = 0; offset < count; offset += length) {
			std::complex<float> factor(1.0F, 0.0F);
			for (size_t pair = 0; pair < length / 2; ++pair) {
				const auto even = values[offset + pair];
				const auto odd = values[offset + pair + length / 2] * factor;
				values[offset + pair] = even + odd;
				values[offset + pair + length / 2] = even - odd;
				factor *= step;
			}
		}
	}
}

} // namespace

void AudioCapture::configure_detection(uint32_t beat_sensitivity, uint32_t beat_cooldown_ms,
				       uint32_t transient_sensitivity, uint32_t transient_cooldown_ms)
{
	const float beat_factor = 100.0F / static_cast<float>(std::clamp(beat_sensitivity, 25U, 200U));
	const float transient_factor = 100.0F / static_cast<float>(std::clamp(transient_sensitivity, 25U, 200U));
	beat_threshold_deviations_.store(default_beat_threshold_deviations * beat_factor, std::memory_order_relaxed);
	beat_cooldown_ns_.store(static_cast<uint64_t>(std::clamp(beat_cooldown_ms, 80U, 1000U)) *
					nanoseconds_per_millisecond,
				std::memory_order_relaxed);
	transient_threshold_deviations_.store(default_transient_threshold_deviations * transient_factor,
					      std::memory_order_relaxed);
	transient_cooldown_ns_.store(static_cast<uint64_t>(std::clamp(transient_cooldown_ms, 20U, 500U)) *
					     nanoseconds_per_millisecond,
				     std::memory_order_relaxed);
}

void AudioCapture::set_debug_logging(bool enabled)
{
	debug_logging_.store(enabled, std::memory_order_relaxed);
}

void AudioCapture::set_fft_size(uint32_t size)
{
	if (size != 2048 && size != 4096 && size != 8192 && size != 16384)
		size = 8192;
	fft_size_.store(size, std::memory_order_relaxed);
	last_fft_write_index_ = 0;
	for (auto &band : fft_bands_)
		band.store(0.0F, std::memory_order_relaxed);
}

bool AudioCapture::attach_first_output_source()
{
	detach();

	for (uint32_t channel = first_audio_output_channel; channel <= last_audio_output_channel; ++channel) {
		obs_source_t *candidate = obs_get_output_source(channel);
		if (!candidate)
			continue;

		if ((obs_source_get_output_flags(candidate) & OBS_SOURCE_AUDIO) == 0) {
			obs_source_release(candidate);
			continue;
		}

		char description[64];
		std::snprintf(description, sizeof(description), "automatic output channel %u", channel);
		return attach_source(candidate, description);
	}

	obs_log(LOG_WARNING, "no configured global OBS audio source found on output channels 1-6");
	return false;
}

bool AudioCapture::attach_source_by_name(const char *name)
{
	detach();
	if (!name || !*name)
		return attach_first_output_source();

	obs_source_t *candidate = obs_get_source_by_name(name);
	if (!candidate) {
		obs_log(LOG_WARNING, "selected audio source not found: '%s'", name);
		return false;
	}
	if ((obs_source_get_output_flags(candidate) & OBS_SOURCE_AUDIO) == 0) {
		obs_log(LOG_WARNING, "selected source has no audio output: '%s'", name);
		obs_source_release(candidate);
		return false;
	}
	return attach_source(candidate, "manual selection");
}

bool AudioCapture::attach_source(obs_source_t *source, const char *selection_description)
{
	source_ = source;
	audio_t *audio_output = obs_get_audio();
	sample_rate_ = audio_output ? audio_output_get_sample_rate(audio_output) : 48000;
	bass_filter_alpha_ = low_pass_alpha(bass_cutoff_hz, sample_rate_);
	high_cut_filter_alpha_ = low_pass_alpha(high_cutoff_hz, sample_rate_);
	bass_filter_state_.fill(0.0F);
	high_cut_filter_state_.fill(0.0F);
	mono_write_index_.store(0, std::memory_order_relaxed);
	last_fft_write_index_ = 0;
	for (auto &band : fft_bands_)
		band.store(0.0F, std::memory_order_relaxed);
	previous_bass_ = 0.0F;
	bass_flux_mean_ = 0.0F;
	bass_flux_variance_ = 0.0F;
	previous_rms_ = 0.0F;
	transient_flux_mean_ = 0.0F;
	transient_flux_variance_ = 0.0F;
	last_beat_timestamp_ = 0;
	last_transient_timestamp_ = 0;
	detection_initialized_ = false;
	obs_source_add_audio_capture_callback(source_, audio_received, this);
	if (debug_logging_.load(std::memory_order_relaxed))
		obs_log(LOG_INFO, "capturing '%s' at %u Hz (%s)", obs_source_get_name(source_), sample_rate_,
			selection_description);
	return true;
}

void AudioCapture::detach()
{
	if (!source_)
		return;

	obs_source_remove_audio_capture_callback(source_, audio_received, this);
	if (debug_logging_.load(std::memory_order_relaxed))
		obs_log(LOG_INFO, "stopped capturing: '%s'", obs_source_get_name(source_));
	obs_source_release(source_);
	source_ = nullptr;
	received_audio_.store(false, std::memory_order_relaxed);
	bass_filter_state_.fill(0.0F);
	high_cut_filter_state_.fill(0.0F);
	mono_write_index_.store(0, std::memory_order_relaxed);
	last_fft_write_index_ = 0;
	for (auto &band : fft_bands_)
		band.store(0.0F, std::memory_order_relaxed);
	previous_bass_ = 0.0F;
	bass_flux_mean_ = 0.0F;
	bass_flux_variance_ = 0.0F;
	previous_rms_ = 0.0F;
	transient_flux_mean_ = 0.0F;
	transient_flux_variance_ = 0.0F;
	last_beat_timestamp_ = 0;
	last_transient_timestamp_ = 0;
	detection_initialized_ = false;
}

void AudioCapture::audio_received(void *context, obs_source_t *, const struct audio_data *audio, bool muted)
{
	auto *capture = static_cast<AudioCapture *>(context);
	if (!audio || audio->frames == 0)
		return;

	uint32_t channel_count = 0;
	for (uint32_t channel = 0; channel < MAX_AUDIO_CHANNELS; ++channel) {
		if (audio->data[channel])
			++channel_count;
	}

	if (channel_count == 0)
		return;

	if (muted) {
		capture->rms_.store(0.0F, std::memory_order_relaxed);
		capture->peak_.store(0.0F, std::memory_order_relaxed);
		capture->bass_.store(0.0F, std::memory_order_relaxed);
		capture->mid_.store(0.0F, std::memory_order_relaxed);
		capture->high_.store(0.0F, std::memory_order_relaxed);
		capture->previous_bass_ = 0.0F;
		capture->previous_rms_ = 0.0F;
		capture->detection_initialized_ = false;
	} else {
		double square_sum = 0.0;
		double bass_square_sum = 0.0;
		double mid_square_sum = 0.0;
		double high_square_sum = 0.0;
		float peak = 0.0F;
		uint64_t sample_count = 0;

		for (uint32_t channel = 0; channel < MAX_AUDIO_CHANNELS; ++channel) {
			if (!audio->data[channel])
				continue;

			const auto *samples = reinterpret_cast<const float *>(audio->data[channel]);
			for (uint32_t frame = 0; frame < audio->frames; ++frame) {
				const float sample = samples[frame];
				float &bass_state = capture->bass_filter_state_[channel];
				float &high_cut_state = capture->high_cut_filter_state_[channel];
				bass_state += capture->bass_filter_alpha_ * (sample - bass_state);
				high_cut_state += capture->high_cut_filter_alpha_ * (sample - high_cut_state);
				const float bass_sample = bass_state;
				const float mid_sample = high_cut_state - bass_state;
				const float high_sample = sample - high_cut_state;

				square_sum += static_cast<double>(sample) * sample;
				bass_square_sum += static_cast<double>(bass_sample) * bass_sample;
				mid_square_sum += static_cast<double>(mid_sample) * mid_sample;
				high_square_sum += static_cast<double>(high_sample) * high_sample;
				peak = std::max(peak, std::abs(sample));
			}
			sample_count += audio->frames;
		}

		const float rms = sample_count > 0 ? static_cast<float>(std::sqrt(square_sum / sample_count)) : 0.0F;
		const float bass = sample_count > 0 ? static_cast<float>(std::sqrt(bass_square_sum / sample_count))
						    : 0.0F;
		const float mid = sample_count > 0 ? static_cast<float>(std::sqrt(mid_square_sum / sample_count))
						   : 0.0F;
		const float high = sample_count > 0 ? static_cast<float>(std::sqrt(high_square_sum / sample_count))
						    : 0.0F;
		capture->rms_.store(rms, std::memory_order_relaxed);
		capture->peak_.store(peak, std::memory_order_relaxed);
		capture->bass_.store(bass, std::memory_order_relaxed);
		capture->mid_.store(mid, std::memory_order_relaxed);
		capture->high_.store(high, std::memory_order_relaxed);
		capture->detect_events(rms, bass, audio->timestamp);

		uint64_t write_index = capture->mono_write_index_.load(std::memory_order_relaxed);
		for (uint32_t frame = 0; frame < audio->frames; ++frame) {
			float mono_sample = 0.0F;
			for (uint32_t channel = 0; channel < MAX_AUDIO_CHANNELS; ++channel) {
				if (!audio->data[channel])
					continue;
				const auto *samples = reinterpret_cast<const float *>(audio->data[channel]);
				mono_sample += samples[frame];
			}
			mono_sample /= static_cast<float>(channel_count);
			capture->mono_ring_buffer_[write_index % ring_buffer_size] = mono_sample;
			++write_index;
		}
		capture->mono_write_index_.store(write_index, std::memory_order_release);
	}

	capture->channels_.store(channel_count, std::memory_order_relaxed);
	capture->frames_.store(audio->frames, std::memory_order_relaxed);
	capture->received_audio_.store(true, std::memory_order_release);
}

void AudioCapture::detect_events(float rms, float bass, uint64_t timestamp)
{
	if (!detection_initialized_) {
		previous_bass_ = bass;
		previous_rms_ = rms;
		detection_initialized_ = true;
		return;
	}

	const float bass_flux = std::max(0.0F, bass - previous_bass_);
	const float bass_deviation = std::sqrt(std::max(0.0F, bass_flux_variance_));
	const float beat_threshold = bass_flux_mean_ +
				     beat_threshold_deviations_.load(std::memory_order_relaxed) * bass_deviation +
				     beat_flux_floor;
	if (bass >= beat_minimum_level && bass_flux > beat_threshold &&
	    (last_beat_timestamp_ == 0 ||
	     timestamp - last_beat_timestamp_ >= beat_cooldown_ns_.load(std::memory_order_relaxed))) {
		const float strength = std::clamp(bass_flux / (beat_threshold * 2.0F), 0.0F, 1.0F);
		beat_strength_.store(strength, std::memory_order_relaxed);
		beat_timestamp_.store(timestamp, std::memory_order_relaxed);
		beat_counter_.fetch_add(1, std::memory_order_release);
		last_beat_timestamp_ = timestamp;
	}
	const float learned_bass_flux = std::min(bass_flux, beat_threshold * 2.0F);
	const float bass_difference = learned_bass_flux - bass_flux_mean_;
	bass_flux_mean_ += adaptive_rate * bass_difference;
	bass_flux_variance_ += adaptive_rate * (bass_difference * bass_difference - bass_flux_variance_);
	previous_bass_ = bass;

	const float transient_flux = std::max(0.0F, rms - previous_rms_);
	const float transient_deviation = std::sqrt(std::max(0.0F, transient_flux_variance_));
	const float transient_threshold =
		transient_flux_mean_ +
		transient_threshold_deviations_.load(std::memory_order_relaxed) * transient_deviation +
		transient_flux_floor;
	if (rms >= transient_minimum_level && transient_flux > transient_threshold &&
	    (last_transient_timestamp_ == 0 ||
	     timestamp - last_transient_timestamp_ >= transient_cooldown_ns_.load(std::memory_order_relaxed))) {
		const float strength = std::clamp(transient_flux / (transient_threshold * 2.0F), 0.0F, 1.0F);
		transient_strength_.store(strength, std::memory_order_relaxed);
		transient_counter_.fetch_add(1, std::memory_order_release);
		last_transient_timestamp_ = timestamp;
	}
	const float learned_transient_flux = std::min(transient_flux, transient_threshold * 2.0F);
	const float transient_difference = learned_transient_flux - transient_flux_mean_;
	transient_flux_mean_ += adaptive_rate * transient_difference;
	transient_flux_variance_ +=
		adaptive_rate * (transient_difference * transient_difference - transient_flux_variance_);
	previous_rms_ = rms;
}

void AudioCapture::tick(float seconds)
{
	analyze_fft();
	log_timer_ += seconds;
	if (log_timer_ < log_interval_seconds)
		return;

	log_timer_ = 0.0F;
	if (!debug_logging_.load(std::memory_order_relaxed) || !source_ ||
	    !received_audio_.load(std::memory_order_acquire))
		return;

	obs_log(LOG_INFO, "audio '%s': channels=%u frames=%u rms=%.5f peak=%.5f bass=%.5f mid=%.5f high=%.5f",
		obs_source_get_name(source_), channels_.load(std::memory_order_relaxed),
		frames_.load(std::memory_order_relaxed), rms_.load(std::memory_order_relaxed),
		peak_.load(std::memory_order_relaxed), bass_.load(std::memory_order_relaxed),
		mid_.load(std::memory_order_relaxed), high_.load(std::memory_order_relaxed));
}

void AudioCapture::analyze_fft()
{
	const size_t fft_size = fft_size_.load(std::memory_order_relaxed);
	const uint64_t write_index = mono_write_index_.load(std::memory_order_acquire);
	if (write_index < fft_size || write_index - last_fft_write_index_ < fft_size / 4 || sample_rate_ == 0)
		return;
	last_fft_write_index_ = write_index;

	std::array<std::complex<float>, maximum_fft_size> spectrum{};
	const uint64_t first_sample = write_index - fft_size;
	for (size_t index = 0; index < fft_size; ++index) {
		const float window =
			0.5F - 0.5F * std::cos(pi * static_cast<float>(index) / static_cast<float>(fft_size - 1));
		spectrum[index] = mono_ring_buffer_[(first_sample + index) % ring_buffer_size] * window;
	}
	fft_in_place(spectrum, fft_size);

	std::array<double, AnalysisSnapshot::fft_band_count> energy{};
	std::array<uint32_t, AnalysisSnapshot::fft_band_count> bins{};
	const float frequency_ratio = fft_max_hz / fft_min_hz;
	for (size_t bin = 1; bin < fft_size / 2; ++bin) {
		const float frequency =
			static_cast<float>(bin) * static_cast<float>(sample_rate_) / static_cast<float>(fft_size);
		if (frequency < fft_min_hz || frequency > fft_max_hz)
			continue;

		const float position = std::log(frequency / fft_min_hz) / std::log(frequency_ratio);
		const size_t band = std::min(static_cast<size_t>(position * AnalysisSnapshot::fft_band_count),
					     AnalysisSnapshot::fft_band_count - 1);
		const float magnitude = 2.0F * std::abs(spectrum[bin]) / static_cast<float>(fft_size);
		energy[band] += static_cast<double>(magnitude) * magnitude;
		++bins[band];
	}

	for (size_t band = 0; band < AnalysisSnapshot::fft_band_count; ++band) {
		const float value = bins[band] > 0 ? static_cast<float>(std::sqrt(energy[band] / bins[band])) : 0.0F;
		const float previous = fft_bands_[band].load(std::memory_order_relaxed);
		const float smoothing = value >= previous ? fft_attack_smoothing : fft_release_smoothing;
		fft_bands_[band].store(previous * smoothing + value * (1.0F - smoothing), std::memory_order_relaxed);
	}
}

AnalysisSnapshot AudioCapture::snapshot() const
{
	AnalysisSnapshot result{
		rms_.load(std::memory_order_relaxed),
		peak_.load(std::memory_order_relaxed),
		bass_.load(std::memory_order_relaxed),
		mid_.load(std::memory_order_relaxed),
		high_.load(std::memory_order_relaxed),
		channels_.load(std::memory_order_relaxed),
		frames_.load(std::memory_order_relaxed),
		received_audio_.load(std::memory_order_acquire),
		{},
		beat_counter_.load(std::memory_order_acquire),
		beat_strength_.load(std::memory_order_relaxed),
		beat_timestamp_.load(std::memory_order_relaxed),
		transient_counter_.load(std::memory_order_acquire),
		transient_strength_.load(std::memory_order_relaxed),
	};
	for (size_t band = 0; band < result.fft_bands.size(); ++band)
		result.fft_bands[band] = fft_bands_[band].load(std::memory_order_relaxed);
	return result;
}
