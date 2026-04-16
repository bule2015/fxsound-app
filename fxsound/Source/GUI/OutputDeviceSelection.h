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
	// Pure helpers that resolve output-device state transitions for both runtime code
	// and tests. The controller uses these helpers to decide what to show, select,
	// persist, or re-route without mixing those decisions with UI side effects.
	struct PriorityEntry
	{
		std::wstring device_id;
		std::wstring device_name;
		std::wstring container_id;
	};

	struct PriorityMergeResult
	{
		std::vector<PriorityEntry> priorities;
		bool changed = false;
	};

	struct OutputRoutingActions
	{
		bool should_retarget_playback = false;
		bool should_restart_processing = false;
		bool should_begin_grace_period = false;
	};

	// Result of re-evaluating the selected output while audio processing is running.
	struct SyncDecision
	{
		SoundDevice resolved_output;
		bool has_resolved_output = false;
		bool output_changed = false;
		bool name_changed = false;
		bool routing_changed = false;
		OutputRoutingActions routing_actions;
		bool should_mute = false;
	};

	// Result of a user-driven output change request from the UI.
	struct ManualSelectionDecision
	{
		SoundDevice selected_output;
		bool found_output = false;
		OutputRoutingActions routing_actions;
		bool should_power_off = false;
		bool should_sync_processing_state = false;
	};

	// Result of choosing the startup output before any routing side effects are applied.
	struct InitDecision
	{
		SoundDevice resolved_output;
		bool has_resolved_output = false;
		bool should_apply_output = false;
		bool should_mute = false;
	};

	// Persisted identity for the last selected output so reconnects and restarts can
	// restore the same device even when endpoint ids change.
	struct PersistedOutputState
	{
		std::wstring device_id;
		std::wstring device_name;
		std::wstring container_id;
		std::wstring device_description;
		int device_num_channel = 2;
	};

	// Result of updating the selected output while processing is idle.
	struct IdleSyncDecision
	{
		SoundDevice resolved_output;
		bool has_resolved_output = false;
		bool should_notify_error = false;
	};

	// Decision about whether a device change should auto-apply a device preset.
	struct AutoPresetDecision
	{
		std::wstring preset_name;
		bool should_apply = false;
		bool should_announce = false;
	};

	// Snapshot of the live playback path as reported by the audio backend.
	struct ProcessingDeviceSnapshot
	{
		SoundDevice synced_output;
		bool has_synced_output = false;
		bool dfx_enabled = false;
	};

	// Common output-resolution result shared by startup, live sync, and idle sync.
	struct ResolvedOutputState
	{
		SoundDevice resolved_output;
		bool has_resolved_output = false;
		bool output_changed = false;
		bool name_changed = false;
		bool should_mute = false;
	};

	// Inputs required to resolve a selected output against the current device list.
	struct OutputResolutionContext
	{
		SoundDevice selected_output;
		std::wstring output_name;
		std::vector<PriorityEntry> priorities;
	};

	// Describes the device callback that triggered a refresh so runtime code can
	// opt into more aggressive switching behavior without rewriting saved priorities.
	struct DeviceChangeSelectionContext
	{
		AudioDeviceChangeKind change_kind = AudioDeviceChangeKind::Unknown;
		std::wstring device_id;
		bool prioritize_new_output = false;
		bool changed_output_became_available = false;
		bool automatic_device_switching = false;
	};

	// Shared persisted identity for outputs whose endpoint ids can churn across reconnects.
	struct StoredOutputIdentity
	{
		std::wstring device_id;
		std::wstring device_name;
		std::wstring container_id;
	};

	// Matches a persisted priority entry to a live device using the strongest
	// identifiers first and falling back to the legacy name-only form.
	inline bool matchesStoredOutputIdentity(const StoredOutputIdentity& identity, const SoundDevice& sound_device)
	{
		if (!identity.device_id.empty() && identity.device_id == sound_device.pwszID)
		{
			return true;
		}

		if (!identity.container_id.empty() &&
			!sound_device.containerId.empty() &&
			identity.container_id == sound_device.containerId &&
			!identity.device_name.empty() &&
			!sound_device.deviceFriendlyName.empty() &&
			identity.device_name == sound_device.deviceFriendlyName)
		{
			return true;
		}

		return identity.container_id.empty() &&
			!identity.device_name.empty() &&
			!sound_device.deviceFriendlyName.empty() &&
			identity.device_name == sound_device.deviceFriendlyName;
	}

	inline bool matchesPriorityEntryExactly(const PriorityEntry& entry, const SoundDevice& sound_device)
	{
		return matchesStoredOutputIdentity(
			StoredOutputIdentity { entry.device_id, entry.device_name, entry.container_id },
			sound_device);
	}

	// Compares two outputs across reconnects where endpoint ids may change.
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

	// Used by output preferences to decide whether a preset change belongs to the
	// currently selected output and should therefore be applied immediately.
	using PresetApplyIdentity = StoredOutputIdentity;

	struct ConfiguredPresetRestoreDecision
	{
		bool should_apply = false;
		bool should_clear_stale_configured_preset = false;
		bool should_fallback_to_default_preset = true;
		int preset_index = -1;
	};

	inline bool shouldApplyPresetToSelectedOutput(const PresetApplyIdentity& identity,
		const SoundDevice& selected_output,
		const std::wstring& current_output_name)
	{
		if ((!identity.device_id.empty() || !identity.container_id.empty()) &&
			matchesStoredOutputIdentity(identity, selected_output))
		{
			return true;
		}

		return identity.device_id.empty() &&
			identity.container_id.empty() &&
			!identity.device_name.empty() &&
			identity.device_name == current_output_name;
	}

	inline ConfiguredPresetRestoreDecision buildConfiguredPresetRestoreDecision(
		const std::wstring& configured_preset,
		const std::vector<std::wstring>& preset_names)
	{
		ConfiguredPresetRestoreDecision decision;
		if (configured_preset.empty())
		{
			return decision;
		}

		for (size_t index = 0; index < preset_names.size(); ++index)
		{
			if (preset_names[index] == configured_preset)
			{
				decision.should_apply = true;
				decision.should_fallback_to_default_preset = false;
				decision.preset_index = static_cast<int>(index);
				return decision;
			}
		}

		decision.should_clear_stale_configured_preset = true;
		return decision;
	}

	inline SoundDevice restorePersistedOutput(const PersistedOutputState& persisted_output)
	{
		if (persisted_output.device_id.empty() && persisted_output.device_name.empty())
		{
			return {};
		}

		SoundDevice sound_device;
		sound_device.isRealDevice = true;
		sound_device.pwszID = persisted_output.device_id;
		sound_device.deviceFriendlyName = persisted_output.device_name;
		sound_device.containerId = persisted_output.container_id;
		sound_device.deviceDescription = persisted_output.device_description;
		sound_device.deviceNumChannel = persisted_output.device_num_channel;
		return sound_device;
	}

	inline PersistedOutputState makePersistedOutputState(const SoundDevice& sound_device)
	{
		PersistedOutputState persisted_output;
		persisted_output.device_id = sound_device.pwszID;
		persisted_output.device_name = sound_device.deviceFriendlyName;
		persisted_output.container_id = sound_device.containerId;
		persisted_output.device_description = sound_device.deviceDescription;
		persisted_output.device_num_channel = sound_device.deviceNumChannel;
		return persisted_output;
	}

	// Returns the saved priority index for a live output. Unknown outputs sort after
	// everything already persisted.
	inline int getOutputDevicePriority(const std::vector<PriorityEntry>& priorities, const SoundDevice& sound_device)
	{
		for (int i = 0; i < static_cast<int>(priorities.size()); ++i)
		{
			const auto& entry = priorities[static_cast<size_t>(i)];
			if (matchesPriorityEntryExactly(entry, sound_device))
			{
				return i;
			}
		}

		return static_cast<int>(priorities.size());
	}

	// Builds the initial priority order from the currently known stereo outputs.
	inline std::vector<PriorityEntry> buildInitialOutputPriorities(const std::vector<SoundDevice>& sound_devices)
	{
		std::vector<SoundDevice> sorted_devices = sound_devices;
		std::stable_sort(sorted_devices.begin(), sorted_devices.end(),
			[](const SoundDevice& a, const SoundDevice& b)
			{
				auto priority_a = (a.isDefaultDevice || a.isTargetedRealPlaybackDevice) ? 2 : (a.isActive ? 1 : 0);
				auto priority_b = (b.isDefaultDevice || b.isTargetedRealPlaybackDevice) ? 2 : (b.isActive ? 1 : 0);
				return priority_a > priority_b;
			});

		std::vector<PriorityEntry> priorities;
		for (const auto& sound_device : sorted_devices)
		{
			if (!sound_device.isRealDevice || sound_device.deviceNumChannel < 2)
			{
				continue;
			}

			PriorityEntry entry { sound_device.pwszID, sound_device.deviceFriendlyName, sound_device.containerId };
			auto duplicate = std::find_if(priorities.begin(), priorities.end(),
				[&entry](const PriorityEntry& existing_entry)
				{
					return (!entry.device_id.empty() && existing_entry.device_id == entry.device_id) ||
						(!entry.container_id.empty() &&
						 !existing_entry.container_id.empty() &&
						 entry.container_id == existing_entry.container_id &&
						 !entry.device_name.empty() &&
						 existing_entry.device_name == entry.device_name) ||
						(entry.container_id.empty() &&
						 existing_entry.container_id.empty() &&
						 !entry.device_name.empty() &&
						 existing_entry.device_name == entry.device_name);
				});

			if (duplicate == priorities.end())
			{
				priorities.push_back(entry);
			}
		}

		return priorities;
	}

	// Merges newly discovered devices into the saved priority list while keeping the
	// original ordering stable across reconnects and endpoint id changes.
	inline PriorityMergeResult mergeOutputPriorities(const std::vector<PriorityEntry>& existing_priorities,
		const std::vector<SoundDevice>& sound_devices)
	{
		PriorityMergeResult result;
		result.priorities.reserve(existing_priorities.size());

		auto findMatchingEntry = [&result](const SoundDevice& sound_device)
		{
			return std::find_if(result.priorities.begin(), result.priorities.end(),
				[&sound_device](const PriorityEntry& entry)
				{
					return matchesPriorityEntryExactly(entry, sound_device);
				});
		};

		for (const auto& existing_entry : existing_priorities)
		{
			auto known_device = std::find_if(sound_devices.begin(), sound_devices.end(),
				[&existing_entry](const SoundDevice& sound_device)
				{
					return sound_device.isRealDevice &&
						matchesPriorityEntryExactly(existing_entry, sound_device);
				});

			if (known_device != sound_devices.end() && known_device->deviceNumChannel < 2)
			{
				result.changed = true;
				continue;
			}

			result.priorities.push_back(existing_entry);
		}

		for (const auto& sound_device : sound_devices)
		{
			if (!sound_device.isRealDevice || sound_device.deviceNumChannel < 2)
			{
				continue;
			}

			auto existing_entry = findMatchingEntry(sound_device);

			if (existing_entry == result.priorities.end())
			{
				PriorityEntry entry { sound_device.pwszID, sound_device.deviceFriendlyName, sound_device.containerId };
				result.priorities.push_back(entry);
				result.changed = true;
				continue;
			}

			if (existing_entry->device_id != sound_device.pwszID ||
				existing_entry->device_name != sound_device.deviceFriendlyName ||
				existing_entry->container_id != sound_device.containerId)
			{
				existing_entry->device_id = sound_device.pwszID;
				existing_entry->device_name = sound_device.deviceFriendlyName;
				existing_entry->container_id = sound_device.containerId;
				result.changed = true;
			}
		}

		return result;
	}

	// Applies the saved priority order to the list shown in the UI.
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

	// Builds the visible output list for the UI, optionally keeping the current
	// inactive selection visible so the user can see what went away.
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

	// Chooses the highest-priority active output from the visible list.
	inline SoundDevice getPreferredOutput(const std::vector<SoundDevice>& output_devices, const std::vector<PriorityEntry>& priorities)
	{
		for (const auto& priority : priorities)
		{
			auto exact_match = std::find_if(output_devices.begin(), output_devices.end(),
				[&priority](const SoundDevice& device)
				{
					return matchesPriorityEntryExactly(priority, device);
				});
			if (exact_match != output_devices.end())
			{
				return *exact_match;
			}
		}

		if (!output_devices.empty())
		{
			return output_devices.front();
		}

		return {};
	}

	inline const SoundDevice* findSelectedOutputMatch(const std::vector<SoundDevice>& output_devices, const SoundDevice& selected_output)
	{
		if (selected_output.pwszID.empty() && selected_output.deviceFriendlyName.empty())
		{
			return nullptr;
		}

		auto selected_match = std::find_if(output_devices.begin(), output_devices.end(),
			[&selected_output](const SoundDevice& device)
			{
				return areSameOutputDevice(selected_output, device);
			});
		return selected_match != output_devices.end() ? &(*selected_match) : nullptr;
	}

	inline const SoundDevice* findLegacyOutputNameMatch(const std::vector<SoundDevice>& output_devices, const std::wstring& output_name)
	{
		if (output_name.empty())
		{
			return nullptr;
		}

		auto name_match = std::find_if(output_devices.begin(), output_devices.end(),
			[&output_name](const SoundDevice& device)
			{
				return device.deviceFriendlyName == output_name;
			});
		return name_match != output_devices.end() ? &(*name_match) : nullptr;
	}

	// Resolves the best output candidate by checking the explicit selection first,
	// then the last stored name, then the configured priority order.
	inline SoundDevice resolveSelectedOutput(const std::vector<SoundDevice>& output_devices,
		const OutputResolutionContext& context)
	{
		if (const auto* selected_match = findSelectedOutputMatch(output_devices, context.selected_output))
		{
			return *selected_match;
		}

		if (const auto* name_match = findLegacyOutputNameMatch(output_devices, context.output_name))
		{
			return *name_match;
		}

		return getPreferredOutput(output_devices, context.priorities);
	}

	// Computes the common change flags that later decisions use to decide whether to
	// re-route playback, update settings, or mute the backend.
	inline ResolvedOutputState buildResolvedOutputState(const SoundDevice& resolved_output,
		const OutputResolutionContext& context)
	{
		ResolvedOutputState state;
		state.resolved_output = resolved_output;
		state.has_resolved_output = !resolved_output.pwszID.empty();

		if (!state.has_resolved_output)
		{
			return state;
		}

		state.output_changed = context.selected_output.pwszID != resolved_output.pwszID;
		state.name_changed = context.output_name != resolved_output.deviceFriendlyName;
		state.should_mute = !resolved_output.isActive;
		return state;
	}

	inline OutputResolutionContext makeOutputResolutionContext(const SoundDevice& selected_output,
		const std::wstring& output_name,
		const std::vector<PriorityEntry>& priorities)
	{
		return { selected_output, output_name, priorities };
	}

	// Finds the active playback output that the backend is currently synced to, with
	// the system default as a fallback when the targeted device is not marked yet.
	inline SoundDevice findDefaultProcessingOutput(const std::vector<SoundDevice>& sound_devices)
	{
		SoundDevice default_output;

		for (const auto& sound_device : sound_devices)
		{
			if (!sound_device.isRealDevice || !sound_device.isActive || sound_device.deviceNumChannel < 2)
			{
				continue;
			}

			if (sound_device.isTargetedRealPlaybackDevice ||
				(default_output.pwszID.empty() && sound_device.isDefaultDevice))
			{
				default_output = sound_device;
			}
		}

		return default_output;
	}

	// Decides which output should be restored during startup.
	inline InitDecision buildInitDecision(const std::vector<SoundDevice>& sound_devices,
		const std::vector<SoundDevice>& output_devices,
		const OutputResolutionContext& context)
	{
		InitDecision decision;
		decision.resolved_output = findDefaultProcessingOutput(sound_devices);

		if (const auto* selected_match = findSelectedOutputMatch(output_devices, context.selected_output))
		{
			decision.resolved_output = *selected_match;
		}
		else if (decision.resolved_output.pwszID.empty() &&
			(!context.selected_output.pwszID.empty() || !context.selected_output.deviceFriendlyName.empty()) &&
			context.selected_output.deviceNumChannel >= 2)
		{
			decision.resolved_output = context.selected_output;
		}

		if (decision.resolved_output.pwszID.empty() && !output_devices.empty())
		{
			decision.resolved_output = resolveSelectedOutput(output_devices, context);
		}

		auto resolved_state = buildResolvedOutputState(decision.resolved_output, context);
		decision.has_resolved_output = resolved_state.has_resolved_output;
		decision.should_mute = resolved_state.should_mute;
		if (!resolved_state.has_resolved_output)
		{
			return decision;
		}

		decision.should_apply_output = decision.resolved_output.isActive;
		return decision;
	}

	// Returns true when a device callback can be ignored because it does not affect
	// the currently selected active playback device.
	inline bool didOutputBecomeAvailable(const std::vector<SoundDevice>& previous_output_devices,
		const std::vector<SoundDevice>& current_sound_devices,
		const std::wstring& device_id)
	{
		if (device_id.empty())
		{
			return false;
		}

		auto current_output_it = std::find_if(current_sound_devices.begin(), current_sound_devices.end(),
			[&device_id](const SoundDevice& sound_device)
			{
				return sound_device.isRealDevice &&
					sound_device.isActive &&
					sound_device.deviceNumChannel >= 2 &&
					sound_device.pwszID == device_id;
			});
		if (current_output_it == current_sound_devices.end())
		{
			return false;
		}

		auto previous_output_it = std::find_if(previous_output_devices.begin(), previous_output_devices.end(),
			[&device_id](const SoundDevice& sound_device)
			{
				return sound_device.pwszID == device_id;
			});

		return previous_output_it == previous_output_devices.end() || !previous_output_it->isActive;
	}

	inline bool shouldIgnoreDeviceChange(AudioDeviceChangeKind change_kind,
		const std::wstring& device_id,
		const SoundDevice& selected_output,
		const std::vector<SoundDevice>& sound_devices,
		bool prioritize_new_output = false,
		bool changed_output_became_available = false)
	{
		if (change_kind == AudioDeviceChangeKind::Unknown || device_id.empty())
		{
			return false;
		}

		if (prioritize_new_output && changed_output_became_available)
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

	inline SoundDevice resolveOutputForDeviceChange(const std::vector<SoundDevice>& output_devices,
		const OutputResolutionContext& context,
		const DeviceChangeSelectionContext& device_change)
	{
		if (device_change.prioritize_new_output &&
			device_change.changed_output_became_available &&
			!device_change.device_id.empty())
		{
			auto added_output = std::find_if(output_devices.begin(), output_devices.end(),
				[&device_change](const SoundDevice& sound_device)
				{
					return sound_device.isActive &&
						sound_device.deviceNumChannel >= 2 &&
						sound_device.pwszID == device_change.device_id;
				});
			if (added_output != output_devices.end())
			{
				return *added_output;
			}
		}

		if (device_change.automatic_device_switching &&
			(device_change.change_kind == AudioDeviceChangeKind::DeviceRemoved ||
			 device_change.change_kind == AudioDeviceChangeKind::DeviceStateChanged))
		{
			auto selected_output_it = std::find_if(output_devices.begin(), output_devices.end(),
				[&context](const SoundDevice& sound_device)
				{
					return areSameOutputDevice(context.selected_output, sound_device);
				});
			auto selected_output_active = selected_output_it != output_devices.end() && selected_output_it->isActive;
			if (!selected_output_active)
			{
				std::vector<SoundDevice> active_outputs;
				active_outputs.reserve(output_devices.size());
				for (const auto& output_device : output_devices)
				{
					if (output_device.isActive)
					{
						active_outputs.push_back(output_device);
					}
				}

				if (!active_outputs.empty())
				{
					return getPreferredOutput(active_outputs, context.priorities);
				}
			}
		}

		return resolveSelectedOutput(output_devices, context);
	}

	// Re-evaluates the selected output while processing is idle. This keeps the UI
	// state correct without forcing backend routing changes.
	inline IdleSyncDecision buildIdleSyncDecision(const std::vector<SoundDevice>& output_devices,
		const OutputResolutionContext& context,
		const DeviceChangeSelectionContext& device_change = {})
	{
		IdleSyncDecision decision;
		auto resolved_state = buildResolvedOutputState(
			resolveOutputForDeviceChange(output_devices, context, device_change),
			context);
		decision.resolved_output = resolved_state.resolved_output;
		decision.has_resolved_output = resolved_state.has_resolved_output;

		if (!resolved_state.has_resolved_output)
		{
			return decision;
		}

		decision.should_notify_error = resolved_state.should_mute;
		return decision;
	}

	// Decides whether a device-driven output change should auto-apply a preset.
	inline AutoPresetDecision buildAutoPresetDecision(bool preset_modified,
		bool trigger_change,
		const std::wstring& configured_preset,
		bool power_state)
	{
		AutoPresetDecision decision;
		if (preset_modified || !trigger_change || configured_preset.empty())
		{
			return decision;
		}

		decision.preset_name = configured_preset;
		decision.should_apply = true;
		decision.should_announce = power_state;
		return decision;
	}

	// Extracts the currently targeted playback output and whether the FxSound virtual
	// endpoint is still present in the backend device snapshot.
	inline ProcessingDeviceSnapshot scanProcessingOutputs(const std::vector<SoundDevice>& sound_devices)
	{
		ProcessingDeviceSnapshot snapshot;

		for (const auto& sound_device : sound_devices)
		{
			if (sound_device.isRealDevice)
			{
				if (!sound_device.isActive || sound_device.deviceNumChannel < 2)
				{
					continue;
				}

				if (sound_device.isTargetedRealPlaybackDevice ||
					(!snapshot.has_synced_output && sound_device.isDefaultDevice))
				{
					snapshot.synced_output = sound_device;
					snapshot.has_synced_output = true;
				}

				continue;
			}

			if (sound_device.deviceFriendlyName.find(L"FxSound Audio Enhancer") != std::wstring::npos)
			{
				snapshot.dfx_enabled = true;
			}
		}

		return snapshot;
	}

	// Re-evaluates the selected output while processing is running and determines
	// whether routing must be re-applied.
	inline SyncDecision buildSyncDecision(const std::vector<SoundDevice>& output_devices,
		const OutputResolutionContext& context,
		bool timer_running,
		const DeviceChangeSelectionContext& device_change = {})
	{
		SyncDecision decision;
		auto resolved_state = buildResolvedOutputState(
			resolveOutputForDeviceChange(output_devices, context, device_change),
			context);
		decision.resolved_output = resolved_state.resolved_output;
		decision.has_resolved_output = resolved_state.has_resolved_output;
		decision.output_changed = resolved_state.output_changed;
		decision.name_changed = resolved_state.name_changed;
		decision.should_mute = resolved_state.should_mute;

		if (!resolved_state.has_resolved_output)
		{
			return decision;
		}

		decision.routing_changed = decision.resolved_output.isActive && !decision.resolved_output.isTargetedRealPlaybackDevice;
		decision.routing_actions.should_retarget_playback = timer_running && decision.resolved_output.isActive &&
			(decision.output_changed || decision.routing_changed);
		decision.routing_actions.should_begin_grace_period = decision.routing_actions.should_retarget_playback;
		return decision;
	}

	// Resolves a user-selected output id into routing actions and power-state changes.
	inline ManualSelectionDecision buildManualSelectionDecision(const std::vector<SoundDevice>& sound_devices,
		const std::wstring& output_device_id,
		const SoundDevice& previous_selected_output,
		bool timer_running,
		bool power_state,
		bool playback_device_available)
	{
		ManualSelectionDecision decision;

		for (const auto& sound_device : sound_devices)
		{
			if (!sound_device.isRealDevice || output_device_id != sound_device.pwszID || sound_device.deviceNumChannel < 2)
			{
				continue;
			}

			decision.selected_output = sound_device;
			decision.found_output = true;

			if (!timer_running && sound_device.isDefaultDevice)
			{
				decision.should_sync_processing_state = power_state;
				return decision;
			}

			decision.routing_actions.should_retarget_playback = !sound_device.isTargetedRealPlaybackDevice;
			decision.routing_actions.should_restart_processing =
				timer_running &&
				sound_device.isActive &&
				(!sound_device.isTargetedRealPlaybackDevice ||
					!previous_selected_output.isActive ||
					!playback_device_available);
			decision.routing_actions.should_begin_grace_period =
				power_state &&
				(decision.routing_actions.should_restart_processing || decision.routing_actions.should_retarget_playback);
			decision.should_sync_processing_state = power_state;
			return decision;
		}

		decision.should_power_off = true;
		return decision;
	}
}
