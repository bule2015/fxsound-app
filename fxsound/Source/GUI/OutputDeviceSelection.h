/*
FxSound
Copyright (C) 2026  FxSound LLC

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

#include <algorithm>
#include <string>
#include <vector>

#include "AudioPassthru.h"

namespace FxSound::OutputDeviceSelection
{
	struct PriorityEntry
	{
		std::wstring device_id;
		std::wstring device_name;
	};

	struct SyncDecision
	{
		SoundDevice resolved_output;
		bool has_resolved_output = false;
		bool output_changed = false;
		bool name_changed = false;
		bool routing_changed = false;
		bool should_apply_routing = false;
		bool should_mute = false;
	};

	inline bool areSameOutputDevice(const SoundDevice& lhs, const SoundDevice& rhs)
	{
		if (!lhs.pwszID.empty() && !rhs.pwszID.empty() && lhs.pwszID == rhs.pwszID)
		{
			return true;
		}

		if (!lhs.containerId.empty() &&
			!rhs.containerId.empty() &&
			lhs.containerId == rhs.containerId &&
			!lhs.deviceFriendlyName.empty() &&
			!rhs.deviceFriendlyName.empty() &&
			lhs.deviceFriendlyName == rhs.deviceFriendlyName)
		{
			return true;
		}

		return !lhs.deviceFriendlyName.empty() &&
			!rhs.deviceFriendlyName.empty() &&
			lhs.deviceFriendlyName == rhs.deviceFriendlyName &&
			lhs.deviceDescription == rhs.deviceDescription;
	}

	inline int getOutputDevicePriority(const std::vector<PriorityEntry>& priorities, const SoundDevice& sound_device)
	{
		auto priority = static_cast<int>(priorities.size());

		for (int i = 0; i < static_cast<int>(priorities.size()); ++i)
		{
			const auto& entry = priorities[static_cast<size_t>(i)];
			if ((!entry.device_id.empty() && entry.device_id == sound_device.pwszID) ||
				(!entry.device_name.empty() && entry.device_name == sound_device.deviceFriendlyName))
			{
				return i;
			}
		}

		return priority;
	}

	inline void sortOutputDevicesByPriority(std::vector<SoundDevice>& output_devices, const std::vector<PriorityEntry>& priorities)
	{
		std::stable_sort(output_devices.begin(), output_devices.end(),
			[&priorities](const SoundDevice& a, const SoundDevice& b)
			{
				auto priority_a = getOutputDevicePriority(priorities, a);
				auto priority_b = getOutputDevicePriority(priorities, b);
				if (priority_a != priority_b)
				{
					return priority_a < priority_b;
				}

				if (a.isActive != b.isActive)
				{
					return a.isActive > b.isActive;
				}

				return a.deviceFriendlyName < b.deviceFriendlyName;
			});
	}

	inline std::vector<SoundDevice> buildVisibleOutputDevices(const std::vector<SoundDevice>& sound_devices,
		const SoundDevice& selected_output,
		const std::vector<PriorityEntry>& priorities,
		bool include_selected_inactive)
	{
		std::vector<SoundDevice> output_devices;
		bool selected_output_present = false;

		for (auto sound_device : sound_devices)
		{
			if (!sound_device.isRealDevice || sound_device.deviceNumChannel < 2)
			{
				continue;
			}

			auto is_selected_output = areSameOutputDevice(selected_output, sound_device);
			if (sound_device.isActive || (include_selected_inactive && is_selected_output))
			{
				if (!sound_device.isActive && is_selected_output)
				{
					sound_device.isTargetedRealPlaybackDevice = false;
					sound_device.isDefaultDevice = false;
					sound_device.isCaptureDevice = false;
				}

				selected_output_present = selected_output_present || is_selected_output;
				output_devices.push_back(sound_device);
			}
		}

		if (include_selected_inactive &&
			(!selected_output.pwszID.empty() || !selected_output.deviceFriendlyName.empty()) &&
			!selected_output_present &&
			selected_output.deviceNumChannel >= 2)
		{
			auto inactive_selected_output = selected_output;
			inactive_selected_output.isActive = false;
			inactive_selected_output.isTargetedRealPlaybackDevice = false;
			inactive_selected_output.isDefaultDevice = false;
			inactive_selected_output.isCaptureDevice = false;
			output_devices.push_back(inactive_selected_output);
		}

		sortOutputDevicesByPriority(output_devices, priorities);
		return output_devices;
	}

	inline SoundDevice getPreferredOutput(const std::vector<SoundDevice>& output_devices, const std::vector<PriorityEntry>& priorities)
	{
		for (const auto& priority : priorities)
		{
			for (const auto& device : output_devices)
			{
				if ((!priority.device_id.empty() && priority.device_id == device.pwszID) ||
					(!priority.device_name.empty() && priority.device_name == device.deviceFriendlyName))
				{
					return device;
				}
			}
		}

		if (!output_devices.empty())
		{
			return output_devices.front();
		}

		return {};
	}

	inline SoundDevice resolveSelectedOutput(const std::vector<SoundDevice>& output_devices,
		const SoundDevice& selected_output,
		const std::wstring& output_name,
		const std::vector<PriorityEntry>& priorities)
	{
		if (!selected_output.pwszID.empty() || !selected_output.deviceFriendlyName.empty())
		{
			for (const auto& device : output_devices)
			{
				if (areSameOutputDevice(selected_output, device))
				{
					return device;
				}
			}
		}

		if (!output_name.empty())
		{
			for (const auto& device : output_devices)
			{
				if (device.deviceFriendlyName == output_name)
				{
					return device;
				}
			}
		}

		return getPreferredOutput(output_devices, priorities);
	}

	inline bool shouldIgnoreDeviceChange(AudioDeviceChangeKind change_kind,
		const std::wstring& device_id,
		const SoundDevice& selected_output,
		const std::vector<SoundDevice>& sound_devices)
	{
		if (change_kind == AudioDeviceChangeKind::Unknown || device_id.empty())
		{
			return false;
		}

		if (selected_output.pwszID.empty())
		{
			return false;
		}

		auto selected_output_it = std::find_if(sound_devices.begin(), sound_devices.end(),
			[&selected_output](const SoundDevice& sound_device)
			{
				return areSameOutputDevice(selected_output, sound_device);
			});

		if (selected_output_it == sound_devices.end() ||
			!selected_output_it->isActive ||
			!selected_output_it->isTargetedRealPlaybackDevice)
		{
			return false;
		}

		if (device_id == selected_output.pwszID)
		{
			return false;
		}

		return true;
	}

	inline SyncDecision buildSyncDecision(const std::vector<SoundDevice>& output_devices,
		const SoundDevice& selected_output,
		const std::wstring& output_name,
		const std::vector<PriorityEntry>& priorities,
		bool timer_running)
	{
		SyncDecision decision;
		decision.resolved_output = resolveSelectedOutput(output_devices, selected_output, output_name, priorities);
		decision.has_resolved_output = !decision.resolved_output.pwszID.empty();

		if (!decision.has_resolved_output)
		{
			return decision;
		}

		decision.output_changed = selected_output.pwszID != decision.resolved_output.pwszID;
		decision.name_changed = output_name != decision.resolved_output.deviceFriendlyName;
		decision.routing_changed = decision.resolved_output.isActive && !decision.resolved_output.isTargetedRealPlaybackDevice;
		decision.should_apply_routing = timer_running && decision.resolved_output.isActive &&
			(decision.output_changed || decision.routing_changed);
		decision.should_mute = !decision.resolved_output.isActive;
		return decision;
	}
}
