// SPDX-FileCopyrightText: 2026 Funkrusha
// SPDX-License-Identifier: GPL-3.0-or-later

#include "rgb-split-filter.hpp"

#include "modulation/modulation-binding.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <algorithm>
#include <cmath>

namespace {
constexpr char filter_id[] = "art_rgb_split_filter";
constexpr float pi = 3.14159265F;

namespace Field {
constexpr char angle[] = "angle";
constexpr char smoothing[] = "smoothing";
constexpr char strength[] = "strength";

constexpr char split_source[] = "split_source";
constexpr char split_min[] = "split_min";
constexpr char split_max[] = "split_max";
} // namespace Field

struct RgbSplitFilter {
	obs_source_t *source = nullptr;
	gs_effect_t *effect = nullptr;
	gs_eparam_t *offset_param = nullptr;
	gs_eparam_t *source_size_param = nullptr;

	// Direction the color channels split apart in, independent of the audio-driven distance.
	float angle_degrees = 0.0F;
	// Shared "Return/Spring" damping for the bound parameter below.
	float damping = 8.0F;
	float strength = 1.0F;

	ArtModulation::BoundParameter split{Field::split_source, Field::split_min, Field::split_max};
};

const char *rgb_split_name(void *)
{
	return obs_module_text("RgbSplitFilter.Name");
}

void rgb_split_update(void *data, obs_data_t *settings)
{
	auto *filter = static_cast<RgbSplitFilter *>(data);
	filter->angle_degrees = static_cast<float>(obs_data_get_double(settings, Field::angle));
	filter->damping = std::clamp(static_cast<float>(obs_data_get_double(settings, Field::smoothing)), 1.0F, 30.0F);
	filter->strength =
		std::clamp(static_cast<float>(obs_data_get_double(settings, Field::strength)), 0.0F, 100.0F) / 100.0F;

	ArtModulation::read_binding(settings, filter->split, 0.0F, 200.0F);
}

void *rgb_split_create(obs_data_t *settings, obs_source_t *source)
{
	auto *filter = new RgbSplitFilter;
	filter->source = source;

	char *path = obs_module_file("effects/rgb-split.effect");
	char *error = nullptr;
	obs_enter_graphics();
	filter->effect = gs_effect_create_from_file(path, &error);
	obs_leave_graphics();
	if (!filter->effect) {
		obs_log(LOG_ERROR, "could not load RGB Split effect '%s': %s", path, error ? error : "unknown error");
		bfree(error);
		bfree(path);
		delete filter;
		return nullptr;
	}
	bfree(error);
	bfree(path);

	filter->offset_param = gs_effect_get_param_by_name(filter->effect, "offset_px");
	filter->source_size_param = gs_effect_get_param_by_name(filter->effect, "source_size");
	rgb_split_update(filter, settings);
	return filter;
}

void rgb_split_destroy(void *data)
{
	auto *filter = static_cast<RgbSplitFilter *>(data);
	obs_enter_graphics();
	gs_effect_destroy(filter->effect);
	obs_leave_graphics();
	delete filter;
}

void rgb_split_defaults(obs_data_t *settings)
{
	obs_data_set_default_double(settings, Field::angle, 0.0);
	obs_data_set_default_double(settings, Field::smoothing, 8.0);
	obs_data_set_default_double(settings, Field::strength, 100.0);

	obs_data_set_default_int(settings, Field::split_source, static_cast<int>(ArtModulation::Source::Bass));
	obs_data_set_default_double(settings, Field::split_min, 0.0);
	obs_data_set_default_double(settings, Field::split_max, 20.0);
}

obs_properties_t *rgb_split_properties(void *)
{
	obs_properties_t *properties = obs_properties_create();
	obs_properties_add_float_slider(properties, Field::angle, obs_module_text("RgbSplitFilter.Angle"), 0.0, 360.0,
					1.0);
	obs_properties_add_float_slider(properties, Field::smoothing, obs_module_text("RgbSplitFilter.Smoothing"), 1.0,
					30.0, 0.5);
	obs_properties_add_float_slider(properties, Field::strength, obs_module_text("RgbSplitFilter.Strength"), 0.0,
					100.0, 1.0);

	obs_properties_add_group(
		properties, "split_group", obs_module_text("RgbSplitFilter.SplitGroup"), OBS_GROUP_NORMAL,
		ArtModulation::make_binding_group(Field::split_source, Field::split_min, Field::split_max,
						  obs_module_text("RgbSplitFilter.SplitMin"),
						  obs_module_text("RgbSplitFilter.SplitMax"), 0.0, 200.0, 1.0));
	return properties;
}

void rgb_split_tick(void *data, float seconds)
{
	auto *filter = static_cast<RgbSplitFilter *>(data);
	const ArtModulation::Snapshot audio = ArtModulation::snapshot();
	filter->split.value = filter->split.channel.update(filter->split.binding, audio, filter->damping, seconds);
}

void rgb_split_render(void *data, gs_effect_t *)
{
	auto *filter = static_cast<RgbSplitFilter *>(data);
	obs_source_t *target = obs_filter_get_target(filter->source);
	const uint32_t width = target ? obs_source_get_base_width(target) : 0;
	const uint32_t height = target ? obs_source_get_base_height(target) : 0;
	if (width == 0 || height == 0 ||
	    !obs_source_process_filter_begin(filter->source, GS_RGBA, OBS_NO_DIRECT_RENDERING)) {
		obs_source_skip_video_filter(filter->source);
		return;
	}

	const float angle_radians = filter->angle_degrees * (pi / 180.0F);
	const float distance = filter->split.resolved(filter->strength);
	const vec2 offset_px = {std::cos(angle_radians) * distance, std::sin(angle_radians) * distance};
	const vec2 source_size = {static_cast<float>(width), static_cast<float>(height)};
	gs_effect_set_vec2(filter->offset_param, &offset_px);
	gs_effect_set_vec2(filter->source_size_param, &source_size);
	obs_source_process_filter_end(filter->source, filter->effect, width, height);
}
} // namespace

void register_rgb_split_filter()
{
	obs_source_info info = {};
	info.id = filter_id;
	info.type = OBS_SOURCE_TYPE_FILTER;
	info.output_flags = OBS_SOURCE_VIDEO;
	info.get_name = rgb_split_name;
	info.create = rgb_split_create;
	info.destroy = rgb_split_destroy;
	info.update = rgb_split_update;
	info.get_defaults = rgb_split_defaults;
	info.get_properties = rgb_split_properties;
	info.video_tick = rgb_split_tick;
	info.video_render = rgb_split_render;
	obs_register_source(&info);
}
