// SPDX-FileCopyrightText: 2026 Funkrusha
// SPDX-License-Identifier: GPL-3.0-or-later

#include "wave-filter.hpp"

#include "modulation/modulation-binding.hpp"
#include "modulation/tempo-sync.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <algorithm>

namespace {
constexpr char filter_id[] = "art_wave_filter";
constexpr float two_pi = 6.28318531F;

namespace Field {
constexpr char frequency[] = "frequency";
constexpr char speed[] = "speed";
constexpr char smoothing[] = "smoothing";
constexpr char strength[] = "strength";

constexpr char sync_to_bpm[] = "sync_to_bpm";
constexpr char sync_cycles_per_beat[] = "sync_cycles_per_beat";
constexpr char sync_lock_strength[] = "sync_lock_strength";

constexpr char amplitude_source[] = "amplitude_source";
constexpr char amplitude_min[] = "amplitude_min";
constexpr char amplitude_max[] = "amplitude_max";
} // namespace Field

struct WaveFilter {
	obs_source_t *source = nullptr;
	gs_effect_t *effect = nullptr;
	gs_eparam_t *phase_param = nullptr;
	gs_eparam_t *frequency_param = nullptr;
	gs_eparam_t *amplitude_param = nullptr;
	gs_eparam_t *source_size_param = nullptr;

	// Cycles of the wave across the image height.
	float frequency = 6.0F;
	// How many full cycles per second the wave scrolls through. Ignored while sync_to_bpm is on.
	float speed_hz = 1.5F;
	// Shared "Return/Spring" damping for the bound parameter below.
	float damping = 8.0F;
	float strength = 1.0F;
	// Advances by `speed_hz * two_pi * seconds` each tick (or by the tempo oscillator below, in sync mode).
	float phase = 0.0F;

	// When true, `phase` tracks the live BPM estimate (via `tempo_oscillator`) instead of `speed_hz`.
	bool sync_to_bpm = false;
	// Wave cycles per beat, e.g. 1.0 = one full wave cycle per beat, 2.0 = twice per beat.
	float sync_cycles_per_beat = 1.0F;
	// 0..1: how strongly each new beat nudges the phase back on-tempo. 0 ignores beats (pure BPM speed).
	float sync_lock_strength = 0.35F;
	ArtModulation::TempoOscillator tempo_oscillator;

	ArtModulation::BoundParameter amplitude{Field::amplitude_source, Field::amplitude_min, Field::amplitude_max};
};

const char *wave_name(void *)
{
	return obs_module_text("WaveFilter.Name");
}

void wave_update(void *data, obs_data_t *settings)
{
	auto *filter = static_cast<WaveFilter *>(data);
	filter->frequency =
		std::clamp(static_cast<float>(obs_data_get_double(settings, Field::frequency)), 0.5F, 30.0F);
	filter->speed_hz = std::clamp(static_cast<float>(obs_data_get_double(settings, Field::speed)), 0.0F, 10.0F);
	filter->damping = std::clamp(static_cast<float>(obs_data_get_double(settings, Field::smoothing)), 1.0F, 30.0F);
	filter->strength =
		std::clamp(static_cast<float>(obs_data_get_double(settings, Field::strength)), 0.0F, 100.0F) / 100.0F;

	filter->sync_to_bpm = obs_data_get_bool(settings, Field::sync_to_bpm);
	filter->sync_cycles_per_beat =
		std::clamp(static_cast<float>(obs_data_get_double(settings, Field::sync_cycles_per_beat)), 0.25F, 8.0F);
	filter->sync_lock_strength =
		std::clamp(static_cast<float>(obs_data_get_double(settings, Field::sync_lock_strength)), 0.0F, 100.0F) /
		100.0F;

	ArtModulation::read_binding(settings, filter->amplitude, 0.0F, 100.0F);
}

void *wave_create(obs_data_t *settings, obs_source_t *source)
{
	auto *filter = new WaveFilter;
	filter->source = source;

	char *path = obs_module_file("effects/wave.effect");
	char *error = nullptr;
	obs_enter_graphics();
	filter->effect = gs_effect_create_from_file(path, &error);
	obs_leave_graphics();
	if (!filter->effect) {
		obs_log(LOG_ERROR, "could not load Wave effect '%s': %s", path, error ? error : "unknown error");
		bfree(error);
		bfree(path);
		delete filter;
		return nullptr;
	}
	bfree(error);
	bfree(path);

	filter->phase_param = gs_effect_get_param_by_name(filter->effect, "phase");
	filter->frequency_param = gs_effect_get_param_by_name(filter->effect, "frequency");
	filter->amplitude_param = gs_effect_get_param_by_name(filter->effect, "amplitude_px");
	filter->source_size_param = gs_effect_get_param_by_name(filter->effect, "source_size");
	wave_update(filter, settings);
	return filter;
}

void wave_destroy(void *data)
{
	auto *filter = static_cast<WaveFilter *>(data);
	obs_enter_graphics();
	gs_effect_destroy(filter->effect);
	obs_leave_graphics();
	delete filter;
}

void wave_defaults(obs_data_t *settings)
{
	obs_data_set_default_double(settings, Field::frequency, 6.0);
	obs_data_set_default_double(settings, Field::speed, 1.5);
	obs_data_set_default_double(settings, Field::smoothing, 8.0);
	obs_data_set_default_double(settings, Field::strength, 100.0);

	obs_data_set_default_bool(settings, Field::sync_to_bpm, false);
	obs_data_set_default_double(settings, Field::sync_cycles_per_beat, 1.0);
	obs_data_set_default_double(settings, Field::sync_lock_strength, 35.0);

	obs_data_set_default_int(settings, Field::amplitude_source, static_cast<int>(ArtModulation::Source::Mids));
	obs_data_set_default_double(settings, Field::amplitude_min, 0.0);
	obs_data_set_default_double(settings, Field::amplitude_max, 20.0);
}

// Toggles "Speed (Hz)" vs. the two BPM-sync sliders depending on the Sync to BPM checkbox.
bool wave_sync_modified(obs_properties_t *properties, obs_property_t *, obs_data_t *settings)
{
	const bool sync_to_bpm = obs_data_get_bool(settings, Field::sync_to_bpm);
	obs_property_set_visible(obs_properties_get(properties, Field::speed), !sync_to_bpm);
	obs_property_set_visible(obs_properties_get(properties, Field::sync_cycles_per_beat), sync_to_bpm);
	obs_property_set_visible(obs_properties_get(properties, Field::sync_lock_strength), sync_to_bpm);
	return true;
}

obs_properties_t *wave_properties(void *data)
{
	// See the matching comment in custom-shader-filter.cpp: get_properties() has to read the filter's
	// actual current mode for the initial visibility, since OBS won't re-run modified_callback for us.
	const auto *filter = static_cast<const WaveFilter *>(data);
	const bool sync_to_bpm = filter && filter->sync_to_bpm;

	obs_properties_t *properties = obs_properties_create();
	obs_properties_add_float_slider(properties, Field::frequency, obs_module_text("WaveFilter.Frequency"), 0.5,
					30.0, 0.5);
	obs_property_t *speed_prop = obs_properties_add_float_slider(
		properties, Field::speed, obs_module_text("WaveFilter.Speed"), 0.0, 10.0, 0.1);
	obs_properties_add_float_slider(properties, Field::smoothing, obs_module_text("WaveFilter.Smoothing"), 1.0,
					30.0, 0.5);
	obs_properties_add_float_slider(properties, Field::strength, obs_module_text("WaveFilter.Strength"), 0.0, 100.0,
					1.0);

	obs_property_t *sync =
		obs_properties_add_bool(properties, Field::sync_to_bpm, obs_module_text("WaveFilter.SyncToBpm"));
	obs_property_t *cycles_prop = obs_properties_add_float_slider(properties, Field::sync_cycles_per_beat,
								      obs_module_text("WaveFilter.SyncCyclesPerBeat"),
								      0.25, 8.0, 0.25);
	obs_property_t *lock_prop = obs_properties_add_float_slider(
		properties, Field::sync_lock_strength, obs_module_text("WaveFilter.SyncLockStrength"), 0.0, 100.0, 1.0);
	obs_property_set_visible(speed_prop, !sync_to_bpm);
	obs_property_set_visible(cycles_prop, sync_to_bpm);
	obs_property_set_visible(lock_prop, sync_to_bpm);
	obs_property_set_modified_callback(sync, wave_sync_modified);

	obs_properties_add_group(
		properties, "amplitude_group", obs_module_text("WaveFilter.AmplitudeGroup"), OBS_GROUP_NORMAL,
		ArtModulation::make_binding_group(Field::amplitude_source, Field::amplitude_min, Field::amplitude_max,
						  obs_module_text("WaveFilter.AmplitudeMin"),
						  obs_module_text("WaveFilter.AmplitudeMax"), 0.0, 100.0, 1.0));
	return properties;
}

void wave_tick(void *data, float seconds)
{
	auto *filter = static_cast<WaveFilter *>(data);
	const ArtModulation::Snapshot audio = ArtModulation::snapshot();
	filter->amplitude.value =
		filter->amplitude.channel.update(filter->amplitude.binding, audio, filter->damping, seconds);
	if (filter->sync_to_bpm) {
		// tempo_oscillator's phase is in cycles (1.0 = one full cycle), same unit convention as this
		// filter's own manual accumulator below, just tempo-locked instead of a fixed speed.
		const float phase_cycles = filter->tempo_oscillator.advance(audio, filter->sync_cycles_per_beat,
									    filter->sync_lock_strength, seconds);
		filter->phase = phase_cycles * two_pi;
	} else {
		filter->phase += filter->speed_hz * two_pi * seconds;
	}
}

void wave_render(void *data, gs_effect_t *)
{
	auto *filter = static_cast<WaveFilter *>(data);
	obs_source_t *target = obs_filter_get_target(filter->source);
	const uint32_t width = target ? obs_source_get_base_width(target) : 0;
	const uint32_t height = target ? obs_source_get_base_height(target) : 0;
	if (width == 0 || height == 0 ||
	    !obs_source_process_filter_begin(filter->source, GS_RGBA, OBS_NO_DIRECT_RENDERING)) {
		obs_source_skip_video_filter(filter->source);
		return;
	}

	const float amplitude_px = filter->amplitude.resolved(filter->strength);
	const vec2 source_size = {static_cast<float>(width), static_cast<float>(height)};
	gs_effect_set_float(filter->phase_param, filter->phase);
	gs_effect_set_float(filter->frequency_param, filter->frequency);
	gs_effect_set_float(filter->amplitude_param, amplitude_px);
	gs_effect_set_vec2(filter->source_size_param, &source_size);
	obs_source_process_filter_end(filter->source, filter->effect, width, height);
}
} // namespace

void register_wave_filter()
{
	obs_source_info info = {};
	info.id = filter_id;
	info.type = OBS_SOURCE_TYPE_FILTER;
	info.output_flags = OBS_SOURCE_VIDEO;
	info.get_name = wave_name;
	info.create = wave_create;
	info.destroy = wave_destroy;
	info.update = wave_update;
	info.get_defaults = wave_defaults;
	info.get_properties = wave_properties;
	info.video_tick = wave_tick;
	info.video_render = wave_render;
	obs_register_source(&info);
}
