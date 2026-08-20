// SPDX-FileCopyrightText: 2026 Funkrusha
// SPDX-License-Identifier: GPL-3.0-or-later

#include "modulation-state.hpp"

#include <atomic>

namespace {
std::atomic<float> level{0.0F};
std::atomic<float> bass{0.0F};
std::atomic<float> mids{0.0F};
std::atomic<float> highs{0.0F};
std::atomic<float> beat_strength{0.0F};
std::atomic<float> transient_strength{0.0F};
std::atomic<uint64_t> beat_counter{0};
std::atomic<uint64_t> transient_counter{0};
std::atomic<bool> active{false};
std::atomic<float> bpm{0.0F};
std::atomic<bool> tempo_locked{false};
} // namespace

namespace ArtModulation {
void publish(const AnalysisSnapshot &value, const TempoSnapshot &tempo)
{
	level.store(value.rms, std::memory_order_relaxed);
	bass.store(value.bass, std::memory_order_relaxed);
	mids.store(value.mid, std::memory_order_relaxed);
	highs.store(value.high, std::memory_order_relaxed);
	beat_strength.store(value.beat_strength, std::memory_order_relaxed);
	transient_strength.store(value.transient_strength, std::memory_order_relaxed);
	beat_counter.store(value.beat_counter, std::memory_order_release);
	transient_counter.store(value.transient_counter, std::memory_order_release);
	active.store(value.active, std::memory_order_release);
	bpm.store(tempo.bpm, std::memory_order_relaxed);
	tempo_locked.store(tempo.locked, std::memory_order_relaxed);
}

Snapshot snapshot()
{
	return {level.load(std::memory_order_relaxed),         bass.load(std::memory_order_relaxed),
		mids.load(std::memory_order_relaxed),          highs.load(std::memory_order_relaxed),
		beat_strength.load(std::memory_order_relaxed), transient_strength.load(std::memory_order_relaxed),
		beat_counter.load(std::memory_order_acquire),  transient_counter.load(std::memory_order_acquire),
		active.load(std::memory_order_acquire),        bpm.load(std::memory_order_relaxed),
		tempo_locked.load(std::memory_order_relaxed)};
}
} // namespace ArtModulation
