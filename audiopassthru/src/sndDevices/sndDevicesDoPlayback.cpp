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

/* Standard includes */
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <stdio.h>
#include <math.h>

#include <mmreg.h>
#include <Mmdeviceapi.h>
#include <Audioclient.h>
#include <Functiondiscoverykeys_devpkey.h>
#include <endpointvolume.h>
#include <Propvarutil.h>

#include "codedefs.h"
#include "slout.h"
#include "mry.h"
#include "u_sndDevices.h"
#include "sndDevices.h"

static void sndDevicesAppendLatencyLogLine(const wchar_t *line)
{
	wchar_t appDataPath[PT_MAX_PATH_STRLEN] = {};
	wchar_t directoryPath[PT_MAX_PATH_STRLEN] = {};
	wchar_t logPath[PT_MAX_PATH_STRLEN] = {};
	FILE *stream = NULL;
	DWORD appDataLength;

	appDataLength = GetEnvironmentVariableW(L"APPDATA", appDataPath, PT_MAX_PATH_STRLEN);
	if ((appDataLength == 0) || (appDataLength >= PT_MAX_PATH_STRLEN))
		return;

	swprintf(directoryPath, PT_MAX_PATH_STRLEN, L"%ls\\FxSound", appDataPath);
	CreateDirectoryW(directoryPath, NULL);

	swprintf(logPath, PT_MAX_PATH_STRLEN, L"%ls\\fxsound-output-latency.log", directoryPath);
	if ((_wfopen_s(&stream, logPath, L"a+, ccs=UTF-8") != 0) || (stream == NULL))
		return;

	fwprintf(stream, L"%ls\r\n", line);
	fclose(stream);
}

static void sndDevicesRecordLatencyMeasurement(struct sndDevicesHdlType *cast_handle, UINT32 numFramesQueuedUpToPlay)
{
	LARGE_INTEGER now_qpc;
	SYSTEMTIME local_time;
	wchar_t logLine[512];
	double captureToRenderMs;
	double captureAgeMs;
	double playbackQueueMs;
	double estimatedOutputMs;
	double averageCaptureToRenderMs;
	double averageCaptureAgeMs;
	double averagePlaybackQueueMs;
	double averageEstimatedOutputMs;
	UINT64 currentQpc100ns;

	if ((cast_handle->latencyLoggingEnabled != TRUE) ||
		(cast_handle->latencyCaptureBatchStartQpc == 0) ||
		(cast_handle->latencyQpcFrequency == 0))
		return;

	if (!QueryPerformanceCounter(&now_qpc))
		return;

	captureToRenderMs = (double)(now_qpc.QuadPart - cast_handle->latencyCaptureBatchStartQpc) * 1000.0 / (double)cast_handle->latencyQpcFrequency;
	playbackQueueMs = ((double)numFramesQueuedUpToPlay * 1000.0) / (double)cast_handle->wfxPlayback.nSamplesPerSec;
	currentQpc100ns = (UINT64)(((long double)now_qpc.QuadPart * 10000000.0L) / (long double)cast_handle->latencyQpcFrequency);

	if ((cast_handle->latencyCaptureBatchQpc100ns != 0) && (currentQpc100ns >= cast_handle->latencyCaptureBatchQpc100ns))
		captureAgeMs = (double)(currentQpc100ns - cast_handle->latencyCaptureBatchQpc100ns) / 10000.0;
	else
		captureAgeMs = captureToRenderMs;

	estimatedOutputMs = captureAgeMs + playbackQueueMs;

	if (cast_handle->latencyMeasurementCount == 0)
	{
		cast_handle->latencyCaptureToRenderMinMs = captureToRenderMs;
		cast_handle->latencyCaptureToRenderMaxMs = captureToRenderMs;
		cast_handle->latencyCaptureAgeMinMs = captureAgeMs;
		cast_handle->latencyCaptureAgeMaxMs = captureAgeMs;
		cast_handle->latencyPlaybackQueueMaxMs = playbackQueueMs;
		cast_handle->latencyEstimatedOutputMinMs = estimatedOutputMs;
		cast_handle->latencyEstimatedOutputMaxMs = estimatedOutputMs;
	}
	else
	{
		if (captureToRenderMs < cast_handle->latencyCaptureToRenderMinMs)
			cast_handle->latencyCaptureToRenderMinMs = captureToRenderMs;
		if (captureToRenderMs > cast_handle->latencyCaptureToRenderMaxMs)
			cast_handle->latencyCaptureToRenderMaxMs = captureToRenderMs;
		if (captureAgeMs < cast_handle->latencyCaptureAgeMinMs)
			cast_handle->latencyCaptureAgeMinMs = captureAgeMs;
		if (captureAgeMs > cast_handle->latencyCaptureAgeMaxMs)
			cast_handle->latencyCaptureAgeMaxMs = captureAgeMs;
		if (playbackQueueMs > cast_handle->latencyPlaybackQueueMaxMs)
			cast_handle->latencyPlaybackQueueMaxMs = playbackQueueMs;
		if (estimatedOutputMs < cast_handle->latencyEstimatedOutputMinMs)
			cast_handle->latencyEstimatedOutputMinMs = estimatedOutputMs;
		if (estimatedOutputMs > cast_handle->latencyEstimatedOutputMaxMs)
			cast_handle->latencyEstimatedOutputMaxMs = estimatedOutputMs;
	}

	cast_handle->latencyCaptureToRenderSumMs += captureToRenderMs;
	cast_handle->latencyCaptureAgeSumMs += captureAgeMs;
	cast_handle->latencyPlaybackQueueSumMs += playbackQueueMs;
	cast_handle->latencyEstimatedOutputSumMs += estimatedOutputMs;
	cast_handle->latencyMeasurementCount += 1;
	cast_handle->latencyCaptureBatchStartQpc = 0;
	cast_handle->latencyCaptureBatchQpc100ns = 0;

	if ((cast_handle->latencyLastLogQpc != 0) &&
		((now_qpc.QuadPart - cast_handle->latencyLastLogQpc) < cast_handle->latencyQpcFrequency))
	{
		return;
	}

	averageCaptureToRenderMs = cast_handle->latencyCaptureToRenderSumMs / (double)cast_handle->latencyMeasurementCount;
	averageCaptureAgeMs = cast_handle->latencyCaptureAgeSumMs / (double)cast_handle->latencyMeasurementCount;
	averagePlaybackQueueMs = cast_handle->latencyPlaybackQueueSumMs / (double)cast_handle->latencyMeasurementCount;
	averageEstimatedOutputMs = cast_handle->latencyEstimatedOutputSumMs / (double)cast_handle->latencyMeasurementCount;

	GetLocalTime(&local_time);
	swprintf(
		logLine,
		sizeof(logLine) / sizeof(logLine[0]),
		L"%04d-%02d-%02d %02d:%02d:%02d internal_latency_ms capture_to_render(avg=%.2f min=%.2f max=%.2f) capture_packet_age(avg=%.2f min=%.2f max=%.2f) playback_queue_before_write(avg=%.2f max=%.2f) estimated_output(avg=%.2f min=%.2f max=%.2f) samples=%u buffer_ms=%d",
		local_time.wYear,
		local_time.wMonth,
		local_time.wDay,
		local_time.wHour,
		local_time.wMinute,
		local_time.wSecond,
		averageCaptureToRenderMs,
		cast_handle->latencyCaptureToRenderMinMs,
		cast_handle->latencyCaptureToRenderMaxMs,
		averageCaptureAgeMs,
		cast_handle->latencyCaptureAgeMinMs,
		cast_handle->latencyCaptureAgeMaxMs,
		averagePlaybackQueueMs,
		cast_handle->latencyPlaybackQueueMaxMs,
		averageEstimatedOutputMs,
		cast_handle->latencyEstimatedOutputMinMs,
		cast_handle->latencyEstimatedOutputMaxMs,
		cast_handle->latencyMeasurementCount,
		cast_handle->bufferSizeMilliSecs);
	sndDevicesAppendLatencyLogLine(logLine);

	cast_handle->latencyLastLogQpc = now_qpc.QuadPart;
	cast_handle->latencyCaptureToRenderSumMs = 0.0;
	cast_handle->latencyCaptureToRenderMinMs = 0.0;
	cast_handle->latencyCaptureToRenderMaxMs = 0.0;
	cast_handle->latencyCaptureAgeSumMs = 0.0;
	cast_handle->latencyCaptureAgeMinMs = 0.0;
	cast_handle->latencyCaptureAgeMaxMs = 0.0;
	cast_handle->latencyPlaybackQueueSumMs = 0.0;
	cast_handle->latencyPlaybackQueueMaxMs = 0.0;
	cast_handle->latencyEstimatedOutputSumMs = 0.0;
	cast_handle->latencyEstimatedOutputMinMs = 0.0;
	cast_handle->latencyEstimatedOutputMaxMs = 0.0;
	cast_handle->latencyMeasurementCount = 0;
}

/*
 * FUNCTION: sndDevicesDoPlayback()
 * DESCRIPTION: Plays back audio buffers to specified playback device.
 * Plays buffer frames that have been queued up by the capture device.
 */
int PT_DECLSPEC sndDevicesDoPlayback(PT_HANDLE *hp_sndDevices, int *ip_resultFlag)
{
	struct sndDevicesHdlType *cast_handle;
	HRESULT hr;
	UINT32 numFramesQueuedUpToPlay;
	UINT32 i, j, index, loopsize;
	DWORD flags = 0;
	int numPlaybackChannels;
	int k;

	float *fptr;

	cast_handle = (struct sndDevicesHdlType *)hp_sndDevices;

	if (cast_handle == NULL)
		return(NOT_OKAY);

	if( cast_handle->pAudioClientPlayback == NULL )
	{
		*ip_resultFlag = SND_DEVICES_NULL_PLAYBACK_CLIENT;
		SND_DEVICES_SET_STATUS_AND_RETURN_OK(SND_DEVICES_NULL_PLAYBACK_CLIENT)
	}

	numPlaybackChannels = cast_handle->wfxPlayback.nChannels;

	// Do upsampling if needed.
	if( cast_handle->upsampleRatio > 1 )
	{
		// Copy channel corrected playback buffers back to capture buffer so we can then re-sample.
		loopsize = cast_handle->capturedFramesCount * numPlaybackChannels;
		for(i=0; i<loopsize; i++)
			cast_handle->fCaptureBuf[i] = cast_handle->fPlaybackBuf[i];

		// Fill playback buff with upsampling
		index = 0;
		for(i=0; i<cast_handle->capturedFramesCount; i++) // Index through frames (sample sets)
		{
			for(j=0; j<cast_handle->upsampleRatio; j++)	 // Repeat frame fills for upsampling
			{
				for(k=0; k<numPlaybackChannels; k++)
				{
					cast_handle->fPlaybackBuf[index] = cast_handle->fCaptureBuf[i * numPlaybackChannels + k];
					index++;
				}
			}
		}
	}

	// This call returns the number of frames still awaiting playback in the playback buffer
	hr = cast_handle->pAudioClientPlayback->GetCurrentPadding(&numFramesQueuedUpToPlay);
	if (FAILED(hr)) SND_DEVICES_SET_STATUS_AND_RETURN_OK(SND_DEVICES_GET_PADDING_FAILED)

	cast_handle->numPlaybackFramesAvailableToFill = cast_handle->bufferFrameSizePlayback - numFramesQueuedUpToPlay;

	// Data size should never come in to this function larger than available space, truncate it if need be.
	if( cast_handle->playbackFrameCount > cast_handle->numPlaybackFramesAvailableToFill )
		cast_handle->playbackFrameCount = cast_handle->numPlaybackFramesAvailableToFill;			

	// If we have playback buffers to write, acquire the requested frame space in the internal playback buffer.
	if( cast_handle->playbackFrameCount > 0 )
	{
		hr = cast_handle->pAudioClientPlaybackRender->GetBuffer(cast_handle->playbackFrameCount, &(cast_handle->pDataPacketPlayback));
		if (FAILED(hr)) SND_DEVICES_SET_STATUS_AND_RETURN_OK(SND_DEVICES_PLAYBACK_RENDER_FAILED)

		// Don't do copy if either buffer is NULL
		if( (cast_handle->pDataPacketPlayback != NULL) && (cast_handle->fPlaybackBuf != NULL) )
		{
			loopsize = cast_handle->playbackFrameCount * numPlaybackChannels;
			fptr = (float *)(cast_handle->pDataPacketPlayback);

			for(i=0; i<loopsize; i++)
				fptr[i] = cast_handle->fPlaybackBuf[i];
		}

		hr = cast_handle->pAudioClientPlaybackRender->ReleaseBuffer(cast_handle->playbackFrameCount, flags);
		if (FAILED(hr)) SND_DEVICES_SET_STATUS_AND_RETURN_OK(SND_DEVICES_RELEASE_BUFFER_FAILED)

		sndDevicesRecordLatencyMeasurement(cast_handle, numFramesQueuedUpToPlay);
	}

	*ip_resultFlag = SND_DEVICES_CAPTURE_PLAYBACK_SUCCESS;

	return(OKAY);
}
