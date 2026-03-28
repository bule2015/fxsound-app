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
/*
*  Public defines for the AudioPassthru class
*/

#ifndef _AUDIOPASSTHRU_H_
#define _AUDIOPASSTHRU_H_ 

#ifndef __ANDROID__
#include <Windows.h>
#endif //WIN32
#include <Mmdeviceapi.h>
#include <vector> 
#include "DfxDsp.h"

struct SoundDevice {
	IMMDevice *pAllDevices = NULL; // Object pointers for each device.
	bool isCaptureDevice = false;
	bool isPlaybackDevice = false;
	bool isTargetedRealPlaybackDevice = false;
	bool isRealDevice = false;
	bool isDFXDevice = false;
	bool isUserSelectedPlaybackDevice = false;
	bool isDefaultDevice = false;
    bool isActive = false;

	std::wstring pwszID; // For the GUID ID strings for each device, all devices combined.
	std::wstring containerId;
	std::wstring pwszIDRealDevices; // For the GUID ID strings for each real playback device.
	WCHAR pwszIDPreviousRealDevices[512]; // To detect when a new devices is added.

	std::wstring deviceFriendlyName;  // Friendly name, ie "DFX Audio Enhancer 10.5", all devices.
	std::wstring deviceDescription;  // Descriptive name, ie "Speakers", all devices.
	std::wstring deviceFriendlyNameRealDevices;  // Friendly names, just the real devices.
	std::wstring deviceDescriptionRealDevices;  // Descriptive names, just the real devices.
	int deviceNumChannel; // Number of channels for all devices.
};

enum class AudioDeviceChangeKind
{
	Unknown = 0,
	DefaultChanged,
	DeviceAdded,
	DeviceRemoved,
	DeviceStateChanged
};

class AudioPassthruCallback
{
public:
	virtual ~AudioPassthruCallback() = default;
	virtual void onSoundDeviceChange(AudioDeviceChangeKind change_kind, const std::wstring& device_id) = 0;
};

class IAudioPassthru
{
public:
	virtual ~IAudioPassthru() = default;
	virtual int init() = 0;
	virtual void mute(bool mute) = 0;
	virtual std::vector<SoundDevice> getSoundDevices(bool active_devices = true) = 0;
	virtual int setBufferLength(int i_buffer_length_msecs) = 0;
	virtual int processTimer() = 0;
	virtual void setDspProcessingModule(DfxDsp* pDspProcessingModule) = 0;
	virtual void setAsPlaybackDevice(const SoundDevice sound_device) = 0;
	virtual void registerCallback(AudioPassthruCallback* callback) = 0;
	virtual bool isPlaybackDeviceAvailable() = 0;
	virtual void restoreDefaultPlaybackDevice() = 0;
	virtual bool restartProcessingForDeviceChange() = 0;
};

class AudioPassthruPrivate;
class AudioPassthru : public IAudioPassthru
{
public:
	AudioPassthru();
	~AudioPassthru();
	int init() override;
	void mute(bool mute) override;
	std::vector<SoundDevice> getSoundDevices(bool active_devices = true) override;
	int setBufferLength(int i_buffer_length_msecs) override;
	int processTimer() override;
	void setDspProcessingModule(DfxDsp* pDspProcessingModule) override;
	void setAsPlaybackDevice(const SoundDevice sound_device) override;
	void registerCallback(AudioPassthruCallback *callback) override;
    bool isPlaybackDeviceAvailable() override;
	void restoreDefaultPlaybackDevice() override;
	bool restartProcessingForDeviceChange() override;

private:
	AudioPassthruPrivate *data_;
};

#endif
