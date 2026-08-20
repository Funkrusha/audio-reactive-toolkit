// SPDX-FileCopyrightText: 2026 Funkrusha
// SPDX-License-Identifier: GPL-3.0-or-later

#include "custom-shader-filter.hpp"

#include "modulation/modulation-binding.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace {
constexpr char filter_id[] = "art_custom_shader_filter";
constexpr size_t mod_slot_count = 4;

namespace Field {
constexpr char smoothing[] = "smoothing";
constexpr char strength[] = "strength";
constexpr char raw_mode[] = "raw_mode";
constexpr char raw_path[] = "raw_path";
constexpr char shader_body[] = "shader_body";
constexpr char info_hint[] = "info_hint";

constexpr char mix_source[] = "mix_source";
constexpr char mix_min[] = "mix_min";
constexpr char mix_max[] = "mix_max";
} // namespace Field

// Field names for the four generic modulation slots (mod_1..mod_4 in the shader), reusing the same
// ArtModulation::BoundParameter machinery every other filter uses instead of inventing something new
// for user-authored shaders.
constexpr const char *mod_source_fields[mod_slot_count] = {"mod1_source", "mod2_source", "mod3_source", "mod4_source"};
constexpr const char *mod_min_fields[mod_slot_count] = {"mod1_min", "mod2_min", "mod3_min", "mod4_min"};
constexpr const char *mod_max_fields[mod_slot_count] = {"mod1_max", "mod2_max", "mod3_max", "mod4_max"};

// Boilerplate injected around the user's "shader body" in wrapper mode, so the user only ever has to
// write the inside of `mainImage` - no ViewProj/VSDefault/technique ceremony required.
constexpr char wrapper_header[] = R"(
uniform float4x4 ViewProj;
uniform texture2d image;
uniform float2 source_size;
uniform float elapsed_time;
uniform float effect_mix;
uniform float mod_1;
uniform float mod_2;
uniform float mod_3;
uniform float mod_4;

sampler_state textureSampler {
	Filter = Linear;
	AddressU = Clamp;
	AddressV = Clamp;
};

struct VertData {
	float4 pos : POSITION;
	float2 uv : TEXCOORD0;
};

VertData VSDefault(VertData input)
{
	VertData output;
	output.pos = mul(float4(input.pos.xyz, 1.0), ViewProj);
	output.uv = input.uv;
	return output;
}

float4 mainImage(float2 uv)
{
)";

constexpr char wrapper_footer[] = R"(
}

float4 PSMain(VertData input) : TARGET
{
	// effect_mix crossfades the shader's own output back against the untouched original pixel, so
	// "how much of the effect shows" doesn't rely on OBS scene compositing/alpha at all - it just
	// mixes two colors this pass already has on hand. 100% = only the effect, 0% = only the original.
	float4 original = image.Sample(textureSampler, input.uv);
	float4 color = mainImage(input.uv);
	float mix_amount = saturate(effect_mix);
	// OBS composites some source types (video-decoded sources in particular) with premultiplied-alpha
	// blending, so a shader that writes a partial alpha without premultiplying its own RGB can come out
	// as a bright/wrong-colored fringe. Premultiplying both sides here keeps that correct through the mix.
	original.rgb *= original.a;
	color.rgb *= color.a;
	return lerp(original, color, mix_amount);
}

technique Draw
{
	pass
	{
		vertex_shader = VSDefault(input);
		pixel_shader = PSMain(input);
	}
}
)";

// Default wrapper body: a plain passthrough, so a freshly added filter renders the untouched source
// instead of garbage while the user starts editing.
constexpr char default_shader_body[] =
	"// uv: 0..1 screen coordinates. Available: image, textureSampler, source_size, elapsed_time, mod_1..mod_4.\n"
	"return image.Sample(textureSampler, uv);\n";

struct CustomShaderFilter {
	obs_source_t *source = nullptr;
	gs_effect_t *effect = nullptr;
	gs_eparam_t *source_size_param = nullptr;
	gs_eparam_t *elapsed_time_param = nullptr;
	gs_eparam_t *mix_param = nullptr;
	std::array<gs_eparam_t *, mod_slot_count> mod_params{};

	bool raw_mode = false;
	std::string raw_path;
	std::string shader_body;

	// Shared "Return/Spring" damping for all bound parameters.
	float damping = 8.0F;
	float strength = 1.0F;
	float elapsed_time = 0.0F;

	// How much of the shader's output shows vs. the untouched original - a real crossfade against the
	// original pixel (see PSMain above), not an alpha/compositing trick.
	ArtModulation::BoundParameter mix{Field::mix_source, Field::mix_min, Field::mix_max};

	std::array<ArtModulation::BoundParameter, mod_slot_count> mods{{
		ArtModulation::BoundParameter{mod_source_fields[0], mod_min_fields[0], mod_max_fields[0]},
		ArtModulation::BoundParameter{mod_source_fields[1], mod_min_fields[1], mod_max_fields[1]},
		ArtModulation::BoundParameter{mod_source_fields[2], mod_min_fields[2], mod_max_fields[2]},
		ArtModulation::BoundParameter{mod_source_fields[3], mod_min_fields[3], mod_max_fields[3]},
	}};
};

const char *custom_shader_name(void *)
{
	return obs_module_text("CustomShaderFilter.Name");
}

// (Re)compiles the effect from whatever source the filter currently points at (wrapper body or raw
// file). On failure the previous, still-working effect (if any) is kept running and the error is
// logged - so a typo while editing doesn't blank out the filter or drop it from the source.
void custom_shader_recompile(CustomShaderFilter *filter)
{
	gs_effect_t *new_effect = nullptr;
	char *error = nullptr;

	obs_enter_graphics();
	if (filter->raw_mode) {
		if (!filter->raw_path.empty())
			new_effect = gs_effect_create_from_file(filter->raw_path.c_str(), &error);
	} else {
		const std::string source = std::string(wrapper_header) + filter->shader_body + wrapper_footer;
		new_effect = gs_effect_create(source.c_str(), "art_custom_shader_filter", &error);
	}

	if (new_effect) {
		if (filter->effect)
			gs_effect_destroy(filter->effect);
		filter->effect = new_effect;
		// Any of these may legitimately come back null (e.g. a raw .effect that doesn't declare
		// "elapsed_time"); gs_effect_set_* no-ops on a null param, so unused uniforms are simply
		// left unbound instead of being an error.
		filter->source_size_param = gs_effect_get_param_by_name(filter->effect, "source_size");
		filter->elapsed_time_param = gs_effect_get_param_by_name(filter->effect, "elapsed_time");
		filter->mix_param = gs_effect_get_param_by_name(filter->effect, "effect_mix");
		for (size_t i = 0; i < filter->mod_params.size(); ++i) {
			const std::string name = "mod_" + std::to_string(i + 1);
			filter->mod_params[i] = gs_effect_get_param_by_name(filter->effect, name.c_str());
		}
	} else {
		obs_log(LOG_WARNING, "ART Custom Shader: compile failed, keeping previous effect: %s",
			error ? error : "unknown error");
	}
	bfree(error);
	obs_leave_graphics();
}

void custom_shader_update(void *data, obs_data_t *settings)
{
	auto *filter = static_cast<CustomShaderFilter *>(data);
	filter->damping = std::clamp(static_cast<float>(obs_data_get_double(settings, Field::smoothing)), 1.0F, 30.0F);
	filter->strength =
		std::clamp(static_cast<float>(obs_data_get_double(settings, Field::strength)), 0.0F, 100.0F) / 100.0F;

	for (auto &mod : filter->mods)
		ArtModulation::read_binding(settings, mod, -10.0F, 10.0F);
	ArtModulation::read_binding(settings, filter->mix, 0.0F, 100.0F);

	const bool raw_mode = obs_data_get_bool(settings, Field::raw_mode);
	const std::string raw_path = obs_data_get_string(settings, Field::raw_path);
	const std::string shader_body = obs_data_get_string(settings, Field::shader_body);

	// Only recompile when the actual source changed - update() also fires for unrelated settings
	// (e.g. dragging the strength slider), and a shader recompile on every such tick would be wasteful.
	const bool source_changed = raw_mode != filter->raw_mode || raw_path != filter->raw_path ||
				    shader_body != filter->shader_body;
	filter->raw_mode = raw_mode;
	filter->raw_path = raw_path;
	filter->shader_body = shader_body;

	if (source_changed)
		custom_shader_recompile(filter);
}

void *custom_shader_create(obs_data_t *settings, obs_source_t *source)
{
	auto *filter = new CustomShaderFilter;
	filter->source = source;
	custom_shader_update(filter, settings);
	return filter;
}

void custom_shader_destroy(void *data)
{
	auto *filter = static_cast<CustomShaderFilter *>(data);
	obs_enter_graphics();
	if (filter->effect)
		gs_effect_destroy(filter->effect);
	obs_leave_graphics();
	delete filter;
}

// Absolute path to the bundled "Audio Kaleidoscope Pulse" example, used to pre-fill Advanced mode so
// flipping the switch loads something working immediately instead of an empty path.
std::string example_effect_path()
{
	char *path = obs_module_file("effects/custom-shader-example.effect");
	std::string result = path ? path : "";
	bfree(path);
	return result;
}

void custom_shader_defaults(obs_data_t *settings)
{
	obs_data_set_default_double(settings, Field::smoothing, 8.0);
	obs_data_set_default_double(settings, Field::strength, 100.0);
	obs_data_set_default_bool(settings, Field::raw_mode, false);
	obs_data_set_default_string(settings, Field::raw_path, example_effect_path().c_str());
	obs_data_set_default_string(settings, Field::shader_body, default_shader_body);
	obs_data_set_default_string(settings, Field::info_hint, obs_module_text("CustomShaderFilter.HintText"));

	// Full effect by default and unbound (Source::None) - existing shaders keep looking exactly like
	// they did before this control existed, until someone deliberately dials it down or binds it.
	obs_data_set_default_int(settings, Field::mix_source, static_cast<int>(ArtModulation::Source::None));
	obs_data_set_default_double(settings, Field::mix_min, 100.0);
	obs_data_set_default_double(settings, Field::mix_max, 100.0);

	for (size_t i = 0; i < mod_slot_count; ++i) {
		obs_data_set_default_int(settings, mod_source_fields[i], static_cast<int>(ArtModulation::Source::None));
		obs_data_set_default_double(settings, mod_min_fields[i], 0.0);
		obs_data_set_default_double(settings, mod_max_fields[i], 1.0);
	}
}

// Toggles which of "shader body" / "raw effect path" is visible depending on the advanced-mode switch.
bool custom_shader_mode_modified(obs_properties_t *properties, obs_property_t *, obs_data_t *settings)
{
	const bool raw_mode = obs_data_get_bool(settings, Field::raw_mode);
	obs_property_set_visible(obs_properties_get(properties, Field::shader_body), !raw_mode);
	obs_property_set_visible(obs_properties_get(properties, Field::raw_path), raw_mode);
	return true;
}

obs_properties_t *custom_shader_properties(void *data)
{
	// OBS re-applies each control's current settings value after get_properties() returns, but it does
	// not re-run modified_callback to fix up dynamic visibility - so the initial visibility set here has
	// to match the filter's actual current mode, not just assume "off", or reopening the properties
	// dialog on an Advanced-mode filter would show the wrong fields until the checkbox is toggled once.
	const auto *filter = static_cast<const CustomShaderFilter *>(data);
	const bool raw_mode = filter && filter->raw_mode;

	obs_properties_t *properties = obs_properties_create();
	obs_properties_add_float_slider(properties, Field::smoothing, obs_module_text("CustomShaderFilter.Smoothing"),
					1.0, 30.0, 0.5);
	obs_properties_add_float_slider(properties, Field::strength, obs_module_text("CustomShaderFilter.Strength"),
					0.0, 100.0, 1.0);

	obs_property_t *mode =
		obs_properties_add_bool(properties, Field::raw_mode, obs_module_text("CustomShaderFilter.RawMode"));
	obs_property_t *shader_body_prop = obs_properties_add_text(
		properties, Field::shader_body, obs_module_text("CustomShaderFilter.ShaderBody"), OBS_TEXT_MULTILINE);
	const std::string example_path = example_effect_path();
	obs_property_t *raw_path_prop = obs_properties_add_path(properties, Field::raw_path,
								obs_module_text("CustomShaderFilter.RawPath"),
								OBS_PATH_FILE, "OBS Effect (*.effect)",
								example_path.empty() ? nullptr : example_path.c_str());
	obs_property_set_visible(shader_body_prop, !raw_mode);
	obs_property_set_visible(raw_path_prop, raw_mode);
	obs_property_set_modified_callback(mode, custom_shader_mode_modified);

	obs_properties_add_group(
		properties, "mix_group", obs_module_text("CustomShaderFilter.MixGroup"), OBS_GROUP_NORMAL,
		ArtModulation::make_binding_group(Field::mix_source, Field::mix_min, Field::mix_max,
						  obs_module_text("CustomShaderFilter.MixMin"),
						  obs_module_text("CustomShaderFilter.MixMax"), 0.0, 100.0, 1.0));

	static constexpr const char *group_ids[mod_slot_count] = {"mod1_group", "mod2_group", "mod3_group",
								  "mod4_group"};
	static constexpr const char *group_keys[mod_slot_count] = {"CustomShaderFilter.Mod1Group",
								   "CustomShaderFilter.Mod2Group",
								   "CustomShaderFilter.Mod3Group",
								   "CustomShaderFilter.Mod4Group"};
	for (size_t i = 0; i < mod_slot_count; ++i) {
		obs_properties_add_group(properties, group_ids[i], obs_module_text(group_keys[i]), OBS_GROUP_NORMAL,
					 ArtModulation::make_binding_group(
						 mod_source_fields[i], mod_min_fields[i], mod_max_fields[i],
						 obs_module_text("CustomShaderFilter.ModMin"),
						 obs_module_text("CustomShaderFilter.ModMax"), -10.0, 10.0, 0.01));
	}

	obs_property_t *hint = obs_properties_add_text(
		properties, Field::info_hint, obs_module_text("CustomShaderFilter.HintLabel"), OBS_TEXT_DEFAULT);
	obs_property_set_enabled(hint, false);
	return properties;
}

void custom_shader_tick(void *data, float seconds)
{
	auto *filter = static_cast<CustomShaderFilter *>(data);
	const ArtModulation::Snapshot audio = ArtModulation::snapshot();
	for (auto &mod : filter->mods)
		mod.value = mod.channel.update(mod.binding, audio, filter->damping, seconds);
	filter->mix.value = filter->mix.channel.update(filter->mix.binding, audio, filter->damping, seconds);
	// Wrapped so shaders using elapsed_time for periodic motion (sin/cos) don't lose float precision
	// over a long streaming session.
	filter->elapsed_time = std::fmod(filter->elapsed_time + seconds, 100000.0F);
}

void custom_shader_render(void *data, gs_effect_t *)
{
	auto *filter = static_cast<CustomShaderFilter *>(data);
	obs_source_t *target = obs_filter_get_target(filter->source);
	const uint32_t width = target ? obs_source_get_base_width(target) : 0;
	const uint32_t height = target ? obs_source_get_base_height(target) : 0;
	if (width == 0 || height == 0 || !filter->effect ||
	    !obs_source_process_filter_begin(filter->source, GS_RGBA, OBS_NO_DIRECT_RENDERING)) {
		obs_source_skip_video_filter(filter->source);
		return;
	}

	const vec2 source_size = {static_cast<float>(width), static_cast<float>(height)};
	gs_effect_set_vec2(filter->source_size_param, &source_size);
	gs_effect_set_float(filter->elapsed_time_param, filter->elapsed_time);
	gs_effect_set_float(filter->mix_param, filter->mix.resolved(filter->strength) / 100.0F);
	for (size_t i = 0; i < filter->mod_params.size(); ++i)
		gs_effect_set_float(filter->mod_params[i], filter->mods[i].resolved(filter->strength));
	obs_source_process_filter_end(filter->source, filter->effect, width, height);
}
} // namespace

void register_custom_shader_filter()
{
	obs_source_info info = {};
	info.id = filter_id;
	info.type = OBS_SOURCE_TYPE_FILTER;
	info.output_flags = OBS_SOURCE_VIDEO;
	info.get_name = custom_shader_name;
	info.create = custom_shader_create;
	info.destroy = custom_shader_destroy;
	info.update = custom_shader_update;
	info.get_defaults = custom_shader_defaults;
	info.get_properties = custom_shader_properties;
	info.video_tick = custom_shader_tick;
	info.video_render = custom_shader_render;
	obs_register_source(&info);
}
