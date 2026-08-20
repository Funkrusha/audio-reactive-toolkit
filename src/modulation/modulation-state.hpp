// SPDX-FileCopyrightText: 2026 Funkrusha
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "audio/audio-capture.hpp"
#include "analysis/tempo-estimator.hpp"

namespace ArtModulation {
struct Snapshot {
	float level;
	float bass;
	float mids;
	float highs;
	float beat_strength;
	float transient_strength;
	uint64_t beat_counter;
	uint64_t transient_counter;
	bool active;
	// Published alongside the rest of the analysis data so filters can read the current tempo
	// estimate every video tick, not just at the throttled WebSocket/BPM-text rate. `bpm` is `0` and
	// `tempo_locked` is `false` until the estimator has enough beats to produce a result.
	float bpm;
	bool tempo_locked;
};

void publish(const AnalysisSnapshot &snapshot, const TempoSnapshot &tempo);
Snapshot snapshot();
} // namespace ArtModulation
