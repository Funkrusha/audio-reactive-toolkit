// SPDX-FileCopyrightText: 2026 Funkrusha
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shake-filter.hpp"

#include "modulation/modulation-binding.hpp"
#include "modulation/tempo-sync.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace {
constexpr char filter_id[] = "art_shake_filter";

namespace Field {
constexpr char speed[] = "speed";
constexpr char smoothing[] = "smoothing";
constexpr char strength[] = "strength";

constexpr char sync_to_bpm[] = "sync_to_bpm";
constexpr char sync_cycles_per_beat[] = "sync_cycles_per_beat";
constexpr char sync_lock_strength[] = "sync_lock_strength";

constexpr char shake_source[] = "shake_source";
constexpr char shake_min[] = "shake_min";
constexpr char shake_max[] = "shake_max";
} // namespace Field

// Cheap integer hash, mapped to [-1, 1]. Used to build a smoothly interpolated random walk.
float hash_signed(uint32_t seed)
{
	seed = seed * 747796405u + 2891336453u;
	uint32_t result = ((seed >> ((seed >> 28u) + 4u)) ^ seed) * 277803737u;
	result = (result >> 22u) ^ result;
	return (static_cast<float>(result) / 4294967295.0F) * 2.0F - 1.0F;
}

struct ShakeFilter {
	obs_source_t *source = nullptr;
	gs_effect_t *effect = nullptr;
	gs_eparam_t *offset_param = nullptr;
	gs_eparam_t *source_size_param = nullptr;

	// How many times per second the shake direction changes. Ignored while sync_to_bpm is on.
	float speed_hz = 15.0F;
	// Shared "Return/Spring" damping for the bound parameter below.
	float damping = 8.0F;
	float strength = 1.0F;
	// Advances by `speed_hz * seconds` each tick (or by the tempo oscillator, in sync mode); drives the
	// random-walk direction.
	float phase = 0.0F;

	// When true, `phase` tracks the live BPM estimate (via `tempo_oscillator`) instead of `speed_hz`.
	bool sync_to_bpm = false;
	// Random-walk steps per beat, e.g. 1.0 = one direction change per beat, 4.0 = four per beat.
	float sync_cycles_per_beat = 1.0F;
	// 0..1: how strongly each new beat nudges the phase back on-tempo. 0 ignores beats (pure BPM speed).
	float sync_lock_strength = 0.35F;
	ArtModulation::TempoOscillator tempo_oscillator;

	ArtModulation::BoundParameter shake{Field::shake_source, Field::shake_min, Field::shake_max};
};

const char *shake_name(void *)
{
	return obs_module_text("ShakeFilter.Name");
}

void shake_update(void *data, obs_data_t *settings)
{
	auto *filter = static_cast<ShakeFilter *>(data);
	filter->speed_hz = std::clamp(static_cast<float>(obs_data_get_double(settings, Field::speed)), 1.0F, 40.0F);
	filter->damping = std::clamp(static_cast<float>(obs_data_get_double(settings, Field::smoothing)), 1.0F, 30.0F);
	filter->strength =
		std::clamp(static_cast<float>(obs_data_get_double(settings, Field::strength)), 0.0F, 100.0F) / 100.0F;

	filter->sync_to_bpm = obs_data_get_bool(settings, Field::sync_to_bpm);
	filter->sync_cycles_per_beat =
		std::clamp(static_cast<float>(obs_data_get_double(settings, Field::sync_cycles_per_beat)), 0.25F, 8.0F);
	filter->sync_lock_strength =
		std::clamp(static_cast<float>(obs_data_get_double(settings, Field::sync_lock_strength)), 0.0F, 100.0F) /
		100.0F;

	ArtModulation::read_binding(settings, filter->shake, 0.0F, 100.0F);
}

void *shake_create(obs_data_t *settings, obs_source_t *source)
{
	auto *filter = new ShakeFilter;
	filter->source = source;

	char *path = obs_module_file("effects/shake.effect");
	char *error = nullptr;
	obs_enter_graphics();
	filter->effect = gs_effect_create_from_file(path, &error);
	obs_leave_graphics();
	if (!filter->effect) {
		obs_log(LOG_ERROR, "could not load Shake effect '%s': %s", path, error ? error : "unknown error");
		bfree(error);
		bfree(path);
		delete filter;
		return nullptr;
	}
	bfree(error);
	bfree(path);

	filter->offset_param = gs_effect_get_param_by_name(filter->effect, "offset_px");
	filter->source_size_param = gs_effect_get_param_by_name(filter->effect, "source_size");
	shake_update(filter, settings);
	return filter;
}

void shake_destroy(void *data)
{
	auto *filter = static_cast<ShakeFilter *>(data);
	obs_enter_graphics();
	gs_effect_destroy(filter->effect);
	obs_leave_graphics();
	delete filter;
}

void shake_defaults(obs_data_t *settings)
{
	obs_data_set_default_double(settings, Field::speed, 15.0);
	obs_data_set_default_double(settings, Field::smoothing, 8.0);
	obs_data_set_default_double(settings, Field::strength, 100.0);

	obs_data_set_default_bool(settings, Field::sync_to_bpm, false);
	obs_data_set_default_double(settings, Field::sync_cycles_per_beat, 1.0);
	obs_data_set_default_double(settings, Field::sync_lock_strength, 35.0);

	obs_data_set_default_int(settings, Field::shake_source, static_cast<int>(ArtModulation::Source::Transient));
	obs_data_set_default_double(settings, Field::shake_min, 0.0);
	obs_data_set_default_double(settings, Field::shake_max, 20.0);
}

// Toggles "Speed (Hz)" vs. the two BPM-sync sliders depending on the Sync to BPM checkbox.
bool shake_sync_modified(obs_properties_t *properties, obs_property_t *, obs_data_t *settings)
{
	const bool sync_to_bpm = obs_data_get_bool(settings, Field::sync_to_bpm);
	obs_property_set_visible(obs_properties_get(properties, Field::speed), !sync_to_bpm);
	obs_property_set_visible(obs_properties_get(properties, Field::sync_cycles_per_beat), sync_to_bpm);
	obs_property_set_visible(obs_properties_get(properties, Field::sync_lock_strength), sync_to_bpm);
	return true;
}

obs_properties_t *shake_properties(void *data)
{
	// See the matching comment in custom-shader-filter.cpp: get_properties() has to read the filter's
	// actual current mode for the initial visibility, since OBS won't re-run modified_callback for us.
	const auto *filter = static_cast<const ShakeFilter *>(data);
	const bool sync_to_bpm = filter && filter->sync_to_bpm;

	obs_properties_t *properties = obs_properties_create();
	obs_property_t *speed_prop = obs_properties_add_float_slider(
		properties, Field::speed, obs_module_text("ShakeFilter.Speed"), 1.0, 40.0, 1.0);
	obs_properties_add_float_slider(properties, Field::smoothing, obs_module_text("ShakeFilter.Smoothing"), 1.0,
					30.0, 0.5);
	obs_properties_add_float_slider(properties, Field::strength, obs_module_text("ShakeFilter.Strength"), 0.0,
					100.0, 1.0);

	obs_property_t *sync =
		obs_properties_add_bool(properties, Field::sync_to_bpm, obs_module_text("ShakeFilter.SyncToBpm"));
	obs_property_t *cycles_prop = obs_properties_add_float_slider(properties, Field::sync_cycles_per_beat,
								      obs_module_text("ShakeFilter.SyncStepsPerBeat"),
								      0.25, 8.0, 0.25);
	obs_property_t *lock_prop = obs_properties_add_float_slider(properties, Field::sync_lock_strength,
								    obs_module_text("ShakeFilter.SyncLockStrength"),
								    0.0, 100.0, 1.0);
	obs_property_set_visible(speed_prop, !sync_to_bpm);
	obs_property_set_visible(cycles_prop, sync_to_bpm);
	obs_property_set_visible(lock_prop, sync_to_bpm);
	obs_property_set_modified_callback(sync, shake_sync_modified);

	obs_properties_add_group(
		properties, "shake_group", obs_module_text("ShakeFilter.ShakeGroup"), OBS_GROUP_NORMAL,
		ArtModulation::make_binding_group(Field::shake_source, Field::shake_min, Field::shake_max,
						  obs_module_text("ShakeFilter.ShakeMin"),
						  obs_module_text("ShakeFilter.ShakeMax"), 0.0, 100.0, 1.0));
	return properties;
}

void shake_tick(void *data, float seconds)
{
	auto *filter = static_cast<ShakeFilter *>(data);
	const ArtModulation::Snapshot audio = ArtModulation::snapshot();
	filter->shake.value = filter->shake.channel.update(filter->shake.binding, audio, filter->damping, seconds);
	if (filter->sync_to_bpm) {
		filter->phase = filter->tempo_oscillator.advance(audio, filter->sync_cycles_per_beat,
								 filter->sync_lock_strength, seconds);
	} else {
		filter->phase += filter->speed_hz * seconds;
	}
}

void shake_render(void *data, gs_effect_t *)
{
	auto *filter = static_cast<ShakeFilter *>(data);
	obs_source_t *target = obs_filter_get_target(filter->source);
	const uint32_t width = target ? obs_source_get_base_width(target) : 0;
	const uint32_t height = target ? obs_source_get_base_height(target) : 0;
	if (width == 0 || height == 0 ||
	    !obs_source_process_filter_begin(filter->source, GS_RGBA, OBS_NO_DIRECT_RENDERING)) {
		obs_source_skip_video_filter(filter->source);
		return;
	}

	// Smoothly interpolated random walk: hash two consecutive integer steps, blend by the
	// fractional phase so the shake direction changes continuously instead of jumping.
	const auto step = static_cast<uint32_t>(std::floor(filter->phase));
	const float frac = filter->phase - static_cast<float>(step);
	const float blend = frac * frac * (3.0F - 2.0F * frac);
	const float dir_x_a = hash_signed(step * 2u);
	const float dir_y_a = hash_signed(step * 2u + 1u);
	float dir_x = dir_x_a + (hash_signed((step + 1u) * 2u) - dir_x_a) * blend;
	float dir_y = dir_y_a + (hash_signed((step + 1u) * 2u + 1u) - dir_y_a) * blend;
	const float length = std::sqrt(dir_x * dir_x + dir_y * dir_y);
	if (length > 1e-5F) {
		dir_x /= length;
		dir_y /= length;
	}

	const float amount = filter->shake.resolved(filter->strength);
	const vec2 offset_px = {dir_x * amount, dir_y * amount};
	const vec2 source_size = {static_cast<float>(width), static_cast<float>(height)};
	gs_effect_set_vec2(filter->offset_param, &offset_px);
	gs_effect_set_vec2(filter->source_size_param, &source_size);
	obs_source_process_filter_end(filter->source, filter->effect, width, height);
}
} // namespace

void register_shake_filter()
{
	obs_source_info info = {};
	info.id = filter_id;
	info.type = OBS_SOURCE_TYPE_FILTER;
	info.output_flags = OBS_SOURCE_VIDEO;
	info.get_name = shake_name;
	info.create = shake_create;
	info.destroy = shake_destroy;
	info.update = shake_update;
	info.get_defaults = shake_defaults;
	info.get_properties = shake_properties;
	info.video_tick = shake_tick;
	info.video_render = shake_render;
	obs_register_source(&info);
}
