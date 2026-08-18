// SPDX-FileCopyrightText: 2026 Funkrusha
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

struct TempoSnapshot {
	float bpm = 0.0F;
	float confidence = 0.0F;
	bool locked = false;
};

class TempoEstimator {
public:
	void add_beat(uint64_t timestamp_ns);
	void update(float seconds);
	void reset();
	TempoSnapshot snapshot() const;

private:
	void estimate();

	static constexpr size_t interval_capacity = 32;
	std::array<double, interval_capacity> intervals_{};
	size_t interval_count_ = 0;
	size_t next_interval_ = 0;
	uint64_t last_beat_timestamp_ = 0;
	float seconds_since_beat_ = 0.0F;
	TempoSnapshot result_{};
};
