// SPDX-FileCopyrightText: 2026 Funkrusha
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mosaic-filter.hpp"

#include "modulation/modulation-binding.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <algorithm>
#include <cmath>

namespace {
constexpr char filter_id[] = "art_mosaic_filter";

namespace Field {
constexpr char rows[] = "rows";
constexpr char columns[] = "columns";
constexpr char smoothing[] = "smoothing";
constexpr char strength[] = "strength";

constexpr char gap_source[] = "gap_source";
constexpr char gap_min[] = "gap_min";
constexpr char gap_max[] = "gap_max";

constexpr char scatter_source[] = "scatter_source";
constexpr char scatter_min[] = "scatter_min";
constexpr char scatter_max[] = "scatter_max";

constexpr char rotation_source[] = "rotation_source";
constexpr char rotation_min[] = "rotation_min";
constexpr char rotation_max[] = "rotation_max";

constexpr char scale_source[] = "scale_source";
constexpr char scale_min[] = "scale_min";
constexpr char scale_max[] = "scale_max";

constexpr char z_push_source[] = "z_push_source";
constexpr char z_push_min[] = "z_push_min";
constexpr char z_push_max[] = "z_push_max";
} // namespace Field

struct MosaicFilter {
	obs_source_t *source = nullptr;
	gs_effect_t *effect = nullptr;
	gs_eparam_t *rows_param = nullptr;
	gs_eparam_t *columns_param = nullptr;
	gs_eparam_t *gap_param = nullptr;
	gs_eparam_t *tile_scale_param = nullptr;
	gs_eparam_t *scatter_param = nullptr;
	gs_eparam_t *rotation_param = nullptr;
	gs_eparam_t *z_push_param = nullptr;
	gs_eparam_t *source_size_param = nullptr;

	uint32_t rows = 6;
	uint32_t columns = 8;
	// Shared "Return/Spring" damping for all bound parameters.
	float damping = 8.0F;
	float strength = 1.0F;

	ArtModulation::BoundParameter gap{Field::gap_source, Field::gap_min, Field::gap_max};
	ArtModulation::BoundParameter scatter{Field::scatter_source, Field::scatter_min, Field::scatter_max};
	ArtModulation::BoundParameter rotation{Field::rotation_source, Field::rotation_min, Field::rotation_max};
	ArtModulation::BoundParameter scale{Field::scale_source, Field::scale_min, Field::scale_max};
	ArtModulation::BoundParameter z_push{Field::z_push_source, Field::z_push_min, Field::z_push_max};
};

const char *mosaic_name(void *)
{
	return obs_module_text("MosaicFilter.Name");
}

void mosaic_update(void *data, obs_data_t *settings)
{
	auto *filter = static_cast<MosaicFilter *>(data);
	filter->rows = static_cast<uint32_t>(std::clamp<int64_t>(obs_data_get_int(settings, Field::rows), 1, 32));
	filter->columns = static_cast<uint32_t>(std::clamp<int64_t>(obs_data_get_int(settings, Field::columns), 1, 32));
	filter->damping = std::clamp(static_cast<float>(obs_data_get_double(settings, Field::smoothing)), 1.0F, 30.0F);
	filter->strength =
		std::clamp(static_cast<float>(obs_data_get_double(settings, Field::strength)), 0.0F, 100.0F) / 100.0F;

	ArtModulation::read_binding(settings, filter->gap, 0.0F, 50.0F);
	ArtModulation::read_binding(settings, filter->scatter, 0.0F, 500.0F);
	ArtModulation::read_binding(settings, filter->rotation, -180.0F, 180.0F);
	ArtModulation::read_binding(settings, filter->scale, 0.1F, 5.0F);
	ArtModulation::read_binding(settings, filter->z_push, 0.0F, 500.0F);
}

void *mosaic_create(obs_data_t *settings, obs_source_t *source)
{
	auto *filter = new MosaicFilter;
	filter->source = source;

	char *path = obs_module_file("effects/mosaic.effect");
	char *error = nullptr;
	obs_enter_graphics();
	filter->effect = gs_effect_create_from_file(path, &error);
	obs_leave_graphics();
	if (!filter->effect) {
		obs_log(LOG_ERROR, "could not load Mosaic effect '%s': %s", path, error ? error : "unknown error");
		bfree(error);
		bfree(path);
		delete filter;
		return nullptr;
	}
	bfree(error);
	bfree(path);

	filter->rows_param = gs_effect_get_param_by_name(filter->effect, "rows");
	filter->columns_param = gs_effect_get_param_by_name(filter->effect, "columns");
	filter->gap_param = gs_effect_get_param_by_name(filter->effect, "gap_pixels");
	filter->tile_scale_param = gs_effect_get_param_by_name(filter->effect, "tile_scale");
	filter->scatter_param = gs_effect_get_param_by_name(filter->effect, "scatter_pixels");
	filter->rotation_param = gs_effect_get_param_by_name(filter->effect, "rotation_degrees");
	filter->z_push_param = gs_effect_get_param_by_name(filter->effect, "z_push_pixels");
	filter->source_size_param = gs_effect_get_param_by_name(filter->effect, "source_size");
	mosaic_update(filter, settings);
	return filter;
}

void mosaic_destroy(void *data)
{
	auto *filter = static_cast<MosaicFilter *>(data);
	obs_enter_graphics();
	gs_effect_destroy(filter->effect);
	obs_leave_graphics();
	delete filter;
}

void mosaic_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, Field::rows, 6);
	obs_data_set_default_int(settings, Field::columns, 8);
	obs_data_set_default_double(settings, Field::smoothing, 8.0);
	obs_data_set_default_double(settings, Field::strength, 100.0);

	// Defaults mirror the reference modulation table; Gap stays static (Source::None) by default.
	obs_data_set_default_int(settings, Field::gap_source, static_cast<int>(ArtModulation::Source::None));
	obs_data_set_default_double(settings, Field::gap_min, 2.0);
	obs_data_set_default_double(settings, Field::gap_max, 20.0);

	obs_data_set_default_int(settings, Field::scatter_source, static_cast<int>(ArtModulation::Source::Bass));
	obs_data_set_default_double(settings, Field::scatter_min, 0.0);
	obs_data_set_default_double(settings, Field::scatter_max, 80.0);

	obs_data_set_default_int(settings, Field::rotation_source, static_cast<int>(ArtModulation::Source::Highs));
	obs_data_set_default_double(settings, Field::rotation_min, 0.0);
	obs_data_set_default_double(settings, Field::rotation_max, 12.0);

	obs_data_set_default_int(settings, Field::scale_source, static_cast<int>(ArtModulation::Source::Beat));
	obs_data_set_default_double(settings, Field::scale_min, 1.0);
	obs_data_set_default_double(settings, Field::scale_max, 1.15);

	obs_data_set_default_int(settings, Field::z_push_source, static_cast<int>(ArtModulation::Source::Transient));
	obs_data_set_default_double(settings, Field::z_push_min, 0.0);
	obs_data_set_default_double(settings, Field::z_push_max, 150.0);
}

obs_properties_t *mosaic_properties(void *)
{
	obs_properties_t *properties = obs_properties_create();
	obs_properties_add_int_slider(properties, Field::rows, obs_module_text("MosaicFilter.Rows"), 1, 32, 1);
	obs_properties_add_int_slider(properties, Field::columns, obs_module_text("MosaicFilter.Columns"), 1, 32, 1);
	obs_properties_add_float_slider(properties, Field::smoothing, obs_module_text("MosaicFilter.Smoothing"), 1.0,
					30.0, 0.5);
	obs_properties_add_float_slider(properties, Field::strength, obs_module_text("MosaicFilter.Strength"), 0.0,
					100.0, 1.0);

	obs_properties_add_group(properties, "gap_group", obs_module_text("MosaicFilter.GapGroup"), OBS_GROUP_NORMAL,
				 ArtModulation::make_binding_group(Field::gap_source, Field::gap_min, Field::gap_max,
								   obs_module_text("MosaicFilter.GapMin"),
								   obs_module_text("MosaicFilter.GapMax"), 0.0, 50.0,
								   0.5));
	obs_properties_add_group(
		properties, "scatter_group", obs_module_text("MosaicFilter.ScatterGroup"), OBS_GROUP_NORMAL,
		ArtModulation::make_binding_group(Field::scatter_source, Field::scatter_min, Field::scatter_max,
						  obs_module_text("MosaicFilter.ScatterMin"),
						  obs_module_text("MosaicFilter.ScatterMax"), 0.0, 500.0, 1.0));
	obs_properties_add_group(
		properties, "rotation_group", obs_module_text("MosaicFilter.RotationGroup"), OBS_GROUP_NORMAL,
		ArtModulation::make_binding_group(Field::rotation_source, Field::rotation_min, Field::rotation_max,
						  obs_module_text("MosaicFilter.RotationMin"),
						  obs_module_text("MosaicFilter.RotationMax"), -180.0, 180.0, 1.0));
	obs_properties_add_group(
		properties, "scale_group", obs_module_text("MosaicFilter.ScaleGroup"), OBS_GROUP_NORMAL,
		ArtModulation::make_binding_group(Field::scale_source, Field::scale_min, Field::scale_max,
						  obs_module_text("MosaicFilter.ScaleMin"),
						  obs_module_text("MosaicFilter.ScaleMax"), 0.1, 5.0, 0.01));
	obs_properties_add_group(
		properties, "z_push_group", obs_module_text("MosaicFilter.ZPushGroup"), OBS_GROUP_NORMAL,
		ArtModulation::make_binding_group(Field::z_push_source, Field::z_push_min, Field::z_push_max,
						  obs_module_text("MosaicFilter.ZPushMin"),
						  obs_module_text("MosaicFilter.ZPushMax"), 0.0, 500.0, 1.0));
	return properties;
}

void mosaic_tick(void *data, float seconds)
{
	auto *filter = static_cast<MosaicFilter *>(data);
	const ArtModulation::Snapshot audio = ArtModulation::snapshot();
	filter->gap.value = filter->gap.channel.update(filter->gap.binding, audio, filter->damping, seconds);
	filter->scatter.value =
		filter->scatter.channel.update(filter->scatter.binding, audio, filter->damping, seconds);
	filter->rotation.value =
		filter->rotation.channel.update(filter->rotation.binding, audio, filter->damping, seconds);
	filter->scale.value = filter->scale.channel.update(filter->scale.binding, audio, filter->damping, seconds);
	filter->z_push.value = filter->z_push.channel.update(filter->z_push.binding, audio, filter->damping, seconds);
}

void mosaic_render(void *data, gs_effect_t *)
{
	auto *filter = static_cast<MosaicFilter *>(data);
	obs_source_t *target = obs_filter_get_target(filter->source);
	const uint32_t width = target ? obs_source_get_base_width(target) : 0;
	const uint32_t height = target ? obs_source_get_base_height(target) : 0;
	if (width == 0 || height == 0 ||
	    !obs_source_process_filter_begin(filter->source, GS_RGBA, OBS_NO_DIRECT_RENDERING)) {
		obs_source_skip_video_filter(filter->source);
		return;
	}

	const vec2 source_size = {static_cast<float>(width), static_cast<float>(height)};
	gs_effect_set_float(filter->rows_param, static_cast<float>(filter->rows));
	gs_effect_set_float(filter->columns_param, static_cast<float>(filter->columns));
	gs_effect_set_float(filter->gap_param, filter->gap.resolved(filter->strength));
	gs_effect_set_float(filter->tile_scale_param, filter->scale.resolved(filter->strength));
	gs_effect_set_float(filter->scatter_param, filter->scatter.resolved(filter->strength));
	gs_effect_set_float(filter->rotation_param, filter->rotation.resolved(filter->strength));
	gs_effect_set_float(filter->z_push_param, filter->z_push.resolved(filter->strength));
	gs_effect_set_vec2(filter->source_size_param, &source_size);
	obs_source_process_filter_end(filter->source, filter->effect, width, height);
}
} // namespace

void register_mosaic_filter()
{
	obs_source_info info = {};
	info.id = filter_id;
	info.type = OBS_SOURCE_TYPE_FILTER;
	info.output_flags = OBS_SOURCE_VIDEO;
	info.get_name = mosaic_name;
	info.create = mosaic_create;
	info.destroy = mosaic_destroy;
	info.update = mosaic_update;
	info.get_defaults = mosaic_defaults;
	info.get_properties = mosaic_properties;
	info.video_tick = mosaic_tick;
	info.video_render = mosaic_render;
	obs_register_source(&info);
}
