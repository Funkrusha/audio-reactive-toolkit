// SPDX-FileCopyrightText: 2026 Funkrusha
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tempo-sync.hpp"

#include <algorithm>
#include <cmath>

namespace ArtModulation {

float TempoOscillator::advance(const Snapshot &snapshot, float cycles_per_beat, float lock_strength, float seconds)
{
	if (snapshot.bpm > 0.0F) {
		const float cycle_hz = (snapshot.bpm / 60.0F) * cycles_per_beat;
		phase_ += cycle_hz * std::clamp(seconds, 0.0F, 0.25F);
	}

	if (!initialized_) {
		// Don't "correct" against a beat counter we've never actually observed advancing yet.
		last_beat_counter_ = snapshot.beat_counter;
		initialized_ = true;
	} else if (snapshot.beat_counter != last_beat_counter_) {
		// Shortest signed distance from the current phase to its nearest cycle boundary (some
		// integer) - works the same regardless of how large phase_ has grown, no wrapping needed.
		const float error = phase_ - std::round(phase_);
		phase_ -= error * std::clamp(lock_strength, 0.0F, 1.0F);
		last_beat_counter_ = snapshot.beat_counter;
	}

	return phase_;
}

} // namespace ArtModulation
