// SPDX-FileCopyrightText: 2026 Funkrusha
// SPDX-License-Identifier: GPL-3.0-or-later

#include "modulation-binding.hpp"

#include <algorithm>
#include <cmath>

namespace {
float raw_signal(ArtModulation::Source source, const ArtModulation::Snapshot &snapshot)
{
	switch (source) {
	case ArtModulation::Source::Level:
		return snapshot.level;
	case ArtModulation::Source::Bass:
		return snapshot.bass;
	case ArtModulation::Source::Mids:
		return snapshot.mids;
	case ArtModulation::Source::Highs:
		return snapshot.highs;
	case ArtModulation::Source::Beat:
		return snapshot.beat_strength;
	case ArtModulation::Source::Transient:
		return snapshot.transient_strength;
	case ArtModulation::Source::None:
	default:
		return 0.0F;
	}
}
} // namespace

namespace ArtModulation {

float default_gain(Source source)
{
	switch (source) {
	// Band energies rarely approach 1.0, so amplify them before treating as a 0..1 amount.
	case Source::Bass:
	case Source::Mids:
	case Source::Highs:
		return 5.0F;
	default:
		return 1.0F;
	}
}

float Channel::update(const Binding &binding, const Snapshot &snapshot, float damping, float seconds)
{
	const float clamped_seconds = std::clamp(seconds, 0.0F, 0.25F);

	if (binding.source == Source::None) {
		envelope_ = 0.0F;
		initialized_ = true;
		return binding.min;
	}

	if (binding.source == Source::Beat || binding.source == Source::Transient) {
		const bool is_beat = binding.source == Source::Beat;
		const uint64_t counter = is_beat ? snapshot.beat_counter : snapshot.transient_counter;
		const float strength = is_beat ? snapshot.beat_strength : snapshot.transient_strength;
		uint64_t &last_counter = is_beat ? last_beat_counter_ : last_transient_counter_;

		if (!initialized_) {
			last_counter = counter;
		} else if (counter != last_counter) {
			envelope_ = std::max(envelope_, std::clamp(strength * binding.gain, 0.0F, 1.0F));
			last_counter = counter;
		}
		envelope_ *= std::exp(-damping * clamped_seconds);
	} else {
		const float target = std::clamp(raw_signal(binding.source, snapshot) * binding.gain, 0.0F, 1.0F);
		const float blend = 1.0F - std::exp(-damping * clamped_seconds);
		envelope_ += (target - envelope_) * blend;
	}

	initialized_ = true;
	return binding.min + (binding.max - binding.min) * envelope_;
}

void read_binding(obs_data_t *settings, BoundParameter &parameter, float min_clamp, float max_clamp)
{
	Binding &binding = parameter.binding;
	const int64_t raw_source = std::clamp<int64_t>(obs_data_get_int(settings, parameter.source_field), 0,
						       static_cast<int64_t>(Source::Transient));
	binding.source = static_cast<Source>(raw_source);
	binding.min = std::clamp(static_cast<float>(obs_data_get_double(settings, parameter.min_field)), min_clamp,
				 max_clamp);
	binding.max = std::clamp(static_cast<float>(obs_data_get_double(settings, parameter.max_field)), min_clamp,
				 max_clamp);
	binding.gain = default_gain(binding.source);
}

void add_source_list(obs_properties_t *properties, const char *field_name)
{
	obs_property_t *list = obs_properties_add_list(properties, field_name, obs_module_text("Modulation.Source"),
						       OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(list, obs_module_text("Modulation.Source.None"), static_cast<int>(Source::None));
	obs_property_list_add_int(list, obs_module_text("Modulation.Source.Level"), static_cast<int>(Source::Level));
	obs_property_list_add_int(list, obs_module_text("Modulation.Source.Bass"), static_cast<int>(Source::Bass));
	obs_property_list_add_int(list, obs_module_text("Modulation.Source.Mids"), static_cast<int>(Source::Mids));
	obs_property_list_add_int(list, obs_module_text("Modulation.Source.Highs"), static_cast<int>(Source::Highs));
	obs_property_list_add_int(list, obs_module_text("Modulation.Source.Beat"), static_cast<int>(Source::Beat));
	obs_property_list_add_int(list, obs_module_text("Modulation.Source.Transient"),
				  static_cast<int>(Source::Transient));
}

obs_properties_t *make_binding_group(const char *source_field, const char *min_field, const char *max_field,
				     const char *min_label, const char *max_label, double range_min, double range_max,
				     double step)
{
	obs_properties_t *group = obs_properties_create();
	add_source_list(group, source_field);
	obs_properties_add_float_slider(group, min_field, min_label, range_min, range_max, step);
	obs_properties_add_float_slider(group, max_field, max_label, range_min, range_max, step);
	return group;
}

} // namespace ArtModulation
