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
#include "u_DfxDsp.h"
#include "codedefs.h"
#include "DfxSdk.h"
#include "dfxp.h"
#include "prelst.h"
#include "file.h"
#include "qnt.h"
#include <string>

#include "BinauralSyn.h"
#include "ptutil\dfxp\u_dfxp.h"
#include "com.h"
#include "dfxSharedUtil.h"
#include "GraphicEq.h"
#include "spectrum.h"
#include "SurroundSyn.h"
#include <cstdio>
#include <sstream>

#define DFXG_REGISTRY_DFX_PRODUCT_NAME_WIDE		L"DFX"
#define DFXG_DISPLAYED_DFX_PRODUCT_NAME_WIDE    L"FxSound"
#define DFXP_VENDOR_CODE_UNIVERSAL			  23

#define DFXG_MIN_USER_PRESET_INDEX				 99 /* 0 based number of min user preset (preset number 100) */
#define DFXG_MAX_PRESET_NAME_LENGTH              128
#define DFXG_NO_PROCESSING_PRESET                0
#define DFXG_DEFAULT_PRESET_INDEX                0 /* 0 based, i.e. index of 2 corresponds to preset number 3 */
#define DFXG_FREE_PRESET_MIN_INDEX               0 
#define DFXG_FREE_PRESET_MAX_INDEX               0


#define MIDI_MIN_VALUE          0
#define MIDI_MAX_VALUE          127

namespace
{
std::wstring getDiagnosticLogPath(const wchar_t* file_name)
{
	wchar_t appdata_path[MAX_PATH] = {};
	auto length = GetEnvironmentVariableW(L"APPDATA", appdata_path, MAX_PATH);
	if (length == 0 || length >= MAX_PATH)
	{
		return L"";
	}

	std::wstring directory = std::wstring(appdata_path) + L"\\FxSound";
	CreateDirectoryW(directory.c_str(), NULL);

	return directory + L"\\" + file_name;
}

std::wstring getLocalTimestamp()
{
	SYSTEMTIME system_time = {};
	GetLocalTime(&system_time);

	wchar_t timestamp[64] = {};
	swprintf(
		timestamp,
		sizeof(timestamp) / sizeof(timestamp[0]),
		L"%04d-%02d-%02d %02d:%02d:%02d.%03d",
		system_time.wYear,
		system_time.wMonth,
		system_time.wDay,
		system_time.wHour,
		system_time.wMinute,
		system_time.wSecond,
		system_time.wMilliseconds);

	return timestamp;
}

void appendDiagnosticLog(const std::wstring& path, const std::wstring& line)
{
	if (path.empty())
	{
		return;
	}

	FILE* stream = nullptr;
	if (_wfopen_s(&stream, path.c_str(), L"a+, ccs=UTF-8") != 0 || stream == nullptr)
	{
		return;
	}

	fwprintf(stream, L"%ls\r\n", line.c_str());
	fclose(stream);
}

void appendEqDiagnosticLog(const std::wstring& line)
{
	appendDiagnosticLog(getDiagnosticLogPath(L"fxsound-dsp-eq.log"), line);
}
}

DfxDspPrivate::DfxDspPrivate()
{
	midi_to_rval_qnt_handle_ = NULL;
	rval_to_midi_qnt_handle_ = NULL;

	swprintf(product_specific_.wcp_registry_product_name, PT_MAX_GENERIC_STRLEN, L"%s", DFXG_REGISTRY_DFX_PRODUCT_NAME_WIDE);
	swprintf(product_specific_.wcp_displayed_product_name, PT_MAX_GENERIC_STRLEN, L"%s", DFXG_DISPLAYED_DFX_PRODUCT_NAME_WIDE);
	product_specific_.full_version = static_cast<float>(13.028);
	product_specific_.major_version = static_cast<int>(13.028);
	vendor_specific_.vendor_code = DFXP_VENDOR_CODE_UNIVERSAL;


	// Initialize the processing mode
	if (dfxpInit(&dfxp_handle_,
		L"DFX", 23,
		14, 1, IS_FALSE,
		IS_FALSE, 0,
		IS_FALSE,
		IS_FALSE,
		IS_FALSE, slout1_) != OKAY)
	{
		//return(NOT_OKAY);
		MessageBox(NULL, L"TTEST", L"TEST", MB_OK);
	}

	// Make sure the vocal reduction is turned off 
	if (dfxpSetButtonValue(dfxp_handle_, DFX_UI_BUTTON_VOCAL_REDUCTION_ON, IS_FALSE) != OKAY)
	{
		//return(NOT_OKAY);
	}

	// From dfxg_InitStaticQnts() in dfxgQnt.cpp
	/*
	* Initalize the qnt handle for calculating the real value based
	* on a MIDI value.
	*/
	if (qntIToRInit(&(midi_to_rval_qnt_handle_), slout1_,
		MIDI_MIN_VALUE,
		MIDI_MAX_VALUE,
		DFX_UI_MIN_VALUE, DFX_UI_MAX_VALUE,
		IS_FALSE, 0,
		IS_FALSE, (realtype)0.0,
		IS_FALSE, QNT_RESPONSE_LINEAR) != OKAY)
	{
	}

	/*
	* Initialize the qnt handle for calculating the midi value
	* based on the real value.
	*/
	if (qntRToIInit(&(rval_to_midi_qnt_handle_), slout1_,
		DFX_UI_MIN_VALUE, DFX_UI_MAX_VALUE,
		MIDI_MIN_VALUE,
		MIDI_MAX_VALUE,
		IS_FALSE, 0,
		IS_FALSE) != OKAY)
	{
	}

	//return(OKAY);
}


DfxDspPrivate::~DfxDspPrivate()
{
	// So the thread/timer will not attempt to use this object while it's being destroyed.
	being_destroyed_ = true;
	
	// Free the prelst
	if (preset_list_handle_ != NULL)
	{
		if (prelstFreeUp(&(preset_list_handle_)) != OKAY)
		{
		}
	}
	// Free the midi to rval and visa versa qnt handles
	if (midi_to_rval_qnt_handle_ != NULL)
	{
		if (qntFreeUp(&(midi_to_rval_qnt_handle_)) != OKAY)
		{
		}
	}
	if (rval_to_midi_qnt_handle_ != NULL)
	{
		if (qntFreeUp(&(rval_to_midi_qnt_handle_)) != OKAY)
		{
		}
	}

	// Free dfxp handle
	dfxpFreeAll();
	free(dfxp_handle_);
	//*dfxp_handle_ = NULL;

	// Free the slout handle
	if (slout1_ != NULL)
		delete slout1_;
}

/**
dfxg_UpdateFromRegistryAllSettings()
**/
void DfxDspPrivate::processTimer()
{
	bool anything_changed = false;
	int i_eq_changed = IS_FALSE;

	if (update_from_registry_)
	{
		eqUpdateFromRegistry(&i_eq_changed);
		update_from_registry_ = false;
	}

	if (i_eq_changed)
	{
		anything_changed = true;
	}

	/* If any settings have been changed, communicate all the changes to the DSP module */
	// NOTE: I find that without this if condition and call dfxpCOmmunicateAll() repeatedly will mess up the audio.
	if (anything_changed)
	{
		logEqFlatTransition(L"processTimer");
		dfxpCommunicateAll(dfxp_handle_);
	}
}

int DfxDspPrivate::processAudio(short int *si_input_samples, short int *si_output_samples, int i_num_sample_sets, int i_check_for_duplicate_buffers)
{
	processTimer();
	// Apply DFX processing here using data and format vars above. Format will always be 32 bit floating point.
	if (dfxpUniversalModifySamples(dfxp_handle_, si_input_samples, si_output_samples, i_num_sample_sets, i_check_for_duplicate_buffers) != OKAY)
		return(NOT_OKAY);

	return OKAY;
}

int DfxDspPrivate::setSignalFormat(int i_bps, int i_nch, int i_srate, int i_valid_bits)
{
	if (dfxpUniversalSetSignalFormat(dfxp_handle_, i_bps, i_nch, i_srate, i_valid_bits) != OKAY)
		return(NOT_OKAY);

	std::wstringstream context;
	context << L"setSignalFormat bps=" << i_bps
		    << L" nch=" << i_nch
		    << L" srate=" << i_srate
		    << L" valid_bits=" << i_valid_bits;
	logEqFlatTransition(context.str());

	return OKAY;
}

bool DfxDspPrivate::isEqFlat()
{
	auto num_bands = getNumEqBands();
	if (num_bands <= 0)
	{
		return false;
	}

	for (int band = 0; band < num_bands; ++band)
	{
		if (fabsf(getEqBandBoostCut(band)) > 0.0001f)
		{
			return false;
		}
	}

	return true;
}

void DfxDspPrivate::logEqFlatTransition(const std::wstring& context)
{
	auto eq_is_flat = isEqFlat();
	if (!eq_is_flat)
	{
		eq_was_flat_ = false;
		return;
	}

	if (eq_was_flat_)
	{
		return;
	}

	eq_was_flat_ = true;

	int eq_on_memory = 0;
	int eq_on_registry = 0;
	realtype sampling_freq = 0.0;

	eqGetProcessingOn(DFXP_STORAGE_TYPE_MEMORY, &eq_on_memory);
	eqGetProcessingOn(DFXP_STORAGE_TYPE_REGISTRY, &eq_on_registry);
	dfxpGetSamplingFreq(dfxp_handle_, &sampling_freq);

	std::wstringstream frequencies;
	auto num_bands = getNumEqBands();
	for (int band = 0; band < num_bands; ++band)
	{
		if (band > 0)
		{
			frequencies << L",";
		}

		frequencies << getEqBandFrequency(band);
	}

	std::wstringstream message;
	message << getLocalTimestamp()
		    << L" EQ flat detected context=\"" << context << L"\""
		    << L" bands=" << num_bands
		    << L" eq_on_memory=" << eq_on_memory
		    << L" eq_on_registry=" << eq_on_registry
		    << L" sampling_freq=" << sampling_freq
		    << L" frequencies=[" << frequencies.str() << L"]";

	appendEqDiagnosticLog(message.str());
	OutputDebugStringW((message.str() + L"\n").c_str());
}


void DfxDspPrivate::powerOn(bool on)
{
	if (on)
	{
		if (dfxpSetButtonValue(dfxp_handle_, DFX_UI_BUTTON_BYPASS, 0) != OKAY)
		{
		}
	}
	else
	{
		if (dfxpSetButtonValue(dfxp_handle_, DFX_UI_BUTTON_BYPASS, 1) != OKAY)
		{
		}
	}
}

bool DfxDspPrivate::isPowerOn()
{
	int value;
	
	dfxpGetButtonValue(dfxp_handle_, DFX_UI_BUTTON_BYPASS, &value);
	if (value != 0)
	{
		return true;
	}
	else
	{
		return false;
	}
}

float DfxDspPrivate::getEffectValue(DfxDsp::Effect effect)
{
	switch (effect)
	{
	case DfxDsp::Effect::Fidelity:
		return fidelity_.value;

	case DfxDsp::Effect::Ambience:
		return ambience_.value;

	case DfxDsp::Effect::Surround:
		return surround_.value;

	case DfxDsp::Effect::DynamicBoost:
		return dynamic_boost_.value;

	case DfxDsp::Effect::Bass:
		return bass_boost_.value;
	}

	return -1.0f;
}

void DfxDspPrivate::setEffectValue(DfxDsp::Effect effect, float value)
{
	int button;
	int knob;

	switch (effect)
	{
	case DfxDsp::Effect::Fidelity:
		button = DFX_UI_BUTTON_FIDELITY;
		knob = DFX_UI_KNOB_FIDELITY;
		fidelity_.value = (realtype)value / (realtype)10.0;
		break;

	case DfxDsp::Effect::Ambience:
		button = DFX_UI_BUTTON_AMBIENCE;
		knob = DFX_UI_KNOB_AMBIENCE;
		ambience_.value = (realtype)value / (realtype)10.0;
		break;

	case DfxDsp::Effect::Surround:
		button = DFX_UI_BUTTON_SURROUND;
		knob = DFX_UI_KNOB_SURROUND;
		surround_.value = (realtype)value / (realtype)10.0;
		break;

	case DfxDsp::Effect::DynamicBoost:
		button = DFX_UI_BUTTON_DYNAMIC_BOOST;
		knob = DFX_UI_KNOB_DYNAMIC_BOOST;
		dynamic_boost_.value = (realtype)value / (realtype)10.0;
		break;

	case DfxDsp::Effect::Bass:
		button = DFX_UI_BUTTON_BASS_BOOST;
		knob = DFX_UI_KNOB_BASS_BOOST;
		bass_boost_.value = (realtype)value / (realtype)10.0;
		break;

	default:
		return;
	}

	if (value != 0.0)
	{
		dfxpSetButtonValue(dfxp_handle_, button, 1);
	}
	else
	{
		dfxpSetButtonValue(dfxp_handle_, button, 0);
	}

	dfxpSetKnobValue(dfxp_handle_, knob, (realtype)value / (realtype)10.0, false);
}

unsigned long DfxDspPrivate::getTotalAudioProcessedTime()
{
	unsigned long value;

	dfxpGetTotalAudioProcessedTime(dfxp_handle_, &value);

	return value;
}

void DfxDspPrivate::resetTotalAudioProcessedTime()
{
	dfxpSetTotalAudioProcessedTime(dfxp_handle_, 0);
}

/*
 * 
 */
int DfxDspPrivate::dfxpFreeAll()
{
	struct dfxpHdlType *cast_handle;

	cast_handle = (struct dfxpHdlType *)(dfxp_handle_);

	if (cast_handle == NULL)
		return(OKAY);

	/* Free all the midi_to_dsp qnt handles */
	if (cast_handle->midi_to_dsp.fidelity_qnt_hdl != NULL)
	{
		if (qntFreeUp(&(cast_handle->midi_to_dsp.fidelity_qnt_hdl)) != OKAY)
			return(NOT_OKAY);
	}

	if (cast_handle->midi_to_dsp.spaciousness_qnt_hdl != NULL)
	{
		if (qntFreeUp(&(cast_handle->midi_to_dsp.spaciousness_qnt_hdl)) != OKAY)
			return(NOT_OKAY);
	}

	if (cast_handle->midi_to_dsp.ambience_qnt_hdl != NULL)
	{
		if (qntFreeUp(&(cast_handle->midi_to_dsp.ambience_qnt_hdl)) != OKAY)
			return(NOT_OKAY);
	}

	if (cast_handle->midi_to_dsp.dynamic_boost_qnt_hdl != NULL)
	{
		if (qntFreeUp(&(cast_handle->midi_to_dsp.dynamic_boost_qnt_hdl)) != OKAY)
			return(NOT_OKAY);
	}

	if (cast_handle->midi_to_dsp.bass_boost_qnt_hdl != NULL)
	{
		if (qntFreeUp(&(cast_handle->midi_to_dsp.bass_boost_qnt_hdl)) != OKAY)
			return(NOT_OKAY);
	}

	/* Fixed Aural Activation Specific */
	if (cast_handle->midi_to_dsp.aural_filter_gain_qnt_hdl != NULL)
	{
		if (qntFreeUp(&(cast_handle->midi_to_dsp.aural_filter_gain_qnt_hdl)) != OKAY)
			return(NOT_OKAY);
	}

	if (cast_handle->midi_to_dsp.aural_filter_a1_qnt_hdl != NULL)
	{
		if (qntFreeUp(&(cast_handle->midi_to_dsp.aural_filter_a1_qnt_hdl)) != OKAY)
			return(NOT_OKAY);
	}

	if (cast_handle->midi_to_dsp.aural_filter_a0_qnt_hdl != NULL)
	{
		if (qntFreeUp(&(cast_handle->midi_to_dsp.aural_filter_a0_qnt_hdl)) != OKAY)
			return(NOT_OKAY);
	}

	/* Fixed Reverb Specific */
	if (cast_handle->midi_to_dsp.room_size_qnt_hdl != NULL)
	{
		if (qntFreeUp(&(cast_handle->midi_to_dsp.room_size_qnt_hdl)) != OKAY)
			return(NOT_OKAY);
	}

	if (cast_handle->midi_to_dsp.damping_bandwidth_qnt_hdl != NULL)
	{
		if (qntFreeUp(&(cast_handle->midi_to_dsp.damping_bandwidth_qnt_hdl)) != OKAY)
			return(NOT_OKAY);
	}

	if (cast_handle->midi_to_dsp.rolloff_bandwidth_qnt_hdl != NULL)
	{
		if (qntFreeUp(&(cast_handle->midi_to_dsp.rolloff_bandwidth_qnt_hdl)) != OKAY)
			return(NOT_OKAY);
	}

	if (cast_handle->midi_to_dsp.motion_rate_qnt_hdl != NULL)
	{
		if (qntFreeUp(&(cast_handle->midi_to_dsp.motion_rate_qnt_hdl)) != OKAY)
			return(NOT_OKAY);
	}

	if (cast_handle->midi_to_dsp.motion_depth_qnt_hdl != NULL)
	{
		if (qntFreeUp(&(cast_handle->midi_to_dsp.motion_depth_qnt_hdl)) != OKAY)
			return(NOT_OKAY);
	}

	if (cast_handle->midi_to_dsp.screen_lex_main_knob3_qnt_hdl != NULL)
	{
		if (qntFreeUp(&(cast_handle->midi_to_dsp.screen_lex_main_knob3_qnt_hdl)) != OKAY)
			return(NOT_OKAY);
	}

	if (cast_handle->midi_to_dsp.screen_lex_main_knob4_qnt_hdl != NULL)
	{
		if (qntFreeUp(&(cast_handle->midi_to_dsp.screen_lex_main_knob4_qnt_hdl)) != OKAY)
			return(NOT_OKAY);
	}

	/* Fixed Optimizer Specific */
	if (cast_handle->midi_to_dsp.release_time_beta_qnt_hdl != NULL)
	{
		if (qntFreeUp(&(cast_handle->midi_to_dsp.release_time_beta_qnt_hdl)) != OKAY)
			return(NOT_OKAY);
	}

	if (cast_handle->midi_to_dsp.screen_opt_main_knob3_qnt_hdl != NULL)
	{
		if (qntFreeUp(&(cast_handle->midi_to_dsp.screen_opt_main_knob3_qnt_hdl)) != OKAY)
			return(NOT_OKAY);
	}

	/* Fixed Widener Specific */
	if (cast_handle->midi_to_dsp.dispersion_delay_qnt_hdl != NULL)
	{
		if (qntFreeUp(&(cast_handle->midi_to_dsp.dispersion_delay_qnt_hdl)) != OKAY)
			return(NOT_OKAY);
	}

	if (cast_handle->midi_to_dsp.wid_filter_gain_qnt_hdl != NULL)
	{
		if (qntFreeUp(&(cast_handle->midi_to_dsp.wid_filter_gain_qnt_hdl)) != OKAY)
			return(NOT_OKAY);
	}

	if (cast_handle->midi_to_dsp.wid_filter_a1_qnt_hdl != NULL)
	{
		if (qntFreeUp(&(cast_handle->midi_to_dsp.wid_filter_a1_qnt_hdl)) != OKAY)
			return(NOT_OKAY);
	}

	if (cast_handle->midi_to_dsp.wid_filter_a0_qnt_hdl != NULL)
	{
		if (qntFreeUp(&(cast_handle->midi_to_dsp.wid_filter_a0_qnt_hdl)) != OKAY)
			return(NOT_OKAY);
	}

	/* Delay specific */
	if (cast_handle->midi_to_dsp.dly_qnt_hdl != NULL)
	{
		if (qntFreeUp(&(cast_handle->midi_to_dsp.dly_qnt_hdl)) != OKAY)
			return(NOT_OKAY);
	}

	/* Free all the other qnt handles */
	if (cast_handle->real_to_midi_qnt_hdl != NULL)
	{
		if (qntFreeUp(&(cast_handle->real_to_midi_qnt_hdl)) != OKAY)
			return(NOT_OKAY);
	}

	if (cast_handle->midi_to_real_qnt_hdl != NULL)
	{
		if (qntFreeUp(&(cast_handle->midi_to_real_qnt_hdl)) != OKAY)
			return(NOT_OKAY);
	}

	/* Free the com handles */
	if (cast_handle->com_hdl_front != NULL)
	{
		if (comFreeUp(&(cast_handle->com_hdl_front)) != OKAY)
			return(NOT_OKAY);
	}

	if (cast_handle->com_hdl_rear != NULL)
	{
		if (comFreeUp(&(cast_handle->com_hdl_rear)) != OKAY)
			return(NOT_OKAY);
	}

	if (cast_handle->com_hdl_side != NULL)
	{
		if (comFreeUp(&(cast_handle->com_hdl_side)) != OKAY)
			return(NOT_OKAY);
	}

	if (cast_handle->com_hdl_center != NULL)
	{
		if (comFreeUp(&(cast_handle->com_hdl_center)) != OKAY)
			return(NOT_OKAY);
	}

	if (cast_handle->com_hdl_subwoofer != NULL)
	{
		if (comFreeUp(&(cast_handle->com_hdl_subwoofer)) != OKAY)
			return(NOT_OKAY);
	}

	// Free EQ handle
	if (cast_handle->eq.graphicEq_hdl != NULL)
	{
		if (GraphicEqFreeUp(&(cast_handle->eq.graphicEq_hdl)) != OKAY)
			return(NOT_OKAY);
	}

	// Free SurroundSyn handle
	if (cast_handle->SurroundSyn_hdl != NULL)
	{
		if (SurroundSynFreeUp(&(cast_handle->SurroundSyn_hdl)) != OKAY)
			return(NOT_OKAY);
	}

	/* Free the spectrum handle */
	if (cast_handle->spectrum.spectrum_hdl != NULL)
	{
		if (spectrumFreeUp(&(cast_handle->spectrum.spectrum_hdl)) != OKAY)
			return(NOT_OKAY);
	}

	/* Free the binauralSyn handle */
	if (cast_handle->BinauralSyn_hdl != NULL)
	{
		if (BinauralSynFreeUp(&(cast_handle->BinauralSyn_hdl)) != OKAY)
			return(NOT_OKAY);
	}

	/* Free shared memory library */
	if (cast_handle->hp_sharedUtil != NULL)
	{
		if (dfxSharedUtilFreeUp(&(cast_handle->hp_sharedUtil)) != OKAY)
			return(NOT_OKAY);
	}

	return(OKAY);
}

void DfxDspPrivate::getSpectrumBandValues(float* rp_band_values, int i_array_size)
{
    dfxpSpectrumGetBandValues(dfxp_handle_, rp_band_values, i_array_size);
}

