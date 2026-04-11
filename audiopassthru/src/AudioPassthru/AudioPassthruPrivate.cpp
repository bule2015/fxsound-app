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

//#include "stdafx.h"
#include "u_AudioPassthru.h"
#include "sndDevices.h"
#include <cmath>

#define DFXG_SND_SERVER_KILL_THREAD_TIMEOUT_MSECS		  3000
#define DFXG_SND_SERVER_KILL_THREAD_WAIT_PER_LOOP_MSECS   50
#define DFXG_TRUNCATED_DRIVER_TEXT_LENGTH			      50

// See https://stackoverflow.com/questions/6472948/converting-member-function-pointer-to-timerproc
//std::map<UINT_PTR, AudioPassthru*> AudioPassthru::m_AudioPassthruClassMap;  //definition

// According to Paul's note, sndDevice handle must be declared statically.
// He didn't explain fully why (something to do with device event callbacks) 
// but without doing this I was getting access violiation excpeiotn in MMDevApi.dll so 
// there is some truth to it.
sndDevicesHdlType AudioPassthruPrivate::s_sndDevices_;
AudioPassthruCallback* AudioPassthruPrivate::s_callback_ = nullptr;

namespace
{
void notifyDiagnosticMessage(const std::wstring& message)
{
	AudioPassthruPrivate::notifyDiagnostic(message);
}

void resetLatencyMeasurementState(sndDevicesHdlType::LatencyMeasurementState* latency)
{
	if (latency == NULL)
		return;

	latency->captureBatchStartQpc = 0;
	latency->captureBatchQpc100ns = 0;
	latency->lastLogQpc = 0;
	latency->captureToRenderSumMs = 0.0;
	latency->captureToRenderMinMs = 0.0;
	latency->captureToRenderMaxMs = 0.0;
	latency->captureAgeSumMs = 0.0;
	latency->captureAgeMinMs = 0.0;
	latency->captureAgeMaxMs = 0.0;
	latency->playbackQueueSumMs = 0.0;
	latency->playbackQueueMaxMs = 0.0;
	latency->estimatedOutputSumMs = 0.0;
	latency->estimatedOutputMinMs = 0.0;
	latency->estimatedOutputMaxMs = 0.0;
	latency->measurementCount = 0;
}

float calculateBufferRmsDb(const float* buffer, int sample_count)
{
	if ((buffer == nullptr) || (sample_count <= 0))
		return -160.0f;

	double sum_squares = 0.0;
	for (int i = 0; i < sample_count; ++i)
	{
		const double sample = buffer[i];
		sum_squares += sample * sample;
	}

	const double rms = std::sqrt(sum_squares / static_cast<double>(sample_count));
	if (rms <= 1.0e-9)
		return -160.0f;

	return static_cast<float>(20.0 * std::log10(rms));
}
}

AudioPassthruPrivate::AudioPassthruPrivate()
{
	hp_sndDevices_ = (PT_HANDLE *)&(s_sndDevices_);
	//hp_sndDevices_ = hp_sndDevices;
	hProcessingThread_ = NULL;
	ProcessingThreadID_ = (DWORD)0;
	i_kill_processing_thread_ = IS_FALSE;
	device_change_pending_ = false;
	processing_thread_running_ = false;
	mute_ = false;
	swprintf(wcp_playback_device_guid_, PT_MAX_GENERIC_STRLEN, L"");
	b_no_valid_snd_device_dialog_shown_ = false;
	debug_ = IS_TRUE;
	s_sndDevices_.latency.loggingEnabled = FALSE;
	s_sndDevices_.latency.qpcFrequency = 0;
	resetLatencyMeasurementState(&s_sndDevices_.latency);
}

AudioPassthruPrivate::~AudioPassthruPrivate()
{
	int i_result_flag;
	int i_timed_out;

	if (hp_sndDevices_ == NULL)
		return;

	/* If the processing thread is running, kill it */
	const bool thread_shutdown_succeeded = (killProcessingThread(&i_timed_out) == OKAY);
	if (!FxSound::AudioPassthruLifecycle::shouldContinueCleanupAfterThreadShutdown(
		thread_shutdown_succeeded,
		i_timed_out != 0))
		return;

	/* Restore the real default playback device before the virtual path disappears. */
	sndDevicesRestoreDefaultDevice(hp_sndDevices_, &i_result_flag);

	s_callback_ = nullptr;
	s_sndDevices_.deviceChangeCallback = nullptr;
	device_change_pending_ = false;
	processing_thread_running_ = false;

	/*
	* Disable the virtual soundcard
	* NOTE: FOR NOW WE DON'T DO THE DISABLE BECAUSE THIS CAN CAUSE PROBLEMS
	*/
	/*
	if (sndDevicesSetDeviceEnabledStatus(cast_handle->snd_server.hp_sndDevices,
	SND_DEVICES_VIRTUAL_PLAYBACK_DFX, FALSE,  &i_result_flag) != OKAY)
	return(NOT_OKAY);
	*/

	/* Free the data allocated inside the sndDevices_hdl (NOTE: Does not free up structure) */
	if (sndDevicesFree(hp_sndDevices_) != OKAY)
		return;
}

int AudioPassthruPrivate::init(bool enable_output_latency_logging)
{
	int status_flag;

	s_sndDevices_.latency.loggingEnabled = enable_output_latency_logging ? TRUE : FALSE;
	if (!enable_output_latency_logging)
	{
		s_sndDevices_.latency.qpcFrequency = 0;
		resetLatencyMeasurementState(&s_sndDevices_.latency);
	}
	
	/* Initialize the handle */
	if (sndDevicesInit(this->hp_sndDevices_, NULL, SND_DEVICES_INIT_FOR_PROCESSING, debug_, &status_flag) != OKAY)
		return(NOT_OKAY);

	s_sndDevices_.deviceChangeCallback = onDeviceChange;

	if (status_flag != SND_DEVICES_DEVICE_OPERATION_COMPLETED)
	{
		// Its possible that the audio system was in transition and returned a not ready status that caused us to arrive here.
		// The timer will force a call to sndDevicesReInit on first play attempt so there is no need to call it here, this section should only deal with fatal errors.
		if (debug_)
		{
			//swprintf(cast_handle->wcp_msg1, 2048, L"dfxg_SndServerInit(): sndDevicesInit failed, status_flag = %d", status_flag);
			//(cast_handle->slout1)->Message_Wide(FIRST_LINE, cast_handle->wcp_msg1);
		}

		// Fatal errors.
		if (status_flag == SND_DEVICES_MP3_DLL_LOAD_FAILED)
		{
			// We were unable to load the MP3 decoding dll (only applies to MR), in addition to putting trace statement here we may want to add message box to user here.
		}
	}

	/* Initialize code for fixing the case of HDMI monitors being plugged in */
	//if (dfxg_SndServerHdmiFixInit(hp_dfxg) != OKAY)
		//return(NOT_OKAY);

	//pDspProcessingModule_->init();

	return(OKAY);
}

void AudioPassthruPrivate::setDspProcessingModule(DfxDsp* p_dfx_dsp)
{
	p_dfx_dsp_ = p_dfx_dsp;
}

void AudioPassthruPrivate::setDspProcessingEnabled(bool enabled)
{
	dsp_processing_enabled_ = enabled;
}


std::vector<SoundDevice> AudioPassthruPrivate::getSoundDevices(bool active_devices)
{
	std::vector<SoundDevice> sound_devices;
	sndDeviceHandleToSoundDevices(sound_devices, active_devices);
	return sound_devices;
}

int AudioPassthruPrivate::sndDeviceHandleToSoundDevices(std::vector<SoundDevice>& sound_devices, bool active_devices)
{
	int i_resultFlag;
	wchar_t wcp_user_seleted_playback_device_guid[PT_MAX_GENERIC_STRLEN];
	wchar_t wcp_targeted_real_playback_device_guid[PT_MAX_GENERIC_STRLEN];
	wchar_t wcp_capture_device_guid[PT_MAX_GENERIC_STRLEN];
	wchar_t wcp_dfx_device_guid[PT_MAX_GENERIC_STRLEN];
	wchar_t wcp_default_device_guid[PT_MAX_GENERIC_STRLEN];

	// Cast the handle so we can access its data.
	struct sndDevicesHdlType *cast_handle;
	cast_handle = (struct sndDevicesHdlType *)hp_sndDevices_;

	sound_devices.clear();

	wcp_user_seleted_playback_device_guid[0] = L'\0';

	// Get the guid of the currently selected real playback device
	if ((sndDevicesGetID(hp_sndDevices_, SND_DEVICES_USER_SELECTED_PLAYBACK_DEVICE, wcp_user_seleted_playback_device_guid, &i_resultFlag) != OKAY &&
		i_resultFlag != SND_DEVICES_DEVICE_NOT_PRESENT) ||
		sndDevicesGetID(hp_sndDevices_, SND_DEVICES_TARGETED_REAL_PLAYBACK, wcp_targeted_real_playback_device_guid, &i_resultFlag) != OKAY ||
		sndDevicesGetID(hp_sndDevices_, SND_DEVICES_CAPTURE, wcp_capture_device_guid, &i_resultFlag) != OKAY ||
		sndDevicesGetID(hp_sndDevices_, SND_DEVICES_VIRTUAL_PLAYBACK_DFX, wcp_dfx_device_guid, &i_resultFlag) != OKAY ||
		sndDevicesGetID(hp_sndDevices_, SND_DEVICES_DEFAULT, wcp_default_device_guid, &i_resultFlag) != OKAY)
	{
		return(NOT_OKAY);
	}

	for (int index = 0; index < cast_handle->totalNumDevices; index++) 
	{
		if (cast_handle->pwszID[index][0] == L'\0' || cast_handle->deviceFriendlyName[index][0] == L'\0')
		{
			continue;
		}

		if (active_devices && cast_handle->deviceState[index] != DEVICE_STATE_ACTIVE)
		{
			continue;
		}

		SoundDevice sound_device;
		sound_device.pwszID = std::wstring(cast_handle->pwszID[index]);
		sound_device.containerId = std::wstring(cast_handle->containerId[index]);
		sound_device.deviceFriendlyName = std::wstring(cast_handle->deviceFriendlyName[index]);
		sound_device.deviceDescription = std::wstring(cast_handle->deviceDescription[index][0] != L'\0' ? cast_handle->deviceDescription[index] : L"");
		sound_device.deviceNumChannel = cast_handle->deviceNumChannel[index];
		if (cast_handle->deviceState[index] == DEVICE_STATE_ACTIVE)
		{
			sound_device.isActive = true;
		}
		else
		{
			sound_device.isActive = false;
        }

		// Skip mono devices if SND_DEVICES_MONO_BUG_SKIP_MONO_DEVICES is IS_TRUE
		if (SND_DEVICES_MONO_BUG_SKIP_MONO_DEVICES && sound_device.deviceNumChannel == 1)
		{
			continue;
		}

		// Figure out if this is a real device or not
		sound_device.isRealDevice = false;
		for (int index2 = 0; index2 < cast_handle->numRealDevices; index2++) 
		{
			if (sound_device.pwszID == cast_handle->pwszIDRealDevices[index2]) 
			{
				sound_device.isRealDevice = true;
			}
		}

		if (sound_device.pwszID == wcp_user_seleted_playback_device_guid)
		{
			sound_device.isUserSelectedPlaybackDevice = true;
		}
		if (sound_device.pwszID == wcp_targeted_real_playback_device_guid &&
			sound_device.pwszID != wcp_dfx_device_guid)
		{
			sound_device.isTargetedRealPlaybackDevice = true;
		}
		if (sound_device.pwszID == wcp_capture_device_guid)
		{
			sound_device.isCaptureDevice = true;
		}
		if (sound_device.pwszID == wcp_dfx_device_guid)
		{
			sound_device.isDFXDevice = true;
		}
		if (sound_device.pwszID == wcp_default_device_guid)
		{
			sound_device.isDefaultDevice = true;
		}

		sound_devices.push_back(sound_device);
	}	

	return(OKAY);
}

void AudioPassthruPrivate::onDeviceChange(int change_type, LPCWSTR device_id)
{
	if (s_callback_ != nullptr)
	{
		AudioDeviceChangeKind change_kind = AudioDeviceChangeKind::Unknown;

		switch (change_type)
		{
			case SND_DEVICES_DEFAULT_DEVICE_CHANGED:
				change_kind = AudioDeviceChangeKind::DefaultChanged;
				break;
			case SND_DEVICES_DEVICE_ADDED:
				change_kind = AudioDeviceChangeKind::DeviceAdded;
				break;
			case SND_DEVICES_DEVICE_REMOVED:
				change_kind = AudioDeviceChangeKind::DeviceRemoved;
				break;
			case SND_DEVICES_DEVICE_ACTIVE:
			case SND_DEVICES_DEVICE_DISABLED:
			case SND_DEVICES_DEVICE_NOTPRESENT:
			case SND_DEVICES_DEVICE_UNPLUGGED:
			case SND_DEVICES_DEVICE_PROPERTY_CHANGE:
				change_kind = AudioDeviceChangeKind::DeviceStateChanged;
				break;
		}

		s_callback_->onSoundDeviceChange(change_kind, device_id != nullptr ? std::wstring(device_id) : std::wstring());
	}
}

/*
* FUNCTION: killProcessingThread()
* DESCRIPTION:
*
*  If the processing thread is running, kill it.
*  This function does not return until the thread has been killed or it times out
*  trying to kill the thread.
*
*/
int AudioPassthruPrivate::killProcessingThread(int *ip_timed_out)
{
	BOOL b_need_to_kill_thread;
	BOOL bReturn;
	DWORD d_ExitCode;
	int i_thread_has_died;
	long l_total_msecs_waited;

	*ip_timed_out = IS_FALSE;

	/* Check if we should kill the processing thread */
	b_need_to_kill_thread = FALSE;

	if (hProcessingThread_ != NULL)
	{
		if (processing_thread_running_)
		{
			b_need_to_kill_thread = TRUE;
		}
	}

	/* Kill the thread */
	if (b_need_to_kill_thread)
	{
		// Stop capture.  This will cause sndDevicesDoCapture() to exit and then the thread will die.
		if (sndDevicesStartStopCapture(hp_sndDevices_, SND_DEVICES_STOP_CAPTURE) != OKAY)
			return(NOT_OKAY);

		i_kill_processing_thread_ = IS_TRUE;

		/* Wait until thread has died or we have timed out */
		i_thread_has_died = IS_FALSE;
		l_total_msecs_waited = 0L;

		while ((!i_thread_has_died) && (!(*ip_timed_out)))
		{
			bReturn = GetExitCodeThread(hProcessingThread_, &d_ExitCode);
			if (d_ExitCode != STILL_ACTIVE)
			{
				i_thread_has_died = IS_TRUE;
				processing_thread_running_ = false;
				hProcessingThread_ = NULL;
			}
			else
			{
				Sleep(DFXG_SND_SERVER_KILL_THREAD_WAIT_PER_LOOP_MSECS);
				l_total_msecs_waited = l_total_msecs_waited + DFXG_SND_SERVER_KILL_THREAD_WAIT_PER_LOOP_MSECS;
				if (l_total_msecs_waited >= DFXG_SND_SERVER_KILL_THREAD_TIMEOUT_MSECS)
					*ip_timed_out = IS_TRUE;
			}
		}
	}

	return(OKAY);
}

/*
* FUNCTION: setBufferLength()
* DESCRIPTION:
*
*  Sets the buffer length.
*
*/
int AudioPassthruPrivate::setBufferLength(int i_buffer_length_msecs)
{
	int i_timed_out;

	/*
	* Set the new buffer setting.
	* This just sets registry value which will be picked up during reinit.
	*/
	if (sndDevicesSetBufferSizeMilliSecs(hp_sndDevices_, i_buffer_length_msecs) != OKAY)
		return(NOT_OKAY);

	/* Kill the processing thread, so that the timer will then restart it with the new buffer setting */
	if (killProcessingThread(&i_timed_out) != OKAY)
		return(NOT_OKAY);

	/* If the killing of processing attempt timed out, then we can't do anything else */
	if (i_timed_out)
		return(OKAY);

	return(OKAY);
}

/*
* FUNCTION: processTimer()
* DESCRIPTION:
*
*  Process the snd server timer.
*/
int AudioPassthruPrivate::processTimer()
{
	BOOL b_need_to_start_thread;
	int numRealDevices;
	int DfxDeviceEnabledFlag;
	int statusFlag;
	struct sndDevicesHdlType *cast_handle;
	cast_handle = (struct sndDevicesHdlType *)hp_sndDevices_;
	b_need_to_start_thread = FALSE;

	/*
	* Check if we need to start the thread for the first time or restart it.
	* Keep in mind that the main thread will kill itself if anything changes in terms of active soundcards.
	* This is kind of a brute force way of reinitializing and taking care of changes.
	*/
	if (hProcessingThread_ == NULL)
		b_need_to_start_thread = TRUE;
	else if (!processing_thread_running_)
	{
		b_need_to_start_thread = TRUE;
	}

	if (b_need_to_start_thread)
	{
		if (device_change_pending_)
			return(OKAY);

		last_capture_with_samples_tick_ms_ = 0;
		last_successful_playback_tick_ms_ = 0;
		last_capture_input_rms_db_ = -160.0f;
		last_submitted_playback_rms_db_ = -160.0f;

		/* Initialize flag which can be set by the outside telling thread to end */
		i_kill_processing_thread_ = IS_FALSE;

		/* Reinit the sndDevices module */
		if (sndDevicesReInit(hp_sndDevices_, SND_DEVICES_INIT_FOR_PROCESSING, &numRealDevices, &DfxDeviceEnabledFlag, &statusFlag) != OKAY)
		{
			wchar_t diagnostic[256];
			swprintf(diagnostic, 256, L"sndDevicesReInit returned NOT_OKAY statusFlag=%d", statusFlag);
			notifyDiagnosticMessage(diagnostic);
			return(NOT_OKAY);
		}

		if (statusFlag != SND_DEVICES_DEVICE_OPERATION_COMPLETED)
		{
			wchar_t diagnostic[256];
			swprintf(diagnostic, 256, L"sndDevicesReInit completed with non-ready statusFlag=%d numRealDevices=%d dfxEnabled=%d",
				statusFlag, numRealDevices, DfxDeviceEnabledFlag);
			notifyDiagnosticMessage(diagnostic);
			/*
			if (cast_handle->trace.mode)
			{
				swprintf(cast_handle->wcp_msg1, 2048, L"dfxg_ProcessSndServerTimer(): sndDevicesReInit failed, statusFlag = %d", statusFlag);
				(cast_handle->slout1)->Message_Wide(FIRST_LINE, cast_handle->wcp_msg1);
			}

			if (statusFlag == SND_DEVICES_NO_VALID_PLAYBACK_DEVICE)
			{
				if (dfxg_GetTranslatedString(hp_dfxg, IDS_DFX_MSG_ASK_EXIT_DFX_TO_HEAR_SOUNDS,
					cast_handle->wcp_translated_string) != OKAY)
					return(NOT_OKAY);
			}

			if (statusFlag == SND_DEVICES_ASK_USER_SELECT_PLAYBACK_DEVICE)
			{
				if (dfxg_GetTranslatedString(hp_dfxg, IDS_DFX_MSG_SELECT_AUDIO_PLAYBACK_FROM_MENU,
					cast_handle->wcp_translated_string) != OKAY)
					return(NOT_OKAY);
			}

			if (cast_handle->snd_server.b_no_valid_snd_device_dialog_shown == false)
			{
				cast_handle->snd_server.b_no_valid_snd_device_dialog_shown = true;
				if (dfxg_BringUpMsgModelessDlg(hp_dfxg, cast_handle->wcp_translated_string, IS_FALSE, NULL, IS_FALSE, 0) != OKAY)
					return(NOT_OKAY);
			}
			*/
		}
		else
		{
			b_no_valid_snd_device_dialog_shown_ = false;
			wchar_t diagnostic[256];
			swprintf(diagnostic, 256, L"sndDevicesReInit completed statusFlag=%d numRealDevices=%d dfxEnabled=%d",
				statusFlag, numRealDevices, DfxDeviceEnabledFlag);
			notifyDiagnosticMessage(diagnostic);
		}

		/* PTNOTE - added check on DfxDeviceEnabledFlag status, may need to take additional steps if no DFX device is preset. */
		if ((numRealDevices > 0) && (DfxDeviceEnabledFlag == IS_TRUE))
		{
			hProcessingThread_ = CreateThread(NULL, 0, processingThread, (LPVOID)this, 0L, &ProcessingThreadID_);
			processing_thread_running_ = (hProcessingThread_ != NULL);
			if (processing_thread_running_)
			{
				notifyDiagnosticMessage(L"Audio passthru processing thread started");
			}
			else
			{
				notifyDiagnosticMessage(L"CreateThread failed for audio passthru processing thread");
			}
		}

		/* Check if a new playback device has been selected */
		/*
		if (dfxg_SndServerHdmiFixCheckForNewPlaybackDevice(hp_dfxg) != OKAY)
			return(NOT_OKAY);
		*/
	}

	return(OKAY);
}

/*
* FUNCTION: threadWorker()
* DESCRIPTION:
*/
DWORD AudioPassthruPrivate::threadWorker(void)
{
	float *fp_buffer;
	int numSampleSets;
	WAVEFORMATEX *pwfx;
	int i_check_for_duplicate_buffers;
	int i_valid_bits;
	int resultFlag;
	int exitResultFlag = 0;
	std::wstring exitReason = L"normal_exit";
	DWORD setReturn;
	LARGE_INTEGER qpc_frequency;
	struct sndDevicesHdlType *cast_handle;

	cast_handle = (struct sndDevicesHdlType *)hp_sndDevices_;
	// Raise the priority of this tread to improve performance. GetCurrentThread() is a call that
	// returns the current thread ID from within the thread itself.
	// A return of 0 means set failed. Not sure what option to use, MS doc is confusing, THREAD_PRIORITY_HIGHEST is another option.
	setReturn = SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

	if (QueryPerformanceFrequency(&qpc_frequency))
		cast_handle->latency.qpcFrequency = (cast_handle->latency.loggingEnabled == TRUE) ? qpc_frequency.QuadPart : 0;
	else
		cast_handle->latency.qpcFrequency = 0;

	resetLatencyMeasurementState(&cast_handle->latency);

	// Start capture.
	if (sndDevicesStartStopCapture(hp_sndDevices_, SND_DEVICES_START_CAPTURE) != OKAY)
	{
		notifyDiagnosticMessage(L"sndDevicesStartStopCapture(START) failed");
		return(NOT_OKAY);
	}
	notifyDiagnosticMessage(L"sndDevicesStartStopCapture(START) completed");

	/*
	* Keep looping until a goto statement is reached to kill the thread
	* Also, constantly check to see the kill_thread flag has been set from the outside.
	*/
	while (1)
	{
		/* Check if thread has been signaled to end */
		if (i_kill_processing_thread_)
		{
			exitReason = L"kill_flag_before_capture";
			goto KillProcessingThread;
		}

		// Does the data capture and returns a pointer to the data and signal info to be used for in place audio processing.
		// NOT_OKAY is only returned for catastrophic errors but if resultFlag != SND_DEVICES_CAPTURE_PLAYBACK_SUCCESS
		// then typically a change in a device property has caused the capture or playback operation to fail, in this
		// case we need to exit this thread so a reinitialization can be done.
		if (sndDevicesDoCapture(hp_sndDevices_, &fp_buffer, &numSampleSets, &pwfx, &resultFlag) != OKAY)
		{
			notifyDiagnosticMessage(L"sndDevicesDoCapture returned NOT_OKAY");
			return(NOT_OKAY);
		}

		// A non-successful flag will typically be due to a change in the playback devices properties.
		if (resultFlag != SND_DEVICES_CAPTURE_PLAYBACK_SUCCESS)
		{
			exitReason = L"capture_result_flag";
			exitResultFlag = resultFlag;
			goto KillProcessingThread;
		}

		/* Check if thread has been signaled to end */
		if (i_kill_processing_thread_)
		{
			exitReason = L"kill_flag_after_capture";
			goto KillProcessingThread;
		}

		if (numSampleSets > 0)
		{
			if (last_capture_with_samples_tick_ms_ == 0)
			{
				notifyDiagnosticMessage(L"Audio passthru capture stream received first samples");
			}
			last_capture_with_samples_tick_ms_ = GetTickCount64();
			last_capture_input_rms_db_ = calculateBufferRmsDb(fp_buffer, numSampleSets * pwfx->nChannels);

			/* Set additional processing settings */
			i_valid_bits = pwfx->wBitsPerSample;
			i_check_for_duplicate_buffers = IS_FALSE;

			/* Make sure processing module has the current format settings */
			//if (dfxpUniversalSetSignalFormat(cast_handle->dfxp_hdl, pwfx->wBitsPerSample, pwfx->nChannels, pwfx->nSamplesPerSec, i_valid_bits) != OKAY)
				//return(NOT_OKAY);
			if (p_dfx_dsp_->setSignalFormat(pwfx->wBitsPerSample, pwfx->nChannels, pwfx->nSamplesPerSec, i_valid_bits) != OKAY)
			{
				// In Release build, initially setSignalFormat() will return NOT OKAY so execution will hit here and throws up message box due to return NOT_OAKY.
				// However, it doesn't happen in Debug build. Workaround is just comment out return(NOT_OKAY) so it won't throw up message boxes in Release build
				// and eventually it will start to return OKAY.
				//return(NOT_OKAY);
			}
				
			

			// 2016-08-25: Workaround for crashing when playback device has only one channel (mono). For example, a Bleutooth headset is mono. When such
			// device is selected as the playback device, we do not apply DFX/DSP processing to the buffer for now. Otherwise it will crash.
			// NOTE: There will also be no sound with a mono playback device until we add code to fill mono playback buffer in sndDevicesDoCapture.cpp, line 279.
			if (dsp_processing_enabled_ &&
				(pwfx->nChannels != 1 || (pwfx->nChannels == 1 && !SND_DEVICES_MONO_BUG_DO_NOT_PROCESS)))
			{
				// Apply DFX processing here using data and format vars above. Format will always be 32 bit floating point.
			//	if (dfxpUniversalModifySamples(cast_handle->dfxp_hdl, (short int *)fp_buffer, (short int *)fp_buffer, numSampleSets, i_check_for_duplicate_buffers) != OKAY)
				//	return(NOT_OKAY);
				p_dfx_dsp_->processAudio((short int *)fp_buffer, (short int *)fp_buffer, numSampleSets, i_check_for_duplicate_buffers);
			}

			/* Check if thread has been signaled to end */
			if (i_kill_processing_thread_)
			{
				exitReason = L"kill_flag_after_process";
				goto KillProcessingThread;
			}

			/* A non-successful flag will typically be due to a change in the playback devices properties. */
			if (resultFlag != SND_DEVICES_CAPTURE_PLAYBACK_SUCCESS)
			{
				exitReason = L"post_process_result_flag";
				exitResultFlag = resultFlag;
				goto KillProcessingThread;
			}
		}

		/*
		* Plays processed buffer.
		* NOTE: This needs to outside of the if block so that unplayed buffers can still be played.
		*       It is okay to call this even when all buffers have already been played.
		*/
		if (!mute_)
		{
			if (sndDevicesDoPlayback(hp_sndDevices_, &resultFlag) != OKAY)
			{
				notifyDiagnosticMessage(L"sndDevicesDoPlayback returned NOT_OKAY");
				return(NOT_OKAY);
			}

			if ((resultFlag == SND_DEVICES_CAPTURE_PLAYBACK_SUCCESS) &&
				(cast_handle->playbackFrameCount > 0))
			{
				if (last_successful_playback_tick_ms_ == 0)
				{
					notifyDiagnosticMessage(L"Audio passthru playback stream submitted first frames");
				}
				last_successful_playback_tick_ms_ = GetTickCount64();
				last_submitted_playback_rms_db_ = calculateBufferRmsDb(
					cast_handle->fPlaybackBuf,
					static_cast<int>(cast_handle->playbackFrameCount * cast_handle->wfxPlayback.nChannels));
			}
		}
		

		/* Make sure the playback succeeded */
		if (resultFlag != SND_DEVICES_CAPTURE_PLAYBACK_SUCCESS)
		{
			exitReason = L"playback_result_flag";
			exitResultFlag = resultFlag;
			goto KillProcessingThread;
		}
	}

KillProcessingThread:
	// Stop capture.
	if (sndDevicesStartStopCapture(hp_sndDevices_, SND_DEVICES_STOP_CAPTURE) != OKAY)
	{
		notifyDiagnosticMessage(L"sndDevicesStartStopCapture(STOP) failed");
		return(NOT_OKAY);
	}
	notifyDiagnosticMessage(L"sndDevicesStartStopCapture(STOP) completed");

	{
		wchar_t diagnostic[256];
		swprintf(diagnostic, 256, L"Audio passthru processing thread stopped reason=%ls resultFlag=%d",
			exitReason.c_str(), exitResultFlag);
		notifyDiagnosticMessage(diagnostic);
	}

	/* Simply returning from this function stops the thread */
	return(OKAY);
}

/*
* FUNCTION: processingThread()
* DESCRIPTION:
*/
DWORD WINAPI AudioPassthruPrivate::processingThread(LPVOID lpParam)
{
	// Make sure COM is initialized
	HRESULT hr;
	hr = CoInitialize(NULL);

	AudioPassthruPrivate * callerClass = (AudioPassthruPrivate*)lpParam;
	auto ret = callerClass->threadWorker();
	callerClass->processing_thread_running_ = false;

	CoUninitialize();

	return ret;
}

void AudioPassthruPrivate::mute(bool mute)
{
	mute_ = mute;
}

void AudioPassthruPrivate::registerCallback(AudioPassthruCallback* callback)
{
	s_callback_ = callback;
}

void AudioPassthruPrivate::notifyDiagnostic(const std::wstring& message)
{
	if (s_callback_ != nullptr)
	{
		s_callback_->onAudioPassthruDiagnostic(message);
	}
}

bool AudioPassthruPrivate::isPlaybackDeviceAvailable()
{
    BOOL availability;

    sndDevicesGetPlaybackDeviceAvialblility(hp_sndDevices_, &availability);
    if (availability == TRUE)
        return true;
    else
        return false;
}

void AudioPassthruPrivate::restoreDefaultPlaybackDevice()
{
	int i_resultFlag;
	/* Change the default soundcard to not be the DFX virtual one but instead the proper real one */
	if (sndDevicesRestoreDefaultDevice(hp_sndDevices_, &i_resultFlag) != OKAY)
	{
		wchar_t diagnostic[256];
		swprintf(diagnostic, 256, L"sndDevicesRestoreDefaultDevice failed resultFlag=%d", i_resultFlag);
		notifyDiagnosticMessage(diagnostic);
		return;
	}

	notifyDiagnosticMessage(L"sndDevicesRestoreDefaultDevice completed");
}

bool AudioPassthruPrivate::restartProcessingForDeviceChange()
{
	int i_timed_out = IS_FALSE;

	notifyDiagnosticMessage(L"Audio passthru restart requested for device change");

	device_change_pending_ = true;

	if (killProcessingThread(&i_timed_out) != OKAY || i_timed_out)
	{
		wchar_t diagnostic[256];
		swprintf(diagnostic, 256, L"Audio passthru restart failed during thread shutdown timed_out=%d", i_timed_out);
		notifyDiagnosticMessage(diagnostic);
		device_change_pending_ = false;
		return false;
	}

	device_change_pending_ = false;

	auto restarted = processTimer() == OKAY;
	notifyDiagnosticMessage(restarted
		? L"Audio passthru restart completed after device change"
		: L"Audio passthru restart failed after device change");
	return restarted;
}

bool AudioPassthruPrivate::isProcessingThreadRunning()
{
	return processing_thread_running_;
}

bool AudioPassthruPrivate::isMuted()
{
	return mute_;
}

uint64_t AudioPassthruPrivate::getLastCaptureWithSamplesTickMs()
{
	return last_capture_with_samples_tick_ms_;
}

uint64_t AudioPassthruPrivate::getLastSuccessfulPlaybackTickMs()
{
	return last_successful_playback_tick_ms_;
}

float AudioPassthruPrivate::getLastCaptureInputRmsDb()
{
	return last_capture_input_rms_db_;
}

float AudioPassthruPrivate::getLastSubmittedPlaybackRmsDb()
{
	return last_submitted_playback_rms_db_;
}

int AudioPassthruPrivate::setTargetedRealPlaybackDevice(const std::wstring sound_device_guid)
{
	int i_resultFlag;
	wchar_t wcp_new_targeted_real_playback_guid[PT_MAX_GENERIC_STRLEN];
	
	/* Set the guid of the newly selected playback device */
	swprintf(wcp_new_targeted_real_playback_guid, PT_MAX_GENERIC_STRLEN, L"%s", sound_device_guid.c_str());

	/* Set the newly targeted playback device as the default.  It will then automatically become the targeted device */
	if (sndDevicesSetDeviceType(hp_sndDevices_, SND_DEVICES_DEFAULT, wcp_new_targeted_real_playback_guid, &i_resultFlag) != OKAY)
		return(NOT_OKAY);

	return(OKAY);
}
