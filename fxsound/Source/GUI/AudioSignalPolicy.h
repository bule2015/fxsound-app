/*
FxSound
Copyright (C) 2025  FxSound LLC

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU Affero General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Affero General Public License for more details.

You should have received a copy of the GNU Affero General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#pragma once

#include <cstdint>

namespace FxSound
{
namespace AudioSignalPolicy
{
	static constexpr float kSignalThresholdDb = -100.0f;
	static constexpr int64_t kSignalRecentWindowMs = 500;
	static constexpr int kSignalEnableTicks = 1;
	static constexpr int kSignalDisableTicks = 5;
	static constexpr int64_t kStallCaptureWindowMs = 1000;
	static constexpr int64_t kPlaybackStalledWindowMs = 1500;

	inline bool isCaptureSignalPresent(
		int64_t now_ms,
		int64_t last_capture_tick_ms,
		float capture_input_rms_db)
	{
		return last_capture_tick_ms > 0 &&
			now_ms >= last_capture_tick_ms &&
			(now_ms - last_capture_tick_ms) <= kSignalRecentWindowMs &&
			capture_input_rms_db > kSignalThresholdDb;
	}

	struct CounterState
	{
		int signal_present_after = 0;
		int signal_absent_after = 0;
	};

	inline CounterState advanceSignalCounters(
		bool signal_present,
		bool grace_active,
		int signal_present_before,
		int signal_absent_before)
	{
		if (grace_active)
		{
			return {};
		}

		if (signal_present)
		{
			return { signal_present_before + 1, 0 };
		}

		return { 0, signal_absent_before + 1 };
	}

	inline bool shouldEnableDsp(int signal_present_counter, bool dsp_active)
	{
		return signal_present_counter >= kSignalEnableTicks && !dsp_active;
	}

	inline bool shouldDisableDsp(int signal_absent_counter, bool dsp_active)
	{
		return signal_absent_counter >= kSignalDisableTicks && dsp_active;
	}

	inline bool shouldDetectPlaybackStall(
		int process_timer_result,
		bool playback_available,
		int64_t now_ms,
		int64_t last_capture_tick_ms,
		int64_t last_playback_tick_ms,
		bool capture_signal_present)
	{
		const auto capture_recent = last_capture_tick_ms > 0 &&
			now_ms >= last_capture_tick_ms &&
			(now_ms - last_capture_tick_ms) <= kStallCaptureWindowMs;
		const auto playback_stalled = last_playback_tick_ms <= 0 ||
			now_ms < last_playback_tick_ms ||
			(now_ms - last_playback_tick_ms) >= kPlaybackStalledWindowMs;

		return process_timer_result == 0 &&
			playback_available &&
			capture_recent &&
			playback_stalled &&
			capture_signal_present;
	}
}
}
