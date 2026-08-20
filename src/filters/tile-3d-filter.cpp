// SPDX-FileCopyrightText: 2026 Funkrusha
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tile-3d-filter.hpp"

#include "modulation/modulation-binding.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <algorithm>

namespace {
constexpr char filter_id[] = "art_tile_3d_filter";

namespace Field {
constexpr char rows[] = "rows";
constexpr char columns[] = "columns";
constexpr char gap[] = "gap";
constexpr char depth[] = "depth";
constexpr char smoothing[] = "smoothing";
constexpr char strength[] = "strength";

constexpr char tilt_source[] = "tilt_source";
constexpr char tilt_min[] = "tilt_min";
constexpr char tilt_max[] = "tilt_max";
} // namespace Field

struct Tile3DFilter {
	obs_source_t *source = nullptr;
	gs_effect_t *effect = nullptr;
	gs_eparam_t *rows_param = nullptr;
	gs_eparam_t *columns_param = nullptr;
	gs_eparam_t *gap_param = nullptr;
	gs_eparam_t *depth_param = nullptr;
	gs_eparam_t *tilt_param = nullptr;
	gs_eparam_t *source_size_param = nullptr;

	uint32_t rows = 4;
	uint32_t columns = 6;
	// Static gap between tiles, in pixels.
	float gap_px = 4.0F;
	// Camera distance for the per-tile perspective; smaller = stronger 3D look for the same tilt.
	float depth = 1.6F;
	// Shared "Return/Spring" damping for the bound parameter below.
	float damping = 8.0F;
	float strength = 1.0F;

	ArtModulation::BoundParameter tilt{Field::tilt_source, Field::tilt_min, Field::tilt_max};
};

const char *tile_3d_name(void *)
{
	return obs_module_text("Tile3DFilter.Name");
}

void tile_3d_update(void *data, obs_data_t *settings)
{
	auto *filter = static_cast<Tile3DFilter *>(data);
	filter->rows = static_cast<uint32_t>(std::clamp<int64_t>(obs_data_get_int(settings, Field::rows), 1, 32));
	filter->columns = static_cast<uint32_t>(std::clamp<int64_t>(obs_data_get_int(settings, Field::columns), 1, 32));
	filter->gap_px = std::clamp(static_cast<float>(obs_data_get_double(settings, Field::gap)), 0.0F, 50.0F);
	filter->depth = std::clamp(static_cast<float>(obs_data_get_double(settings, Field::depth)), 0.8F, 4.0F);
	filter->damping = std::clamp(static_cast<float>(obs_data_get_double(settings, Field::smoothing)), 1.0F, 30.0F);
	filter->strength =
		std::clamp(static_cast<float>(obs_data_get_double(settings, Field::strength)), 0.0F, 100.0F) / 100.0F;

	ArtModulation::read_binding(settings, filter->tilt, 0.0F, 60.0F);
}

void *tile_3d_create(obs_data_t *settings, obs_source_t *source)
{
	auto *filter = new Tile3DFilter;
	filter->source = source;

	char *path = obs_module_file("effects/tile-3d.effect");
	char *error = nullptr;
	obs_enter_graphics();
	filter->effect = gs_effect_create_from_file(path, &error);
	obs_leave_graphics();
	if (!filter->effect) {
		obs_log(LOG_ERROR, "could not load 3D Tile effect '%s': %s", path, error ? error : "unknown error");
		bfree(error);
		bfree(path);
		delete filter;
		return nullptr;
	}
	bfree(error);
	bfree(path);

	filter->rows_param = gs_effect_get_param_by_name(filter->effect, "rows");
	filter->columns_param = gs_effect_get_param_by_name(filter->effect, "columns");
	filter->gap_param = gs_effect_get_param_by_name(filter->effect, "gap_px");
	filter->depth_param = gs_effect_get_param_by_name(filter->effect, "depth");
	filter->tilt_param = gs_effect_get_param_by_name(filter->effect, "tilt_degrees");
	filter->source_size_param = gs_effect_get_param_by_name(filter->effect, "source_size");
	tile_3d_update(filter, settings);
	return filter;
}

void tile_3d_destroy(void *data)
{
	auto *filter = static_cast<Tile3DFilter *>(data);
	obs_enter_graphics();
	gs_effect_destroy(filter->effect);
	obs_leave_graphics();
	delete filter;
}

void tile_3d_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, Field::rows, 4);
	obs_data_set_default_int(settings, Field::columns, 6);
	obs_data_set_default_double(settings, Field::gap, 4.0);
	obs_data_set_default_double(settings, Field::depth, 1.6);
	obs_data_set_default_double(settings, Field::smoothing, 8.0);
	obs_data_set_default_double(settings, Field::strength, 100.0);

	obs_data_set_default_int(settings, Field::tilt_source, static_cast<int>(ArtModulation::Source::Highs));
	obs_data_set_default_double(settings, Field::tilt_min, 0.0);
	obs_data_set_default_double(settings, Field::tilt_max, 35.0);
}

obs_properties_t *tile_3d_properties(void *)
{
	obs_properties_t *properties = obs_properties_create();
	obs_properties_add_int_slider(properties, Field::rows, obs_module_text("Tile3DFilter.Rows"), 1, 32, 1);
	obs_properties_add_int_slider(properties, Field::columns, obs_module_text("Tile3DFilter.Columns"), 1, 32, 1);
	obs_properties_add_float_slider(properties, Field::gap, obs_module_text("Tile3DFilter.Gap"), 0.0, 50.0, 1.0);
	obs_properties_add_float_slider(properties, Field::depth, obs_module_text("Tile3DFilter.Depth"), 0.8, 4.0, 0.1);
	obs_properties_add_float_slider(properties, Field::smoothing, obs_module_text("Tile3DFilter.Smoothing"), 1.0,
					30.0, 0.5);
	obs_properties_add_float_slider(properties, Field::strength, obs_module_text("Tile3DFilter.Strength"), 0.0,
					100.0, 1.0);

	obs_properties_add_group(properties, "tilt_group", obs_module_text("Tile3DFilter.TiltGroup"), OBS_GROUP_NORMAL,
				 ArtModulation::make_binding_group(Field::tilt_source, Field::tilt_min, Field::tilt_max,
								   obs_module_text("Tile3DFilter.TiltMin"),
								   obs_module_text("Tile3DFilter.TiltMax"), 0.0, 60.0,
								   1.0));
	return properties;
}

void tile_3d_tick(void *data, float seconds)
{
	auto *filter = static_cast<Tile3DFilter *>(data);
	const ArtModulation::Snapshot audio = ArtModulation::snapshot();
	filter->tilt.value = filter->tilt.channel.update(filter->tilt.binding, audio, filter->damping, seconds);
}

void tile_3d_render(void *data, gs_effect_t *)
{
	auto *filter = static_cast<Tile3DFilter *>(data);
	obs_source_t *target = obs_filter_get_target(filter->source);
	const uint32_t width = target ? obs_source_get_base_width(target) : 0;
	const uint32_t height = target ? obs_source_get_base_height(target) : 0;
	if (width == 0 || height == 0 ||
	    !obs_source_process_filter_begin(filter->source, GS_RGBA, OBS_NO_DIRECT_RENDERING)) {
		obs_source_skip_video_filter(filter->source);
		return;
	}

	const float tilt_degrees = filter->tilt.resolved(filter->strength);
	const vec2 source_size = {static_cast<float>(width), static_cast<float>(height)};
	gs_effect_set_float(filter->rows_param, static_cast<float>(filter->rows));
	gs_effect_set_float(filter->columns_param, static_cast<float>(filter->columns));
	gs_effect_set_float(filter->gap_param, filter->gap_px);
	gs_effect_set_float(filter->depth_param, filter->depth);
	gs_effect_set_float(filter->tilt_param, tilt_degrees);
	gs_effect_set_vec2(filter->source_size_param, &source_size);
	obs_source_process_filter_end(filter->source, filter->effect, width, height);
}
} // namespace

void register_tile_3d_filter()
{
	obs_source_info info = {};
	info.id = filter_id;
	info.type = OBS_SOURCE_TYPE_FILTER;
	info.output_flags = OBS_SOURCE_VIDEO;
	info.get_name = tile_3d_name;
	info.create = tile_3d_create;
	info.destroy = tile_3d_destroy;
	info.update = tile_3d_update;
	info.get_defaults = tile_3d_defaults;
	info.get_properties = tile_3d_properties;
	info.video_tick = tile_3d_tick;
	info.video_render = tile_3d_render;
	obs_register_source(&info);
}
