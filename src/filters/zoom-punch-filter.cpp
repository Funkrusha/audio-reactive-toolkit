// SPDX-FileCopyrightText: 2026 Funkrusha
// SPDX-License-Identifier: GPL-3.0-or-later

#include "zoom-punch-filter.hpp"

#include "modulation/modulation-binding.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <algorithm>

namespace {
constexpr char filter_id[] = "art_zoom_punch_filter";

namespace Field {
constexpr char smoothing[] = "smoothing";
constexpr char strength[] = "strength";

constexpr char punch_source[] = "punch_source";
constexpr char punch_min[] = "punch_min";
constexpr char punch_max[] = "punch_max";
} // namespace Field

struct ZoomPunchFilter {
	obs_source_t *source = nullptr;
	gs_effect_t *effect = nullptr;
	gs_eparam_t *scale_param = nullptr;

	// Shared "Return/Spring" damping for the bound parameter below.
	float damping = 8.0F;
	float strength = 1.0F;

	ArtModulation::BoundParameter punch{Field::punch_source, Field::punch_min, Field::punch_max};
};

const char *zoom_punch_name(void *)
{
	return obs_module_text("ZoomPunchFilter.Name");
}

void zoom_punch_update(void *data, obs_data_t *settings)
{
	auto *filter = static_cast<ZoomPunchFilter *>(data);
	filter->damping = std::clamp(static_cast<float>(obs_data_get_double(settings, Field::smoothing)), 1.0F, 30.0F);
	filter->strength =
		std::clamp(static_cast<float>(obs_data_get_double(settings, Field::strength)), 0.0F, 100.0F) / 100.0F;

	ArtModulation::read_binding(settings, filter->punch, 1.0F, 3.0F);
}

void *zoom_punch_create(obs_data_t *settings, obs_source_t *source)
{
	auto *filter = new ZoomPunchFilter;
	filter->source = source;

	char *path = obs_module_file("effects/zoom-punch.effect");
	char *error = nullptr;
	obs_enter_graphics();
	filter->effect = gs_effect_create_from_file(path, &error);
	obs_leave_graphics();
	if (!filter->effect) {
		obs_log(LOG_ERROR, "could not load Zoom/Punch effect '%s': %s", path, error ? error : "unknown error");
		bfree(error);
		bfree(path);
		delete filter;
		return nullptr;
	}
	bfree(error);
	bfree(path);

	filter->scale_param = gs_effect_get_param_by_name(filter->effect, "scale");
	zoom_punch_update(filter, settings);
	return filter;
}

void zoom_punch_destroy(void *data)
{
	auto *filter = static_cast<ZoomPunchFilter *>(data);
	obs_enter_graphics();
	gs_effect_destroy(filter->effect);
	obs_leave_graphics();
	delete filter;
}

void zoom_punch_defaults(obs_data_t *settings)
{
	obs_data_set_default_double(settings, Field::smoothing, 8.0);
	obs_data_set_default_double(settings, Field::strength, 100.0);

	obs_data_set_default_int(settings, Field::punch_source, static_cast<int>(ArtModulation::Source::Beat));
	obs_data_set_default_double(settings, Field::punch_min, 1.0);
	obs_data_set_default_double(settings, Field::punch_max, 1.15);
}

obs_properties_t *zoom_punch_properties(void *)
{
	obs_properties_t *properties = obs_properties_create();
	obs_properties_add_float_slider(properties, Field::smoothing, obs_module_text("ZoomPunchFilter.Smoothing"), 1.0,
					30.0, 0.5);
	obs_properties_add_float_slider(properties, Field::strength, obs_module_text("ZoomPunchFilter.Strength"), 0.0,
					100.0, 1.0);

	obs_properties_add_group(
		properties, "punch_group", obs_module_text("ZoomPunchFilter.PunchGroup"), OBS_GROUP_NORMAL,
		ArtModulation::make_binding_group(Field::punch_source, Field::punch_min, Field::punch_max,
						  obs_module_text("ZoomPunchFilter.PunchMin"),
						  obs_module_text("ZoomPunchFilter.PunchMax"), 1.0, 3.0, 0.01));
	return properties;
}

void zoom_punch_tick(void *data, float seconds)
{
	auto *filter = static_cast<ZoomPunchFilter *>(data);
	const ArtModulation::Snapshot audio = ArtModulation::snapshot();
	filter->punch.value = filter->punch.channel.update(filter->punch.binding, audio, filter->damping, seconds);
}

void zoom_punch_render(void *data, gs_effect_t *)
{
	auto *filter = static_cast<ZoomPunchFilter *>(data);
	obs_source_t *target = obs_filter_get_target(filter->source);
	const uint32_t width = target ? obs_source_get_base_width(target) : 0;
	const uint32_t height = target ? obs_source_get_base_height(target) : 0;
	if (width == 0 || height == 0 ||
	    !obs_source_process_filter_begin(filter->source, GS_RGBA, OBS_NO_DIRECT_RENDERING)) {
		obs_source_skip_video_filter(filter->source);
		return;
	}

	const float scale = filter->punch.resolved(filter->strength);
	gs_effect_set_float(filter->scale_param, scale);
	obs_source_process_filter_end(filter->source, filter->effect, width, height);
}
} // namespace

void register_zoom_punch_filter()
{
	obs_source_info info = {};
	info.id = filter_id;
	info.type = OBS_SOURCE_TYPE_FILTER;
	info.output_flags = OBS_SOURCE_VIDEO;
	info.get_name = zoom_punch_name;
	info.create = zoom_punch_create;
	info.destroy = zoom_punch_destroy;
	info.update = zoom_punch_update;
	info.get_defaults = zoom_punch_defaults;
	info.get_properties = zoom_punch_properties;
	info.video_tick = zoom_punch_tick;
	info.video_render = zoom_punch_render;
	obs_register_source(&info);
}
