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
/*
 * FILE: u_GraphicEq.h
 * DATE: 12/19/2002
 * AUTHOR: Mark Kaplan	
 * DESCRIPTION:
 *
 * Local header file for the GraphicEq module
 */
#ifndef _U_GRAPHIC_EQ_H_
#define _U_GRAPHIC_EQ_H_

#include "GraphicEq.h"
#include "sos.h"

#define GRAPHIC_EQ_MAX_NUM_BANDS SOS_MAX_NUM_SOS_SECTIONS
#define GRAPHIC_EQ_AUTO_EQ_NUM_BUCKETS 600

/* GraphicEq Handle definition */
struct GraphicEqHdlType
{
	CSlout *slout_hdl;
	wchar_t wcp_msg1[1024]; /* String for messages */
	int i_trace_mode;

	/* Main EQ settings */
	int num_bands;

	/* Frequencies of the highest and lowest bands in hz. */
	realtype min_band_freq;
	realtype max_band_freq;

	/* Filter Q setting */
	realtype Q;
	realtype Q_multiplier;

	realtype master_gain;
	realtype normalization_gain;
	realtype volume_leveling_gain_db;
	realtype balance;

	/* Current sampling frequency */
	realtype sampling_freq;

	/* SOS sections and parameters for each band */
	PT_HANDLE *sos_hdl;

	/* Adaptive EQ state (60 s analysis, 10 s updates). */
	int auto_eq_enabled;
	realtype auto_eq_range_db;
	int auto_eq_bucket_index;
	int auto_eq_bucket_count;
	int auto_eq_buckets_since_update;
	realtype auto_eq_samples_per_bucket;
	realtype auto_eq_samples_in_bucket;
	realtype auto_eq_bucket_low_energy;
	realtype auto_eq_bucket_mid_energy;
	realtype auto_eq_bucket_high_energy;
	realtype auto_eq_low_energy_buckets[GRAPHIC_EQ_AUTO_EQ_NUM_BUCKETS];
	realtype auto_eq_mid_energy_buckets[GRAPHIC_EQ_AUTO_EQ_NUM_BUCKETS];
	realtype auto_eq_high_energy_buckets[GRAPHIC_EQ_AUTO_EQ_NUM_BUCKETS];
	realtype auto_eq_low_energy_sum;
	realtype auto_eq_mid_energy_sum;
	realtype auto_eq_high_energy_sum;
	realtype auto_eq_lp_low_state[8];
	realtype auto_eq_lp_mid_state[8];
	realtype auto_eq_user_base_boost[GRAPHIC_EQ_MAX_NUM_BANDS];
	realtype auto_eq_dynamic_offset[GRAPHIC_EQ_MAX_NUM_BANDS];

};

/* Local Functions */

/* GraphicEqInitSections.cpp */
int GraphicEq_InitSections(PT_HANDLE *);

#endif /* _U_GRAPHIC_EQ_H_ */
