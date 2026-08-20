// SPDX-FileCopyrightText: 2026 Funkrusha
// SPDX-License-Identifier: GPL-3.0-or-later

#include "glitch-filter.hpp"

#include "modulation/modulation-binding.hpp"
#include "modulation/tempo-sync.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <algorithm>
#include <cmath>

namespace {
constexpr char filter_id[] = "art_glitch_filter";

namespace Field {
constexpr char block_count[] = "block_count";
constexpr char speed[] = "speed";
constexpr char smoothing[] = "smoothing";
constexpr char strength[] = "strength";

constexpr char sync_to_bpm[] = "sync_to_bpm";
constexpr char sync_cycles_per_beat[] = "sync_cycles_per_beat";
constexpr char sync_lock_strength[] = "sync_lock_strength";

constexpr char amount_source[] = "amount_source";
constexpr char amount_min[] = "amount_min";
constexpr char amount_max[] = "amount_max";
} // namespace Field

struct GlitchFilter {
	obs_source_t *source = nullptr;
	gs_effect_t *effect = nullptr;
	gs_eparam_t *block_count_param = nullptr;
	gs_eparam_t *seed_param = nullptr;
	gs_eparam_t *amount_param = nullptr;
	gs_eparam_t *source_size_param = nullptr;

	// Number of horizontal glitch bands.
	float block_count = 24.0F;
	// How many times per second the bands re-randomize. Ignored while sync_to_bpm is on.
	float speed_hz = 20.0F;
	// Shared "Return/Spring" damping for the bound parameter below.
	float damping = 8.0F;
	float strength = 1.0F;
	// Advances by `speed_hz * seconds` (or by the tempo oscillator, in sync mode); its floor reseeds the
	// shader's per-block hash each step.
	float phase = 0.0F;

	// When true, `phase` tracks the live BPM estimate (via `tempo_oscillator`) instead of `speed_hz`.
	bool sync_to_bpm = false;
	// Reseeds per beat, e.g. 1.0 = one re-randomize per beat, 4.0 = four per beat.
	float sync_cycles_per_beat = 1.0F;
	// 0..1: how strongly each new beat nudges the phase back on-tempo. 0 ignores beats (pure BPM speed).
	float sync_lock_strength = 0.35F;
	ArtModulation::TempoOscillator tempo_oscillator;

	ArtModulation::BoundParameter amount{Field::amount_source, Field::amount_min, Field::amount_max};
};

const char *glitch_name(void *)
{
	return obs_module_text("GlitchFilter.Name");
}

void glitch_update(void *data, obs_data_t *settings)
{
	auto *filter = static_cast<GlitchFilter *>(data);
	filter->block_count =
		std::clamp(static_cast<float>(obs_data_get_double(settings, Field::block_count)), 4.0F, 64.0F);
	filter->speed_hz = std::clamp(static_cast<float>(obs_data_get_double(settings, Field::speed)), 1.0F, 60.0F);
	filter->damping = std::clamp(static_cast<float>(obs_data_get_double(settings, Field::smoothing)), 1.0F, 30.0F);
	filter->strength =
		std::clamp(static_cast<float>(obs_data_get_double(settings, Field::strength)), 0.0F, 100.0F) / 100.0F;

	filter->sync_to_bpm = obs_data_get_bool(settings, Field::sync_to_bpm);
	filter->sync_cycles_per_beat =
		std::clamp(static_cast<float>(obs_data_get_double(settings, Field::sync_cycles_per_beat)), 0.25F, 8.0F);
	filter->sync_lock_strength =
		std::clamp(static_cast<float>(obs_data_get_double(settings, Field::sync_lock_strength)), 0.0F, 100.0F) /
		100.0F;

	ArtModulation::read_binding(settings, filter->amount, 0.0F, 150.0F);
}

void *glitch_create(obs_data_t *settings, obs_source_t *source)
{
	auto *filter = new GlitchFilter;
	filter->source = source;

	char *path = obs_module_file("effects/glitch.effect");
	char *error = nullptr;
	obs_enter_graphics();
	filter->effect = gs_effect_create_from_file(path, &error);
	obs_leave_graphics();
	if (!filter->effect) {
		obs_log(LOG_ERROR, "could not load Glitch effect '%s': %s", path, error ? error : "unknown error");
		bfree(error);
		bfree(path);
		delete filter;
		return nullptr;
	}
	bfree(error);
	bfree(path);

	filter->block_count_param = gs_effect_get_param_by_name(filter->effect, "block_count");
	filter->seed_param = gs_effect_get_param_by_name(filter->effect, "seed");
	filter->amount_param = gs_effect_get_param_by_name(filter->effect, "amount_px");
	filter->source_size_param = gs_effect_get_param_by_name(filter->effect, "source_size");
	glitch_update(filter, settings);
	return filter;
}

void glitch_destroy(void *data)
{
	auto *filter = static_cast<GlitchFilter *>(data);
	obs_enter_graphics();
	gs_effect_destroy(filter->effect);
	obs_leave_graphics();
	delete filter;
}

void glitch_defaults(obs_data_t *settings)
{
	obs_data_set_default_double(settings, Field::block_count, 24.0);
	obs_data_set_default_double(settings, Field::speed, 20.0);
	obs_data_set_default_double(settings, Field::smoothing, 8.0);
	obs_data_set_default_double(settings, Field::strength, 100.0);

	obs_data_set_default_bool(settings, Field::sync_to_bpm, false);
	obs_data_set_default_double(settings, Field::sync_cycles_per_beat, 1.0);
	obs_data_set_default_double(settings, Field::sync_lock_strength, 35.0);

	obs_data_set_default_int(settings, Field::amount_source, static_cast<int>(ArtModulation::Source::Transient));
	obs_data_set_default_double(settings, Field::amount_min, 0.0);
	obs_data_set_default_double(settings, Field::amount_max, 30.0);
}

// Toggles "Glitch speed (Hz)" vs. the two BPM-sync sliders depending on the Sync to BPM checkbox.
bool glitch_sync_modified(obs_properties_t *properties, obs_property_t *, obs_data_t *settings)
{
	const bool sync_to_bpm = obs_data_get_bool(settings, Field::sync_to_bpm);
	obs_property_set_visible(obs_properties_get(properties, Field::speed), !sync_to_bpm);
	obs_property_set_visible(obs_properties_get(properties, Field::sync_cycles_per_beat), sync_to_bpm);
	obs_property_set_visible(obs_properties_get(properties, Field::sync_lock_strength), sync_to_bpm);
	return true;
}

obs_properties_t *glitch_properties(void *data)
{
	// See the matching comment in custom-shader-filter.cpp: get_properties() has to read the filter's
	// actual current mode for the initial visibility, since OBS won't re-run modified_callback for us.
	const auto *filter = static_cast<const GlitchFilter *>(data);
	const bool sync_to_bpm = filter && filter->sync_to_bpm;

	obs_properties_t *properties = obs_properties_create();
	obs_properties_add_int_slider(properties, Field::block_count, obs_module_text("GlitchFilter.BlockCount"), 4, 64,
				      1);
	obs_property_t *speed_prop = obs_properties_add_float_slider(
		properties, Field::speed, obs_module_text("GlitchFilter.Speed"), 1.0, 60.0, 1.0);
	obs_properties_add_float_slider(properties, Field::smoothing, obs_module_text("GlitchFilter.Smoothing"), 1.0,
					30.0, 0.5);
	obs_properties_add_float_slider(properties, Field::strength, obs_module_text("GlitchFilter.Strength"), 0.0,
					100.0, 1.0);

	obs_property_t *sync =
		obs_properties_add_bool(properties, Field::sync_to_bpm, obs_module_text("GlitchFilter.SyncToBpm"));
	obs_property_t *cycles_prop =
		obs_properties_add_float_slider(properties, Field::sync_cycles_per_beat,
						obs_module_text("GlitchFilter.SyncReseedsPerBeat"), 0.25, 8.0, 0.25);
	obs_property_t *lock_prop = obs_properties_add_float_slider(properties, Field::sync_lock_strength,
								    obs_module_text("GlitchFilter.SyncLockStrength"),
								    0.0, 100.0, 1.0);
	obs_property_set_visible(speed_prop, !sync_to_bpm);
	obs_property_set_visible(cycles_prop, sync_to_bpm);
	obs_property_set_visible(lock_prop, sync_to_bpm);
	obs_property_set_modified_callback(sync, glitch_sync_modified);

	obs_properties_add_group(
		properties, "amount_group", obs_module_text("GlitchFilter.AmountGroup"), OBS_GROUP_NORMAL,
		ArtModulation::make_binding_group(Field::amount_source, Field::amount_min, Field::amount_max,
						  obs_module_text("GlitchFilter.AmountMin"),
						  obs_module_text("GlitchFilter.AmountMax"), 0.0, 150.0, 1.0));
	return properties;
}

void glitch_tick(void *data, float seconds)
{
	auto *filter = static_cast<GlitchFilter *>(data);
	const ArtModulation::Snapshot audio = ArtModulation::snapshot();
	filter->amount.value = filter->amount.channel.update(filter->amount.binding, audio, filter->damping, seconds);
	if (filter->sync_to_bpm) {
		filter->phase = filter->tempo_oscillator.advance(audio, filter->sync_cycles_per_beat,
								 filter->sync_lock_strength, seconds);
	} else {
		filter->phase += filter->speed_hz * seconds;
	}
}

void glitch_render(void *data, gs_effect_t *)
{
	auto *filter = static_cast<GlitchFilter *>(data);
	obs_source_t *target = obs_filter_get_target(filter->source);
	const uint32_t width = target ? obs_source_get_base_width(target) : 0;
	const uint32_t height = target ? obs_source_get_base_height(target) : 0;
	if (width == 0 || height == 0 ||
	    !obs_source_process_filter_begin(filter->source, GS_RGBA, OBS_NO_DIRECT_RENDERING)) {
		obs_source_skip_video_filter(filter->source);
		return;
	}

	const float seed = std::floor(filter->phase);
	const float amount_px = filter->amount.resolved(filter->strength);
	const vec2 source_size = {static_cast<float>(width), static_cast<float>(height)};
	gs_effect_set_float(filter->block_count_param, filter->block_count);
	gs_effect_set_float(filter->seed_param, seed);
	gs_effect_set_float(filter->amount_param, amount_px);
	gs_effect_set_vec2(filter->source_size_param, &source_size);
	obs_source_process_filter_end(filter->source, filter->effect, width, height);
}
} // namespace

void register_glitch_filter()
{
	obs_source_info info = {};
	info.id = filter_id;
	info.type = OBS_SOURCE_TYPE_FILTER;
	info.output_flags = OBS_SOURCE_VIDEO;
	info.get_name = glitch_name;
	info.create = glitch_create;
	info.destroy = glitch_destroy;
	info.update = glitch_update;
	info.get_defaults = glitch_defaults;
	info.get_properties = glitch_properties;
	info.video_tick = glitch_tick;
	info.video_render = glitch_render;
	obs_register_source(&info);
}
