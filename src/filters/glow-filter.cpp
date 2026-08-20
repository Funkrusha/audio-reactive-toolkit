// SPDX-FileCopyrightText: 2026 Funkrusha
// SPDX-License-Identifier: GPL-3.0-or-later

#include "glow-filter.hpp"

#include "modulation/modulation-binding.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <algorithm>

namespace {
constexpr char filter_id[] = "art_glow_filter";

namespace Field {
constexpr char radius[] = "radius";
constexpr char smoothing[] = "smoothing";
constexpr char strength[] = "strength";

constexpr char intensity_source[] = "intensity_source";
constexpr char intensity_min[] = "intensity_min";
constexpr char intensity_max[] = "intensity_max";
} // namespace Field

struct GlowFilter {
	obs_source_t *source = nullptr;
	gs_effect_t *effect = nullptr;
	gs_eparam_t *radius_param = nullptr;
	gs_eparam_t *intensity_param = nullptr;
	gs_eparam_t *source_size_param = nullptr;

	// How far the cheap single-pass blur samples spread, in pixels.
	float radius_px = 15.0F;
	// Shared "Return/Spring" damping for the bound parameter below.
	float damping = 8.0F;
	float strength = 1.0F;

	ArtModulation::BoundParameter intensity{Field::intensity_source, Field::intensity_min, Field::intensity_max};
};

const char *glow_name(void *)
{
	return obs_module_text("GlowFilter.Name");
}

void glow_update(void *data, obs_data_t *settings)
{
	auto *filter = static_cast<GlowFilter *>(data);
	filter->radius_px = std::clamp(static_cast<float>(obs_data_get_double(settings, Field::radius)), 1.0F, 60.0F);
	filter->damping = std::clamp(static_cast<float>(obs_data_get_double(settings, Field::smoothing)), 1.0F, 30.0F);
	filter->strength =
		std::clamp(static_cast<float>(obs_data_get_double(settings, Field::strength)), 0.0F, 100.0F) / 100.0F;

	ArtModulation::read_binding(settings, filter->intensity, 0.0F, 2.0F);
}

void *glow_create(obs_data_t *settings, obs_source_t *source)
{
	auto *filter = new GlowFilter;
	filter->source = source;

	char *path = obs_module_file("effects/glow.effect");
	char *error = nullptr;
	obs_enter_graphics();
	filter->effect = gs_effect_create_from_file(path, &error);
	obs_leave_graphics();
	if (!filter->effect) {
		obs_log(LOG_ERROR, "could not load Glow effect '%s': %s", path, error ? error : "unknown error");
		bfree(error);
		bfree(path);
		delete filter;
		return nullptr;
	}
	bfree(error);
	bfree(path);

	filter->radius_param = gs_effect_get_param_by_name(filter->effect, "radius_px");
	filter->intensity_param = gs_effect_get_param_by_name(filter->effect, "intensity");
	filter->source_size_param = gs_effect_get_param_by_name(filter->effect, "source_size");
	glow_update(filter, settings);
	return filter;
}

void glow_destroy(void *data)
{
	auto *filter = static_cast<GlowFilter *>(data);
	obs_enter_graphics();
	gs_effect_destroy(filter->effect);
	obs_leave_graphics();
	delete filter;
}

void glow_defaults(obs_data_t *settings)
{
	obs_data_set_default_double(settings, Field::radius, 15.0);
	obs_data_set_default_double(settings, Field::smoothing, 8.0);
	obs_data_set_default_double(settings, Field::strength, 100.0);

	obs_data_set_default_int(settings, Field::intensity_source, static_cast<int>(ArtModulation::Source::Beat));
	obs_data_set_default_double(settings, Field::intensity_min, 0.0);
	obs_data_set_default_double(settings, Field::intensity_max, 0.8);
}

obs_properties_t *glow_properties(void *)
{
	obs_properties_t *properties = obs_properties_create();
	obs_properties_add_float_slider(properties, Field::radius, obs_module_text("GlowFilter.Radius"), 1.0, 60.0,
					1.0);
	obs_properties_add_float_slider(properties, Field::smoothing, obs_module_text("GlowFilter.Smoothing"), 1.0,
					30.0, 0.5);
	obs_properties_add_float_slider(properties, Field::strength, obs_module_text("GlowFilter.Strength"), 0.0, 100.0,
					1.0);

	obs_properties_add_group(
		properties, "intensity_group", obs_module_text("GlowFilter.IntensityGroup"), OBS_GROUP_NORMAL,
		ArtModulation::make_binding_group(Field::intensity_source, Field::intensity_min, Field::intensity_max,
						  obs_module_text("GlowFilter.IntensityMin"),
						  obs_module_text("GlowFilter.IntensityMax"), 0.0, 2.0, 0.05));
	return properties;
}

void glow_tick(void *data, float seconds)
{
	auto *filter = static_cast<GlowFilter *>(data);
	const ArtModulation::Snapshot audio = ArtModulation::snapshot();
	filter->intensity.value =
		filter->intensity.channel.update(filter->intensity.binding, audio, filter->damping, seconds);
}

void glow_render(void *data, gs_effect_t *)
{
	auto *filter = static_cast<GlowFilter *>(data);
	obs_source_t *target = obs_filter_get_target(filter->source);
	const uint32_t width = target ? obs_source_get_base_width(target) : 0;
	const uint32_t height = target ? obs_source_get_base_height(target) : 0;
	if (width == 0 || height == 0 ||
	    !obs_source_process_filter_begin(filter->source, GS_RGBA, OBS_NO_DIRECT_RENDERING)) {
		obs_source_skip_video_filter(filter->source);
		return;
	}

	const float intensity = filter->intensity.resolved(filter->strength);
	const vec2 source_size = {static_cast<float>(width), static_cast<float>(height)};
	gs_effect_set_float(filter->radius_param, filter->radius_px);
	gs_effect_set_float(filter->intensity_param, intensity);
	gs_effect_set_vec2(filter->source_size_param, &source_size);
	obs_source_process_filter_end(filter->source, filter->effect, width, height);
}
} // namespace

void register_glow_filter()
{
	obs_source_info info = {};
	info.id = filter_id;
	info.type = OBS_SOURCE_TYPE_FILTER;
	info.output_flags = OBS_SOURCE_VIDEO;
	info.get_name = glow_name;
	info.create = glow_create;
	info.destroy = glow_destroy;
	info.update = glow_update;
	info.get_defaults = glow_defaults;
	info.get_properties = glow_properties;
	info.video_tick = glow_tick;
	info.video_render = glow_render;
	obs_register_source(&info);
}
