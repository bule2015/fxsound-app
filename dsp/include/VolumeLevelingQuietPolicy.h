#pragma once

#include <cmath>

namespace FxSound
{
namespace VolumeLevelingQuietPolicy
{
constexpr float kVeryQuietRmsThreshold = 0.035f;
constexpr float kQuietAudiblePeakThreshold = 0.0035f;
constexpr float kQuietMaxGain = 10.0f;
constexpr float kQuietActivationSeconds = 10.0f;
constexpr float kQuietFloorReleaseRmsThreshold = 0.06f;
constexpr float kQuietFloorReleaseAlpha = 0.02f;
constexpr float kQuietFloorSilenceDecayAlpha = 0.08f;
constexpr float kQuietPeakTargetRatio = 0.98f;
constexpr float kQuietPeakFloorRaiseTimeSeconds = 6.0f;
constexpr float kQuietHeadroomReduceLimit = 0.25f;
constexpr float kQuietGainReferenceRms = 0.125f;

inline float clampValue(float value, float min_value, float max_value)
{
	if (value < min_value)
		return min_value;
	if (value > max_value)
		return max_value;
	return value;
}

struct TransitionInput
{
	float peak = 0.0f;
	float post_gain_rms = 0.0f;
	float quiet_duration_before = 0.0f;
	float buffer_duration_seconds = 0.0f;
	float gain_end = 1.0f;
	float effective_target_rms = 0.0f;
	float effective_ceiling = 1.0f;
	float quiet_gain_floor = 1.0f;
	float rolling_peak_max = 0.0f;
	float headroom_reduce_score = 0.0f;
	bool quiet_peak_window_ready = false;
};

struct TransitionResult
{
	float quiet_duration_after = 0.0f;
	float quiet_gain_floor_after = 1.0f;
	bool post_gain_still_quiet = false;
	bool quiet_boost_had_authority = false;
	bool floor_raise_applied = false;
	bool silence_decay_applied = false;
	bool release_decay_applied = false;
};

inline TransitionResult applyTransition(const TransitionInput& input)
{
	TransitionResult result;
	result.quiet_gain_floor_after = (input.quiet_gain_floor > 1.0f) ? input.quiet_gain_floor : 1.0f;

	result.post_gain_still_quiet =
		input.peak > kQuietAudiblePeakThreshold &&
		input.post_gain_rms < kVeryQuietRmsThreshold;

	result.quiet_duration_after = result.post_gain_still_quiet
		? input.quiet_duration_before + input.buffer_duration_seconds
		: 0.0f;

	result.quiet_boost_had_authority =
		input.quiet_duration_before >= kQuietActivationSeconds &&
		input.gain_end > (input.effective_target_rms / kQuietGainReferenceRms);

	if (result.quiet_boost_had_authority && input.gain_end > result.quiet_gain_floor_after)
	{
		result.quiet_gain_floor_after = input.gain_end;
	}

	const bool quiet_floor_is_active =
		input.quiet_duration_before >= kQuietActivationSeconds ||
		result.quiet_gain_floor_after > 1.0f;
	const float quiet_peak_target = input.effective_ceiling * kQuietPeakTargetRatio;
	const bool sustained_headroom_available =
		input.rolling_peak_max > kQuietAudiblePeakThreshold &&
		input.rolling_peak_max < quiet_peak_target &&
		input.headroom_reduce_score < kQuietHeadroomReduceLimit;

	if (input.quiet_peak_window_ready && quiet_floor_is_active && sustained_headroom_available)
	{
		float desired_quiet_floor =
			result.quiet_gain_floor_after *
			(quiet_peak_target / ((input.rolling_peak_max > 1.0e-6f) ? input.rolling_peak_max : 1.0e-6f));
		desired_quiet_floor = clampValue(desired_quiet_floor, result.quiet_gain_floor_after, kQuietMaxGain);

		const float quiet_floor_raise_alpha = clampValue(
			input.buffer_duration_seconds / kQuietPeakFloorRaiseTimeSeconds,
			0.0005f,
			0.05f);
		result.quiet_gain_floor_after =
			result.quiet_gain_floor_after * (1.0f - quiet_floor_raise_alpha) +
			desired_quiet_floor * quiet_floor_raise_alpha;
		result.floor_raise_applied = true;
	}

	if (input.peak <= kQuietAudiblePeakThreshold)
	{
		result.quiet_gain_floor_after +=
			(1.0f - result.quiet_gain_floor_after) * kQuietFloorSilenceDecayAlpha;
		result.silence_decay_applied = true;
	}
	else if (input.post_gain_rms > kQuietFloorReleaseRmsThreshold)
	{
		result.quiet_gain_floor_after +=
			(1.0f - result.quiet_gain_floor_after) * kQuietFloorReleaseAlpha;
		result.release_decay_applied = true;
	}

	if (result.quiet_gain_floor_after < 1.0001f)
	{
		result.quiet_gain_floor_after = 1.0f;
	}

	return result;
}
}
}
