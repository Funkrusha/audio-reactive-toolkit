// SPDX-FileCopyrightText: 2026 Funkrusha
// SPDX-License-Identifier: GPL-3.0-or-later

#include "pixelate-filter.hpp"

#include "modulation/modulation-binding.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <algorithm>

namespace {
constexpr char filter_id[] = "art_pixelate_filter";

namespace Field {
constexpr char smoothing[] = "smoothing";
constexpr char strength[] = "strength";

constexpr char pixel_source[] = "pixel_source";
constexpr char pixel_min[] = "pixel_min";
constexpr char pixel_max[] = "pixel_max";
} // namespace Field

struct PixelateFilter {
	obs_source_t *source = nullptr;
	gs_effect_t *effect = nullptr;
	gs_eparam_t *pixel_size_param = nullptr;
	gs_eparam_t *source_size_param = nullptr;

	// Shared "Return/Spring" damping for the bound parameter below.
	float damping = 8.0F;
	float strength = 1.0F;

	ArtModulation::BoundParameter pixel_size{Field::pixel_source, Field::pixel_min, Field::pixel_max};
};

const char *pixelate_name(void *)
{
	return obs_module_text("PixelateFilter.Name");
}

void pixelate_update(void *data, obs_data_t *settings)
{
	auto *filter = static_cast<PixelateFilter *>(data);
	filter->damping = std::clamp(static_cast<float>(obs_data_get_double(settings, Field::smoothing)), 1.0F, 30.0F);
	filter->strength =
		std::clamp(static_cast<float>(obs_data_get_double(settings, Field::strength)), 0.0F, 100.0F) / 100.0F;

	ArtModulation::read_binding(settings, filter->pixel_size, 1.0F, 100.0F);
}

void *pixelate_create(obs_data_t *settings, obs_source_t *source)
{
	auto *filter = new PixelateFilter;
	filter->source = source;

	char *path = obs_module_file("effects/pixelate.effect");
	char *error = nullptr;
	obs_enter_graphics();
	filter->effect = gs_effect_create_from_file(path, &error);
	obs_leave_graphics();
	if (!filter->effect) {
		obs_log(LOG_ERROR, "could not load Pixelate effect '%s': %s", path, error ? error : "unknown error");
		bfree(error);
		bfree(path);
		delete filter;
		return nullptr;
	}
	bfree(error);
	bfree(path);

	filter->pixel_size_param = gs_effect_get_param_by_name(filter->effect, "pixel_size");
	filter->source_size_param = gs_effect_get_param_by_name(filter->effect, "source_size");
	pixelate_update(filter, settings);
	return filter;
}

void pixelate_destroy(void *data)
{
	auto *filter = static_cast<PixelateFilter *>(data);
	obs_enter_graphics();
	gs_effect_destroy(filter->effect);
	obs_leave_graphics();
	delete filter;
}

void pixelate_defaults(obs_data_t *settings)
{
	obs_data_set_default_double(settings, Field::smoothing, 8.0);
	obs_data_set_default_double(settings, Field::strength, 100.0);

	obs_data_set_default_int(settings, Field::pixel_source, static_cast<int>(ArtModulation::Source::Level));
	obs_data_set_default_double(settings, Field::pixel_min, 1.0);
	obs_data_set_default_double(settings, Field::pixel_max, 30.0);
}

obs_properties_t *pixelate_properties(void *)
{
	obs_properties_t *properties = obs_properties_create();
	obs_properties_add_float_slider(properties, Field::smoothing, obs_module_text("PixelateFilter.Smoothing"), 1.0,
					30.0, 0.5);
	obs_properties_add_float_slider(properties, Field::strength, obs_module_text("PixelateFilter.Strength"), 0.0,
					100.0, 1.0);

	obs_properties_add_group(
		properties, "pixel_group", obs_module_text("PixelateFilter.PixelGroup"), OBS_GROUP_NORMAL,
		ArtModulation::make_binding_group(Field::pixel_source, Field::pixel_min, Field::pixel_max,
						  obs_module_text("PixelateFilter.PixelMin"),
						  obs_module_text("PixelateFilter.PixelMax"), 1.0, 100.0, 1.0));
	return properties;
}

void pixelate_tick(void *data, float seconds)
{
	auto *filter = static_cast<PixelateFilter *>(data);
	const ArtModulation::Snapshot audio = ArtModulation::snapshot();
	filter->pixel_size.value =
		filter->pixel_size.channel.update(filter->pixel_size.binding, audio, filter->damping, seconds);
}

void pixelate_render(void *data, gs_effect_t *)
{
	auto *filter = static_cast<PixelateFilter *>(data);
	obs_source_t *target = obs_filter_get_target(filter->source);
	const uint32_t width = target ? obs_source_get_base_width(target) : 0;
	const uint32_t height = target ? obs_source_get_base_height(target) : 0;
	if (width == 0 || height == 0 ||
	    !obs_source_process_filter_begin(filter->source, GS_RGBA, OBS_NO_DIRECT_RENDERING)) {
		obs_source_skip_video_filter(filter->source);
		return;
	}

	const float pixel_size = std::max(filter->pixel_size.resolved(filter->strength), 1.0F);
	const vec2 source_size = {static_cast<float>(width), static_cast<float>(height)};
	gs_effect_set_float(filter->pixel_size_param, pixel_size);
	gs_effect_set_vec2(filter->source_size_param, &source_size);
	obs_source_process_filter_end(filter->source, filter->effect, width, height);
}
} // namespace

void register_pixelate_filter()
{
	obs_source_info info = {};
	info.id = filter_id;
	info.type = OBS_SOURCE_TYPE_FILTER;
	info.output_flags = OBS_SOURCE_VIDEO;
	info.get_name = pixelate_name;
	info.create = pixelate_create;
	info.destroy = pixelate_destroy;
	info.update = pixelate_update;
	info.get_defaults = pixelate_defaults;
	info.get_properties = pixelate_properties;
	info.video_tick = pixelate_tick;
	info.video_render = pixelate_render;
	obs_register_source(&info);
}
