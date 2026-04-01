/*
FxSound
Copyright (C) 2025  FxSound LLC
Contributors:
	www.theremino.com (2025)

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
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include "codedefs.h"
#include "sos.h"
#include "GraphicEq.h"
#include "u_GraphicEq.h"

namespace
{
	constexpr realtype kAutoEqBucketSeconds = 0.1f;
	constexpr int kAutoEqBucketsPerUpdate = 100; // 10 seconds
	constexpr realtype kAutoEqLowCutHz = 250.0f;
	constexpr realtype kAutoEqMidCutHz = 4000.0f;
	constexpr realtype kAutoEqBandStepDb = 0.30f;
	constexpr realtype kAutoEqStrength = 0.70f;
	constexpr realtype kAutoEqMidStrength = 0.40f;
	constexpr realtype kAutoEqEpsilon = 1e-12f;
	constexpr realtype kPi = 3.14159265358979323846f;

	struct AdaptiveEqTargetOffsets
	{
		realtype low;
		realtype mid;
		realtype high;
	};

	static realtype clampReal(realtype value, realtype min_value, realtype max_value)
	{
		return (value < min_value) ? min_value : ((value > max_value) ? max_value : value);
	}

	static realtype stepToward(realtype current, realtype target, realtype step)
	{
		if (target > current + step)
			return current + step;
		if (target < current - step)
			return current - step;
		return target;
	}

	static AdaptiveEqTargetOffsets computeAdaptiveEqTargetOffsets(const struct GraphicEqHdlType* cast_handle)
	{
		realtype avg_low = cast_handle->auto_eq_low_energy_sum / cast_handle->auto_eq_bucket_count;
		realtype avg_mid = cast_handle->auto_eq_mid_energy_sum / cast_handle->auto_eq_bucket_count;
		realtype avg_high = cast_handle->auto_eq_high_energy_sum / cast_handle->auto_eq_bucket_count;
		realtype avg_ref = (avg_low + avg_mid + avg_high) / 3.0f;

		realtype low_delta_db = (realtype)(10.0f * log10((double)((avg_low + kAutoEqEpsilon) / (avg_ref + kAutoEqEpsilon))));
		realtype mid_delta_db = (realtype)(10.0f * log10((double)((avg_mid + kAutoEqEpsilon) / (avg_ref + kAutoEqEpsilon))));
		realtype high_delta_db = (realtype)(10.0f * log10((double)((avg_high + kAutoEqEpsilon) / (avg_ref + kAutoEqEpsilon))));

		realtype band_limit_db = cast_handle->auto_eq_range_db;
		AdaptiveEqTargetOffsets targets{};
		targets.low = clampReal(-low_delta_db * kAutoEqStrength, -band_limit_db, band_limit_db);
		targets.mid = clampReal(-mid_delta_db * kAutoEqMidStrength, -band_limit_db, band_limit_db);
		targets.high = clampReal(-high_delta_db * kAutoEqStrength, -band_limit_db, band_limit_db);

		return targets;
	}

	static realtype getAdaptiveEqTargetOffset(realtype center_freq, const AdaptiveEqTargetOffsets& targets)
	{
		if (center_freq <= kAutoEqLowCutHz)
			return targets.low;

		if (center_freq >= kAutoEqMidCutHz)
			return targets.high;

		realtype t = (center_freq - kAutoEqLowCutHz) / (kAutoEqMidCutHz - kAutoEqLowCutHz);
		realtype low_to_mid = targets.low + (targets.mid - targets.low) * t;
		realtype mid_to_high = targets.mid + (targets.high - targets.mid) * t;
		return (low_to_mid + mid_to_high) * 0.5f;
	}

	static void updateAdaptiveEqBands(struct GraphicEqHdlType* cast_handle)
	{
		if (cast_handle->auto_eq_bucket_count <= 0)
			return;

		AdaptiveEqTargetOffsets targets = computeAdaptiveEqTargetOffsets(cast_handle);
		realtype* rp_freq_array = NULL;
		realtype* rp_boost_array = NULL;
		if (sosGetCenterFreqArray((PT_HANDLE*)(cast_handle->sos_hdl), &rp_freq_array) != OKAY)
			return;
		if (sosGetCenterFreqResponseArray((PT_HANDLE*)(cast_handle->sos_hdl), &rp_boost_array) != OKAY)
			return;

		for (int i = 0; i < cast_handle->num_bands; i++)
		{
			realtype center_freq = rp_freq_array[i];
			realtype target_offset = getAdaptiveEqTargetOffset(center_freq, targets);

			realtype user_base = rp_boost_array[i] - cast_handle->auto_eq_dynamic_offset[i];
			cast_handle->auto_eq_user_base_boost[i] = user_base;
			realtype new_offset = stepToward(cast_handle->auto_eq_dynamic_offset[i], target_offset, kAutoEqBandStepDb);
			cast_handle->auto_eq_dynamic_offset[i] = new_offset;
			GraphicEqSetBandBoostCut((PT_HANDLE*)cast_handle, i + 1, user_base + new_offset);
		}
	}

	static void commitAdaptiveEqBucket(struct GraphicEqHdlType* cast_handle)
	{
		int write_index = cast_handle->auto_eq_bucket_index;
		if (cast_handle->auto_eq_bucket_count == GRAPHIC_EQ_AUTO_EQ_NUM_BUCKETS)
		{
			cast_handle->auto_eq_low_energy_sum -= cast_handle->auto_eq_low_energy_buckets[write_index];
			cast_handle->auto_eq_mid_energy_sum -= cast_handle->auto_eq_mid_energy_buckets[write_index];
			cast_handle->auto_eq_high_energy_sum -= cast_handle->auto_eq_high_energy_buckets[write_index];
		}
		else
		{
			cast_handle->auto_eq_bucket_count++;
		}

		cast_handle->auto_eq_low_energy_buckets[write_index] = cast_handle->auto_eq_bucket_low_energy;
		cast_handle->auto_eq_mid_energy_buckets[write_index] = cast_handle->auto_eq_bucket_mid_energy;
		cast_handle->auto_eq_high_energy_buckets[write_index] = cast_handle->auto_eq_bucket_high_energy;
		cast_handle->auto_eq_low_energy_sum += cast_handle->auto_eq_bucket_low_energy;
		cast_handle->auto_eq_mid_energy_sum += cast_handle->auto_eq_bucket_mid_energy;
		cast_handle->auto_eq_high_energy_sum += cast_handle->auto_eq_bucket_high_energy;

		cast_handle->auto_eq_bucket_index = (write_index + 1) % GRAPHIC_EQ_AUTO_EQ_NUM_BUCKETS;
		cast_handle->auto_eq_buckets_since_update++;
		cast_handle->auto_eq_samples_in_bucket = 0.0f;
		cast_handle->auto_eq_bucket_low_energy = 0.0f;
		cast_handle->auto_eq_bucket_mid_energy = 0.0f;
		cast_handle->auto_eq_bucket_high_energy = 0.0f;

		if (cast_handle->auto_eq_buckets_since_update >= kAutoEqBucketsPerUpdate)
		{
			cast_handle->auto_eq_buckets_since_update = 0;
			updateAdaptiveEqBands(cast_handle);
		}
	}

	static void analyzeAndUpdateAdaptiveEq(struct GraphicEqHdlType* cast_handle,
		realtype* rp_signal_in,
		int i_num_sample_sets,
		int i_num_channels,
		realtype r_samp_freq)
	{
		if (!cast_handle->auto_eq_enabled || i_num_sample_sets <= 0 || i_num_channels <= 0 || i_num_channels > 8)
			return;

		realtype sample_rate = (r_samp_freq > 1000.0f) ? r_samp_freq : 48000.0f;
		cast_handle->auto_eq_samples_per_bucket = sample_rate * kAutoEqBucketSeconds;

		realtype dt = 1.0f / sample_rate;
		realtype low_alpha = dt / ((1.0f / (2.0f * kPi * kAutoEqLowCutHz)) + dt);
		realtype mid_alpha = dt / ((1.0f / (2.0f * kPi * kAutoEqMidCutHz)) + dt);

		int index = 0;
		for (int sample = 0; sample < i_num_sample_sets; sample++)
		{
			realtype low_energy = 0.0f;
			realtype mid_energy = 0.0f;
			realtype high_energy = 0.0f;
			int analyzed_channels = 0;

			for (int channel = 0; channel < i_num_channels; channel++)
			{
				realtype x = rp_signal_in[index + channel];
				realtype low_lp = cast_handle->auto_eq_lp_low_state[channel] + low_alpha * (x - cast_handle->auto_eq_lp_low_state[channel]);
				realtype mid_lp = cast_handle->auto_eq_lp_mid_state[channel] + mid_alpha * (x - cast_handle->auto_eq_lp_mid_state[channel]);
				cast_handle->auto_eq_lp_low_state[channel] = low_lp;
				cast_handle->auto_eq_lp_mid_state[channel] = mid_lp;

				realtype low_band = low_lp;
				realtype mid_band = mid_lp - low_lp;
				realtype high_band = x - mid_lp;

				low_energy += low_band * low_band;
				mid_energy += mid_band * mid_band;
				high_energy += high_band * high_band;
				analyzed_channels++;
			}

			if (analyzed_channels > 0)
			{
				realtype inv_channels = 1.0f / analyzed_channels;
				cast_handle->auto_eq_bucket_low_energy += low_energy * inv_channels;
				cast_handle->auto_eq_bucket_mid_energy += mid_energy * inv_channels;
				cast_handle->auto_eq_bucket_high_energy += high_energy * inv_channels;
			}

			cast_handle->auto_eq_samples_in_bucket += 1.0f;
			if (cast_handle->auto_eq_samples_in_bucket >= cast_handle->auto_eq_samples_per_bucket)
			{
				commitAdaptiveEqBucket(cast_handle);
			}

			index += i_num_channels;
		}
	}
}

/*
 * FUNCTION: GraphicEqProcess()
 * DESCRIPTION:
 *  Process the passed input signal and set the output signal to have the processed data.
 */
int PT_DECLSPEC GraphicEqProcess(PT_HANDLE *hp_GraphicEq,
							realtype *rp_signal_in,	 /* Input signal, points interleaved */
							realtype *rp_signal_out, /* Array to store the processed signal */
							int i_num_sample_sets,   /* Number of mono sample points or stereo sample pairs */
							int i_num_channels,      /* 1 for mono, 2 for stereo, 6 or 8 for surround */
                     realtype r_samp_freq     /* Sampling frequency in hz. */
							)
{
	struct GraphicEqHdlType *cast_handle;

	cast_handle = (struct GraphicEqHdlType *)(hp_GraphicEq);
 
	if (cast_handle == NULL)
		return(NOT_OKAY);

	/* If the sampling frequency has changed recalc all filter coeffs */
	if( r_samp_freq != cast_handle->sampling_freq )
	{
		cast_handle->sampling_freq = r_samp_freq;
		if( GraphicEqReCalcAllBandCoeffs( hp_GraphicEq ) != OKAY )
			return(NOT_OKAY);
	}

	/* Call processing function */
	analyzeAndUpdateAdaptiveEq(cast_handle, rp_signal_in, i_num_sample_sets, i_num_channels, r_samp_freq);

	/* Call processing function */
	if( i_num_channels <= 2 )
	{
		if( sosProcessBuffer( (PT_HANDLE *)(cast_handle->sos_hdl), rp_signal_in, rp_signal_out, i_num_sample_sets, i_num_channels, r_samp_freq) != OKAY)
			return(NOT_OKAY);
	}
	else if( (i_num_channels == 6) || (i_num_channels == 8) )
	{
		if( sosProcessSurroundBuffer( (PT_HANDLE *)(cast_handle->sos_hdl), rp_signal_in, rp_signal_out, i_num_sample_sets, i_num_channels, r_samp_freq) != OKAY)
			return(NOT_OKAY);
	}
	else
		return(NOT_OKAY);

	return(OKAY);
}
/*
 * FUNCTION: GraphicEqProcess_MasterGainOnly()
 * DESCRIPTION:
 *  Process the passed input signal with MASTER_GAIN only, and set the output signal to have the processed data.
 */
int PT_DECLSPEC GraphicEqProcess_MasterGainOnly(PT_HANDLE* hp_GraphicEq,
												realtype* rp_signal_in,	 /* Input signal, points interleaved */
												realtype* rp_signal_out, /* Array to store the processed signal */
												int i_num_sample_sets,   /* Number of mono sample points or stereo sample pairs */
												int i_num_channels,      /* 1 for mono, 2 for stereo, 6 or 8 for surround */
												realtype r_samp_freq     /* Sampling frequency in hz. */
											   )
{
	struct GraphicEqHdlType* cast_handle;

	cast_handle = (struct GraphicEqHdlType*)(hp_GraphicEq);

	if (cast_handle == NULL)
		return(NOT_OKAY);

	/* If the sampling frequency has changed recalc all filter coeffs */
	if (r_samp_freq != cast_handle->sampling_freq)
	{
		cast_handle->sampling_freq = r_samp_freq;
		if (GraphicEqReCalcAllBandCoeffs(hp_GraphicEq) != OKAY)
			return(NOT_OKAY);
	}

	/* Call processing function */
	if (sosProcessBuffer_MasterGainOnly((PT_HANDLE*)(cast_handle->sos_hdl), rp_signal_in, rp_signal_out, i_num_sample_sets, i_num_channels) != OKAY)
		return(NOT_OKAY);   // SosProcess ERROR

	return(OKAY);
}
