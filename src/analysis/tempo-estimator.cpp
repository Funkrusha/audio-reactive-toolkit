// SPDX-FileCopyrightText: 2026 Funkrusha
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tempo-estimator.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {
constexpr double nanoseconds_per_second = 1000000000.0;
constexpr double minimum_interval_seconds = 0.2;
constexpr double maximum_interval_seconds = 2.0;
constexpr double minimum_bpm = 70.0;
constexpr double maximum_bpm = 180.0;
constexpr float stale_timeout_seconds = 4.0F;
constexpr float bpm_deadband = 0.25F;
constexpr float maximum_bpm_step = 0.5F;
constexpr float fine_smoothing = 0.18F;
constexpr float confidence_smoothing = 0.2F;
constexpr float lock_confidence = 0.65F;
constexpr float unlock_confidence = 0.3F;
constexpr double tempo_cluster_radius = 3.0;
constexpr float minimum_update_confidence = 0.5F;

double normalize_bpm(double bpm)
{
	while (bpm < minimum_bpm)
		bpm *= 2.0;
	while (bpm > maximum_bpm)
		bpm *= 0.5;
	return bpm;
}

double median(std::vector<double> values)
{
	if (values.empty())
		return 0.0;
	std::sort(values.begin(), values.end());
	const size_t middle = values.size() / 2;
	return values.size() % 2 == 0 ? (values[middle - 1] + values[middle]) * 0.5 : values[middle];
}
} // namespace

void TempoEstimator::add_beat(uint64_t timestamp_ns)
{
	seconds_since_beat_ = 0.0F;
	if (last_beat_timestamp_ != 0 && timestamp_ns > last_beat_timestamp_) {
		const double interval =
			static_cast<double>(timestamp_ns - last_beat_timestamp_) / nanoseconds_per_second;
		if (interval >= minimum_interval_seconds && interval <= maximum_interval_seconds) {
			intervals_[next_interval_] = interval;
			next_interval_ = (next_interval_ + 1) % interval_capacity;
			interval_count_ = std::min(interval_count_ + 1, interval_capacity);
			estimate();
		}
	}
	last_beat_timestamp_ = timestamp_ns;
}

void TempoEstimator::update(float seconds)
{
	seconds_since_beat_ += seconds;
	if (last_beat_timestamp_ != 0 && seconds_since_beat_ > stale_timeout_seconds)
		reset();
}

void TempoEstimator::reset()
{
	interval_count_ = 0;
	next_interval_ = 0;
	last_beat_timestamp_ = 0;
	seconds_since_beat_ = 0.0F;
	result_ = {};
}

TempoSnapshot TempoEstimator::snapshot() const
{
	return result_;
}

void TempoEstimator::estimate()
{
	std::vector<double> candidates;
	candidates.reserve(interval_count_);
	for (size_t index = 0; index < interval_count_; ++index)
		candidates.push_back(normalize_bpm(60.0 / intervals_[index]));

	double cluster_center = candidates.front();
	size_t strongest_cluster_size = 0;
	for (const double anchor : candidates) {
		size_t cluster_size = 0;
		for (const double candidate : candidates) {
			if (std::abs(candidate - anchor) <= tempo_cluster_radius)
				++cluster_size;
		}
		const bool closer_to_current = result_.bpm > 0.0F &&
					       std::abs(anchor - result_.bpm) < std::abs(cluster_center - result_.bpm);
		if (cluster_size > strongest_cluster_size ||
		    (cluster_size == strongest_cluster_size && closer_to_current)) {
			strongest_cluster_size = cluster_size;
			cluster_center = anchor;
		}
	}

	std::vector<double> accepted;
	accepted.reserve(candidates.size());
	for (const double candidate : candidates) {
		if (std::abs(candidate - cluster_center) <= tempo_cluster_radius)
			accepted.push_back(candidate);
	}
	if (accepted.empty())
		return;

	const double bpm = median(accepted);
	double absolute_deviation = 0.0;
	for (const double candidate : accepted)
		absolute_deviation += std::abs(candidate - bpm);
	absolute_deviation /= static_cast<double>(accepted.size());
	const double consistency = std::clamp(1.0 - absolute_deviation / tempo_cluster_radius, 0.0, 1.0);
	const double sample_factor = std::min(1.0, static_cast<double>(interval_count_) / 8.0);
	const double cluster_dominance = static_cast<double>(accepted.size()) / static_cast<double>(interval_count_);
	const double accepted_factor = std::min(1.0, cluster_dominance / 0.5);
	const float target_bpm = static_cast<float>(bpm);
	const float measured_confidence = static_cast<float>(sample_factor * accepted_factor * consistency);
	if (result_.bpm == 0.0F && measured_confidence >= minimum_update_confidence) {
		result_.bpm = target_bpm;
	} else if (result_.bpm > 0.0F && measured_confidence >= minimum_update_confidence) {
		const float difference = target_bpm - result_.bpm;
		if (std::abs(difference) > bpm_deadband) {
			const float step = std::abs(difference) < 2.0F
						   ? difference * fine_smoothing
						   : std::clamp(difference, -maximum_bpm_step, maximum_bpm_step);
			result_.bpm += step;
		}
	}

	result_.confidence = result_.confidence == 0.0F ? measured_confidence
							: result_.confidence * (1.0F - confidence_smoothing) +
								  measured_confidence * confidence_smoothing;
	if (!result_.locked && interval_count_ >= 8 && result_.confidence >= lock_confidence)
		result_.locked = true;
	else if (result_.locked && result_.confidence < unlock_confidence)
		result_.locked = false;
}
