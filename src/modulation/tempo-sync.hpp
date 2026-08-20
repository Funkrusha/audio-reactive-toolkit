// SPDX-FileCopyrightText: 2026 Funkrusha
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "modulation-state.hpp"

// Phase-locked oscillator: reusable building block for a filter's "Sync to BPM" option (Wave's phase,
// and potentially Shake/Glitch's step timer later). Free-runs at a rate derived from the live tempo
// estimate between beats, and gently nudges its phase back towards the nearest beat-aligned cycle
// boundary whenever a new Beat event lands - so it stays roughly on-tempo without visibly snapping on
// every single beat. BPM alone only fixes the oscillator's *rate*; without this correction its phase
// would slowly drift out of alignment with the actual beat.
namespace ArtModulation {

class TempoOscillator {
public:
	// `cycles_per_beat` scales the oscillator rate relative to the detected beat (1.0 = one full cycle
	// per beat, 2.0 = twice per beat, 0.5 = one cycle every two beats). `lock_strength` is 0..1: how
	// much of the phase error gets corrected on each new beat event (0 = ignore beats entirely, pure
	// BPM-derived speed; 1 = hard-snap to the nearest cycle boundary every beat). Returns a continuously
	// increasing phase where each whole number is one cycle boundary - it does not wrap, so it's a drop-in
	// replacement for a filter's own `phase += speed_hz * seconds` accumulator (radians: multiply by
	// 2*pi yourself; step counters: use floor()/frac() directly, same as before). While `Snapshot::bpm`
	// is `0` (tempo not locked yet), the phase does not advance at all instead of guessing a fallback speed.
	float advance(const Snapshot &snapshot, float cycles_per_beat, float lock_strength, float seconds);

private:
	float phase_ = 0.0F;
	uint64_t last_beat_counter_ = 0;
	bool initialized_ = false;
};

} // namespace ArtModulation
