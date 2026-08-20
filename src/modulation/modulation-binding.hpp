// SPDX-FileCopyrightText: 2026 Funkrusha
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "modulation-state.hpp"

#include <obs-module.h>

// Reusable mapping from an ART audio signal onto a filter parameter's [min, max] range, shared
// across future native ART effects (Mosaic, RGB Split, Glitch, ...).
namespace ArtModulation {

// The ART signals a parameter can be bound to.
enum class Source {
	None,
	Level,
	Bass,
	Mids,
	Highs,
	Beat,
	Transient,
};

// A parameter binding: which signal drives it, and the output range it's mapped onto (e.g.
// Scatter/Bass/0px/80px).
struct Binding {
	Source source = Source::None;
	float min = 0.0F;
	float max = 0.0F;
	// Compensates band energies (Bass/Mids/Highs) sitting well below 0..1. See `default_gain()`.
	float gain = 1.0F;
};

// Sensible default `Binding::gain` for a given source.
float default_gain(Source source);

// Per-parameter runtime state: turns a `Binding` plus the live `Snapshot` into a value remapped
// into [min, max]. Continuous signals are smoothed directly; event signals (Beat/Transient) latch
// an envelope per event and let it decay, so the parameter springs back to `min` between events.
// `Source::None` always resolves to `min` (no modulation, constant value).
class Channel {
public:
	// `damping` is the shared "Return/Spring" speed; `seconds` is the frame delta time.
	float update(const Binding &binding, const Snapshot &snapshot, float damping, float seconds);

private:
	float envelope_ = 0.0F;
	uint64_t last_beat_counter_ = 0;
	uint64_t last_transient_counter_ = 0;
	bool initialized_ = false;
};

// One audio-reactive filter parameter: its obs_data settings keys, binding, and runtime envelope.
// Shared across filters so each one just supplies its own field names (e.g. Mosaic's "scatter_*"
// vs. RGB Split's "split_*") instead of re-implementing the binding/channel plumbing.
struct BoundParameter {
	const char *source_field;
	const char *min_field;
	const char *max_field;
	Binding binding;
	Channel channel;
	float value = 0.0F;

	// How far strength lets this swing from its resting `min` towards the audio-driven `value`.
	float resolved(float strength) const { return binding.min + (value - binding.min) * strength; }
};

// Reads `parameter`'s binding out of `settings`, clamping min/max to [min_clamp, max_clamp].
void read_binding(obs_data_t *settings, BoundParameter &parameter, float min_clamp, float max_clamp);

// Adds a Source dropdown (None/Level/Bass/Mids/Highs/Beat/Transient) to `properties`.
void add_source_list(obs_properties_t *properties, const char *field_name);

// Builds a "Source + Min + Max" property group for one bound parameter.
obs_properties_t *make_binding_group(const char *source_field, const char *min_field, const char *max_field,
				     const char *min_label, const char *max_label, double range_min, double range_max,
				     double step);

} // namespace ArtModulation
