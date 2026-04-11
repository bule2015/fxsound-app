#include <exception>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../../fxsound/Source/GUI/OutputDeviceSelection.h"
#include "../../fxsound/Source/GUI/AudioSignalPolicy.h"
#include "../../fxsound/Source/GUI/PresetAutoSavePolicy.h"
#include "../../fxsound/Source/GUI/StartupOptionPolicy.h"
#include "../../dsp/include/AutoEqPolicy.h"
#include "../../dsp/include/VolumeLevelingQuietPolicy.h"
#include "../../audiopassthru/include/u_AudioPassthru.h"
#include "../../audiopassthru/include/sndDevicesBufferPolicy.h"

namespace
{
using FxSound::OutputDeviceSelection::PriorityEntry;
using FxSound::OutputDeviceSelection::OutputResolutionContext;

struct ScenarioState
{
	SoundDevice selected_output;
	std::wstring output_name;
	bool timer_running = true;
	bool power_state = true;
	bool playback_device_available = true;
	bool muted = false;
	bool restarted_processing = false;
	bool retargeted_playback = false;
	std::vector<SoundDevice> visible_outputs;
};

struct FakeAudioPassthru : IAudioPassthru
{
	std::vector<SoundDevice> sound_devices;
	bool playback_device_available = true;
	bool muted = false;
	int mute_call_count = 0;
	int mute_true_call_count = 0;
	int mute_false_call_count = 0;
	int set_playback_call_count = 0;
	int restart_call_count = 0;
	int set_dsp_processing_call_count = 0;
	std::wstring last_playback_device_id;
	AudioPassthruCallback* callback = nullptr;
	bool processing_thread_running = true;
	uint64_t last_capture_with_samples_tick_ms = 0;
	uint64_t last_successful_playback_tick_ms = 0;
	float last_capture_input_rms_db = -160.0f;
	float last_submitted_playback_rms_db = -160.0f;
	bool dsp_processing_enabled = true;

	int init(bool = false) override
	{
		return 0;
	}

	void mute(bool mute_value) override
	{
		muted = mute_value;
		++mute_call_count;
		if (mute_value)
		{
			++mute_true_call_count;
		}
		else
		{
			++mute_false_call_count;
		}
	}

	std::vector<SoundDevice> getSoundDevices(bool active_devices = true) override
	{
		if (!active_devices)
		{
			return sound_devices;
		}

		std::vector<SoundDevice> active_sound_devices;
		for (const auto& sound_device : sound_devices)
		{
			if (sound_device.isActive)
			{
				active_sound_devices.push_back(sound_device);
			}
		}

		return active_sound_devices;
	}

	int setBufferLength(int) override
	{
		return 0;
	}

	int processTimer() override
	{
		return 0;
	}

	void setDspProcessingModule(DfxDsp*) override
	{
	}

	void setDspProcessingEnabled(bool enabled) override
	{
		dsp_processing_enabled = enabled;
		++set_dsp_processing_call_count;
	}

	void setAsPlaybackDevice(const SoundDevice sound_device) override
	{
		last_playback_device_id = sound_device.pwszID;
		++set_playback_call_count;

		for (auto& device : sound_devices)
		{
			if (device.isRealDevice)
			{
				device.isTargetedRealPlaybackDevice = (device.pwszID == sound_device.pwszID);
			}
		}

		playback_device_available = sound_device.isActive;
	}

	void registerCallback(AudioPassthruCallback* new_callback) override
	{
		callback = new_callback;
	}

	bool isPlaybackDeviceAvailable() override
	{
		return playback_device_available;
	}

	void restoreDefaultPlaybackDevice() override
	{
	}

	bool restartProcessingForDeviceChange() override
	{
		++restart_call_count;

		auto targeted_device = std::find_if(sound_devices.begin(), sound_devices.end(),
			[](const SoundDevice& sound_device)
			{
				return sound_device.isTargetedRealPlaybackDevice;
			});

		playback_device_available = targeted_device != sound_devices.end() && targeted_device->isActive;
		return playback_device_available;
	}

	bool isProcessingThreadRunning() override
	{
		return processing_thread_running;
	}

	bool isMuted() override
	{
		return muted;
	}

	uint64_t getLastCaptureWithSamplesTickMs() override
	{
		return last_capture_with_samples_tick_ms;
	}

	uint64_t getLastSuccessfulPlaybackTickMs() override
	{
		return last_successful_playback_tick_ms;
	}

	float getLastCaptureInputRmsDb() override
	{
		return last_capture_input_rms_db;
	}

	float getLastSubmittedPlaybackRmsDb() override
	{
		return last_submitted_playback_rms_db;
	}
};

struct RuntimeHarness
{
	ScenarioState state;
	FakeAudioPassthru audio;
	std::vector<PriorityEntry> priorities;
};

void applyRuntimeManualSelection(RuntimeHarness& harness, const std::wstring& output_device_id);

SoundDevice makeOutput(const wchar_t* id,
	const wchar_t* name,
	const wchar_t* description,
	bool is_active,
	bool is_default_device,
	bool is_targeted,
	const wchar_t* container_id = L"")
{
	SoundDevice sound_device;
	sound_device.isRealDevice = true;
	sound_device.isActive = is_active;
	sound_device.isDefaultDevice = is_default_device;
	sound_device.isTargetedRealPlaybackDevice = is_targeted;
	sound_device.deviceNumChannel = 2;
	sound_device.pwszID = id;
	sound_device.deviceFriendlyName = name;
	sound_device.deviceDescription = description;
	sound_device.containerId = container_id;
	return sound_device;
}

OutputResolutionContext makeTestOutputResolutionContext(const SoundDevice& selected_output,
	const std::wstring& output_name,
	const std::vector<PriorityEntry>& priorities)
{
	return FxSound::OutputDeviceSelection::makeOutputResolutionContext(selected_output, output_name, priorities);
}

OutputResolutionContext makeTestOutputResolutionContext(const ScenarioState& state,
	const std::vector<PriorityEntry>& priorities)
{
	return makeTestOutputResolutionContext(state.selected_output, state.output_name, priorities);
}

void expect(bool condition, const std::string& message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}

void resetScenarioActions(ScenarioState& state)
{
	state.restarted_processing = false;
	state.retargeted_playback = false;
}

void resetAudioActions(FakeAudioPassthru& audio)
{
	audio.mute_call_count = 0;
	audio.mute_true_call_count = 0;
	audio.mute_false_call_count = 0;
	audio.set_playback_call_count = 0;
	audio.restart_call_count = 0;
	audio.last_playback_device_id.clear();
}

void applyRefresh(ScenarioState& state,
	const std::vector<SoundDevice>& sound_devices,
	const std::vector<PriorityEntry>& priorities,
	bool include_selected_inactive = true)
{
	state.visible_outputs = FxSound::OutputDeviceSelection::buildVisibleOutputDevices(
		sound_devices,
		state.selected_output,
		priorities,
		include_selected_inactive);

	auto decision = FxSound::OutputDeviceSelection::buildSyncDecision(
		state.visible_outputs,
		makeTestOutputResolutionContext(state, priorities),
		state.timer_running);

	if (!decision.has_resolved_output)
	{
		return;
	}

	state.selected_output = decision.resolved_output;
	state.output_name = decision.resolved_output.deviceFriendlyName;
	state.retargeted_playback = state.retargeted_playback || decision.routing_actions.should_retarget_playback;

	if (decision.should_mute)
	{
		state.playback_device_available = false;
		state.muted = true;
	}
	else
	{
		state.playback_device_available = true;
		state.muted = false;
	}
}

void applyDeviceChange(ScenarioState& state,
	AudioDeviceChangeKind change_kind,
	const std::wstring& device_id,
	const std::vector<SoundDevice>& sound_devices,
	const std::vector<PriorityEntry>& priorities)
{
	resetScenarioActions(state);

	auto ignored = FxSound::OutputDeviceSelection::shouldIgnoreDeviceChange(
		change_kind,
		device_id,
		state.selected_output,
		sound_devices);

	if (!ignored)
	{
		state.restarted_processing = true;
	}

	applyRefresh(state, sound_devices, priorities);
}

void applyManualSelection(ScenarioState& state,
	const std::wstring& output_device_id,
	const std::vector<SoundDevice>& sound_devices)
{
	resetScenarioActions(state);

	auto decision = FxSound::OutputDeviceSelection::buildManualSelectionDecision(
		sound_devices,
		output_device_id,
		state.selected_output,
		state.timer_running,
		state.power_state,
		state.playback_device_available);

	if (!decision.found_output)
	{
		state.power_state = false;
		state.playback_device_available = false;
		state.muted = true;
		return;
	}

	state.selected_output = decision.selected_output;
	state.output_name = decision.selected_output.deviceFriendlyName;
	state.retargeted_playback = decision.routing_actions.should_retarget_playback;
	state.restarted_processing = decision.routing_actions.should_restart_processing;

	if (decision.should_sync_processing_state)
	{
		state.playback_device_available = decision.selected_output.isActive;
		state.muted = !state.playback_device_available;
	}
}

void refreshRuntime(RuntimeHarness& harness, bool include_selected_inactive = true)
{
	harness.state.visible_outputs = FxSound::OutputDeviceSelection::buildVisibleOutputDevices(
		harness.audio.getSoundDevices(false),
		harness.state.selected_output,
		harness.priorities,
		include_selected_inactive);

	auto decision = FxSound::OutputDeviceSelection::buildSyncDecision(
		harness.state.visible_outputs,
		makeTestOutputResolutionContext(harness.state, harness.priorities),
		harness.state.timer_running);

	if (!decision.has_resolved_output)
	{
		return;
	}

	harness.state.selected_output = decision.resolved_output;
	harness.state.output_name = decision.resolved_output.deviceFriendlyName;

	if (decision.routing_actions.should_retarget_playback)
	{
		harness.audio.setAsPlaybackDevice(decision.resolved_output);
		harness.state.retargeted_playback = true;
	}

	if (decision.should_mute)
	{
		harness.audio.mute(true);
		harness.state.playback_device_available = false;
		harness.state.muted = true;
	}
	else
	{
		harness.state.playback_device_available = harness.audio.isPlaybackDeviceAvailable();
		harness.state.muted = harness.audio.muted;
	}
}

void applyRuntimeStartup(RuntimeHarness& harness)
{
	resetScenarioActions(harness.state);
	resetAudioActions(harness.audio);

	harness.state.visible_outputs = FxSound::OutputDeviceSelection::buildVisibleOutputDevices(
		harness.audio.getSoundDevices(false),
		harness.state.selected_output,
		harness.priorities,
		true);

	auto init_decision = FxSound::OutputDeviceSelection::buildInitDecision(
		harness.audio.getSoundDevices(false),
		harness.state.visible_outputs,
		makeTestOutputResolutionContext(harness.state, harness.priorities));

	if (!init_decision.has_resolved_output)
	{
		return;
	}

	harness.state.selected_output = init_decision.resolved_output;
	harness.state.output_name = init_decision.resolved_output.deviceFriendlyName;

	if (init_decision.should_apply_output)
	{
		applyRuntimeManualSelection(harness, init_decision.resolved_output.pwszID);
	}
	else if (init_decision.should_mute)
	{
		harness.state.playback_device_available = false;
		harness.audio.mute(true);
		harness.state.muted = true;
	}
}

void applyRuntimeIdleSync(RuntimeHarness& harness)
{
	resetScenarioActions(harness.state);
	resetAudioActions(harness.audio);

	harness.state.visible_outputs = FxSound::OutputDeviceSelection::buildVisibleOutputDevices(
		harness.audio.getSoundDevices(false),
		harness.state.selected_output,
		harness.priorities,
		true);

	auto decision = FxSound::OutputDeviceSelection::buildIdleSyncDecision(
		harness.state.visible_outputs,
		makeTestOutputResolutionContext(harness.state, harness.priorities));

	if (!decision.has_resolved_output)
	{
		return;
	}

	harness.state.selected_output = decision.resolved_output;
	harness.state.output_name = decision.resolved_output.deviceFriendlyName;
	harness.state.playback_device_available = !decision.should_notify_error;
	if (decision.should_notify_error)
	{
		harness.state.muted = true;
	}
}

void applyRuntimeDeviceChange(RuntimeHarness& harness,
	AudioDeviceChangeKind change_kind,
	const std::wstring& device_id)
{
	resetScenarioActions(harness.state);
	resetAudioActions(harness.audio);

	auto sound_devices = harness.audio.getSoundDevices(false);
	auto ignored = FxSound::OutputDeviceSelection::shouldIgnoreDeviceChange(
		change_kind,
		device_id,
		harness.state.selected_output,
		sound_devices);

	if (ignored)
	{
		refreshRuntime(harness);
		return;
	}

	harness.audio.restartProcessingForDeviceChange();
	harness.state.restarted_processing = true;
	refreshRuntime(harness);

	if (harness.state.power_state)
	{
		harness.audio.mute(false);
		harness.state.muted = false;
	}

	auto refreshed_selected_output = harness.state.selected_output;
	auto refreshed_selected_output_it = std::find_if(sound_devices.begin(), sound_devices.end(),
		[&refreshed_selected_output](const SoundDevice& sound_device)
		{
			return FxSound::OutputDeviceSelection::areSameOutputDevice(refreshed_selected_output, sound_device);
		});

	if (refreshed_selected_output_it == sound_devices.end() || !refreshed_selected_output_it->isActive)
	{
		harness.state.playback_device_available = false;
		harness.audio.mute(true);
		harness.state.muted = true;
	}
	else
	{
		harness.state.playback_device_available = harness.audio.isPlaybackDeviceAvailable();
		harness.state.muted = harness.audio.muted;
	}
}

void applyRuntimeManualSelection(RuntimeHarness& harness, const std::wstring& output_device_id)
{
	resetScenarioActions(harness.state);
	resetAudioActions(harness.audio);

	auto decision = FxSound::OutputDeviceSelection::buildManualSelectionDecision(
		harness.audio.getSoundDevices(),
		output_device_id,
		harness.state.selected_output,
		harness.state.timer_running,
		harness.state.power_state,
		harness.audio.isPlaybackDeviceAvailable());

	if (!decision.found_output)
	{
		harness.audio.mute(true);
		harness.state.playback_device_available = false;
		harness.state.muted = true;
		harness.state.power_state = false;
		return;
	}

	harness.state.selected_output = decision.selected_output;
	harness.state.output_name = decision.selected_output.deviceFriendlyName;

	if (decision.routing_actions.should_retarget_playback)
	{
		harness.audio.setAsPlaybackDevice(decision.selected_output);
		harness.state.retargeted_playback = true;
	}

	if (decision.routing_actions.should_restart_processing)
	{
		harness.audio.restartProcessingForDeviceChange();
		harness.state.restarted_processing = true;
	}

	if (decision.should_sync_processing_state)
	{
		harness.state.playback_device_available = harness.audio.isPlaybackDeviceAvailable();
		harness.audio.mute(!harness.state.playback_device_available);
		harness.state.muted = !harness.state.playback_device_available;
	}
}

void testBuildVisibleOutputsKeepsSelectedInactive()
{
	std::vector<SoundDevice> sound_devices {
		makeOutput(L"spk", L"Speakers", L"Built-in", true, true, true, L"c-spk"),
		makeOutput(L"hdmi", L"Monitor", L"HDMI", true, false, false, L"c-hdmi")
	};
	SoundDevice selected_output = makeOutput(L"dac-old", L"USB DAC", L"USB Audio", false, false, false, L"c-dac");
	std::vector<PriorityEntry> priorities {
		{L"dac-old", L"USB DAC"},
		{L"spk", L"Speakers"},
		{L"hdmi", L"Monitor"}
	};

	auto output_devices = FxSound::OutputDeviceSelection::buildVisibleOutputDevices(
		sound_devices,
		selected_output,
		priorities,
		true);

	expect(output_devices.size() == 3, "selected inactive output should stay visible");
	expect(output_devices[0].deviceFriendlyName == L"USB DAC", "priority order should keep selected inactive output first");
	expect(!output_devices[0].isActive, "selected inactive output should remain inactive");
}

void testBuildVisibleOutputsDropsUnselectedInactive()
{
	std::vector<SoundDevice> sound_devices {
		makeOutput(L"spk", L"Speakers", L"Built-in", true, true, true, L"c-spk"),
		makeOutput(L"hdmi", L"Monitor", L"HDMI", false, false, false, L"c-hdmi")
	};
	SoundDevice selected_output = makeOutput(L"spk", L"Speakers", L"Built-in", true, true, true, L"c-spk");
	std::vector<PriorityEntry> priorities {
		{L"spk", L"Speakers"},
		{L"hdmi", L"Monitor"}
	};

	auto output_devices = FxSound::OutputDeviceSelection::buildVisibleOutputDevices(
		sound_devices,
		selected_output,
		priorities,
		true);

	expect(output_devices.size() == 1, "unselected inactive output should be hidden");
	expect(output_devices[0].deviceFriendlyName == L"Speakers", "active selected output should remain visible");
}

void testRestorePersistedOutputReturnsEmptyWhenStateIsEmpty()
{
	auto sound_device = FxSound::OutputDeviceSelection::restorePersistedOutput({});
	expect(sound_device.pwszID.empty(), "empty persisted output should restore as an empty sound device");
	expect(sound_device.deviceFriendlyName.empty(), "empty persisted output should not set a device name");
}

void testPersistedOutputRoundTripPreservesIdentity()
{
	auto original_output = makeOutput(L"dac", L"USB DAC", L"USB Audio", false, false, false, L"c-dac");
	original_output.deviceNumChannel = 6;

	auto persisted_output = FxSound::OutputDeviceSelection::makePersistedOutputState(original_output);
	auto restored_output = FxSound::OutputDeviceSelection::restorePersistedOutput(persisted_output);

	expect(restored_output.isRealDevice, "restored persisted output should be marked as a real device");
	expect(restored_output.pwszID == L"dac", "restored persisted output should preserve the endpoint id");
	expect(restored_output.deviceFriendlyName == L"USB DAC", "restored persisted output should preserve the device name");
	expect(restored_output.containerId == L"c-dac", "restored persisted output should preserve the container id");
	expect(restored_output.deviceDescription == L"USB Audio", "restored persisted output should preserve the description");
	expect(restored_output.deviceNumChannel == 6, "restored persisted output should preserve the channel count");
}

void testAutoPresetDecisionAppliesWhenTriggered()
{
	auto decision = FxSound::OutputDeviceSelection::buildAutoPresetDecision(
		false,
		true,
		L"General",
		true);

	expect(decision.should_apply, "auto preset should apply when the output change is eligible");
	expect(decision.should_announce, "auto preset should announce when power is on");
	expect(decision.preset_name == L"General", "auto preset should preserve the configured preset name");
}

void testAutoPresetDecisionSkipsModifiedPreset()
{
	auto decision = FxSound::OutputDeviceSelection::buildAutoPresetDecision(
		true,
		true,
		L"General",
		true);

	expect(!decision.should_apply, "auto preset should not apply when the current preset is modified");
}

void testAutoPresetDecisionSkipsWhenNotTriggered()
{
	auto decision = FxSound::OutputDeviceSelection::buildAutoPresetDecision(
		false,
		false,
		L"General",
		true);

	expect(!decision.should_apply, "auto preset should not apply when the output change did not trigger it");
}

void testAutoPresetDecisionSkipsEmptyPreset()
{
	auto decision = FxSound::OutputDeviceSelection::buildAutoPresetDecision(
		false,
		true,
		L"",
		true);

	expect(!decision.should_apply, "auto preset should not apply when no preset is configured");
}

void testAutoEqPolicyResetsAnalysisAfterPresetLoad()
{
	expect(FxSound::AutoEqPolicy::shouldResetAnalysisState(FxSound::AutoEqPolicy::Change::PresetLoaded),
		"preset loads should reset auto eq analysis state");
}

void testAutoEqPolicyResetsAnalysisAfterFilterQChange()
{
	expect(FxSound::AutoEqPolicy::shouldResetAnalysisState(FxSound::AutoEqPolicy::Change::FilterQChanged),
		"filter Q changes should reset auto eq analysis state");
}

void testAutoEqPolicyResetsAnalysisAfterBandCountChange()
{
	expect(FxSound::AutoEqPolicy::shouldResetAnalysisState(FxSound::AutoEqPolicy::Change::BandCountChanged),
		"band count changes should reset auto eq analysis state");
}

void testAutoEqPolicyResetsAnalysisAfterBandFrequencyChange()
{
	expect(FxSound::AutoEqPolicy::shouldResetAnalysisState(FxSound::AutoEqPolicy::Change::BandFrequencyChanged),
		"band frequency changes should reset auto eq analysis state");
}

void testAutoEqPolicyDisablesAfterManualBandGainEdit()
{
	expect(FxSound::AutoEqPolicy::shouldDisablePreservingCurrentEq(FxSound::AutoEqPolicy::Change::ManualBandGainChanged),
		"manual band gain edits should disable auto eq while preserving the current curve");
}

void testAutoEqPolicyDisablesAfterManualBandFrequencyEdit()
{
	expect(FxSound::AutoEqPolicy::shouldDisablePreservingCurrentEq(FxSound::AutoEqPolicy::Change::ManualBandFrequencyChanged),
		"manual band frequency edits should disable auto eq while preserving the current curve");
}

void testPresetSwitchDecisionAutoSavesModifiedCurrentPreset()
{
	auto decision = FxSound::PresetAutoSavePolicy::buildPresetSwitchDecision(
		true,
		1,
		2,
		false);

	expect(decision.should_auto_save_current,
		"switching away from a modified preset should autosave the current preset");
	expect(!decision.should_load_auto_saved_preset,
		"switching to a preset without an autosave file should keep loading the original preset");
}

void testPresetSwitchDecisionSkipsAutoSaveWhenSelectionDoesNotChange()
{
	auto decision = FxSound::PresetAutoSavePolicy::buildPresetSwitchDecision(
		true,
		2,
		2,
		true);

	expect(!decision.should_auto_save_current,
		"re-selecting the current preset should not autosave it again");
	expect(decision.should_load_auto_saved_preset,
		"loading the current preset should still prefer its autosave copy when one exists");
	expect(decision.should_mark_loaded_preset_modified,
		"loading an autosave copy should keep the preset marked modified");
}

void testAutoSaveCleanupKeepsCaseInsensitivePresetMatch()
{
	std::vector<std::wstring> preset_names {
		L"General",
		L"Movies"
	};

	expect(FxSound::PresetAutoSavePolicy::shouldKeepAutoSavedPreset(L"general", preset_names),
		"autosave cleanup should keep autosave files that match presets case-insensitively");
}

void testAutoSaveCleanupDropsUnknownPreset()
{
	std::vector<std::wstring> preset_names {
		L"General",
		L"Movies"
	};

	expect(!FxSound::PresetAutoSavePolicy::shouldKeepAutoSavedPreset(L"Podcast", preset_names),
		"autosave cleanup should remove autosave files that no longer match any preset");
}

void testStartupOptionPolicyFindsExactOutputLatencyFlag()
{
	const std::vector<std::wstring> arguments {
		L"--preset",
		L"General",
		L"--measure-output-latency"
	};

	expect(FxSound::StartupOptionPolicy::shouldEnableOutputLatencyLogging(arguments),
		"startup option policy should enable output latency logging when the exact flag is present");
}

void testStartupOptionPolicyIgnoresSimilarOutputLatencyFlags()
{
	const std::vector<std::wstring> arguments {
		L"--measure-output-latency-extra",
		L"--preset",
		L"General"
	};

	expect(!FxSound::StartupOptionPolicy::shouldEnableOutputLatencyLogging(arguments),
		"startup option policy should ignore similar but non-exact latency logging flags");
}

void testPresetApplyRequiresIdsToBeMissingBeforeUsingNameFallback()
{
	auto selected_output = makeOutput(L"usb-selected", L"USB DAC", L"USB Audio", true, false, true, L"container-selected");

	auto should_apply = FxSound::OutputDeviceSelection::shouldApplyPresetToSelectedOutput(
		{L"usb-other", L"USB DAC", L"container-other"},
		selected_output,
		L"USB DAC");

	expect(!should_apply,
		"preset application should not fall back to the current output name when a different device identity is present");
}

void testPresetApplyUsesNameFallbackOnlyForLegacyEntries()
{
	auto selected_output = makeOutput(L"usb-selected", L"USB DAC", L"USB Audio", true, false, true, L"container-selected");

	auto should_apply = FxSound::OutputDeviceSelection::shouldApplyPresetToSelectedOutput(
		{L"", L"USB DAC", L""},
		selected_output,
		L"USB DAC");

	expect(should_apply,
		"preset application should still support legacy entries that only store the device name");
}

void testConfiguredPresetRestoreDecisionAppliesExistingPreset()
{
	std::vector<std::wstring> preset_names {
		L"General",
		L"Movies",
		L"Music"
	};

	auto decision = FxSound::OutputDeviceSelection::buildConfiguredPresetRestoreDecision(
		L"Movies",
		preset_names);

	expect(decision.should_apply,
		"configured preset restore should apply when the configured preset still exists");
	expect(decision.preset_index == 1,
		"configured preset restore should return the matching preset index");
	expect(!decision.should_clear_stale_configured_preset,
		"configured preset restore should not clear a valid preset mapping");
	expect(!decision.should_fallback_to_default_preset,
		"configured preset restore should skip the default fallback when the preset exists");
}

void testConfiguredPresetRestoreDecisionClearsMissingPreset()
{
	std::vector<std::wstring> preset_names {
		L"General",
		L"Music"
	};

	auto decision = FxSound::OutputDeviceSelection::buildConfiguredPresetRestoreDecision(
		L"Movies",
		preset_names);

	expect(!decision.should_apply,
		"configured preset restore should not apply when the preset has been deleted");
	expect(decision.should_clear_stale_configured_preset,
		"configured preset restore should clear stale preset names from device config");
	expect(decision.should_fallback_to_default_preset,
		"configured preset restore should fall back to the default preset when the configured preset is missing");
}

void testConfiguredPresetRestoreDecisionFallsBackWhenPresetIsUnset()
{
	std::vector<std::wstring> preset_names {
		L"General",
		L"Movies"
	};

	auto decision = FxSound::OutputDeviceSelection::buildConfiguredPresetRestoreDecision(
		L"",
		preset_names);

	expect(!decision.should_apply,
		"configured preset restore should not apply when no preset is configured");
	expect(!decision.should_clear_stale_configured_preset,
		"configured preset restore should not clear anything when the device has no configured preset");
	expect(decision.should_fallback_to_default_preset,
		"configured preset restore should fall back to the default preset when nothing is configured");
}

void testAudioPassthruCleanupContinuesAfterRestoreFailure()
{
	expect(FxSound::AudioPassthruLifecycle::shouldContinueCleanupAfterThreadShutdown(true, false),
		"successful thread shutdown should allow destructor cleanup to continue");
}

void testAudioPassthruCleanupStopsAfterThreadShutdownTimeout()
{
	expect(!FxSound::AudioPassthruLifecycle::shouldContinueCleanupAfterThreadShutdown(true, true),
		"timed out thread shutdown should still abort the remaining teardown");
}

void testBufferPolicyMigratesLegacyMachineDefaultToLowLatencyDefault()
{
	expect(FxSound::SndDevicesBufferPolicy::resolveEffectiveDefaultBufferSize(true, 40)
			== SND_DEVICES_CAPTURE_BUFFER_DEFAULT_SIZE_MILLI_SECS,
		"legacy machine default buffer sizes should migrate to the low-latency default");
}

void testBufferPolicyPreservesExplicitModernMachineDefault()
{
	expect(FxSound::SndDevicesBufferPolicy::resolveEffectiveDefaultBufferSize(true, 25) == 25,
		"non-legacy machine defaults should be preserved");
}

void testBufferPolicyFallsBackWhenMachineDefaultMissing()
{
	expect(FxSound::SndDevicesBufferPolicy::resolveEffectiveDefaultBufferSize(false, 0)
			== SND_DEVICES_CAPTURE_BUFFER_DEFAULT_SIZE_MILLI_SECS,
		"missing machine defaults should fall back to the low-latency default");
}

void testBufferPolicyClampsOutOfRangeValues()
{
	expect(FxSound::SndDevicesBufferPolicy::clampBufferSizeOrDefault(500)
			== SND_DEVICES_CAPTURE_BUFFER_DEFAULT_SIZE_MILLI_SECS,
		"out-of-range buffer sizes should clamp back to the default");
}

void testQuietFloorRetainsBoostAfterProlongedLowOutput()
{
	auto result = FxSound::VolumeLevelingQuietPolicy::applyTransition({
		0.01f,
		0.02f,
		10.0f,
		0.5f,
		4.0f,
		0.2f,
		1.0f,
		1.0f,
		0.0f,
		0.0f,
		false
	});

	expect(std::fabs(result.quiet_duration_after - 10.5f) < 1.0e-6f, "quiet duration should keep accumulating while output stays very quiet");
	expect(std::fabs(result.quiet_gain_floor_after - 4.0f) < 1.0e-6f, "quiet floor should retain the gain reached by prolonged quiet boosting");
}

void testQuietFloorRaiseStopsNearCeiling()
{
	auto result = FxSound::VolumeLevelingQuietPolicy::applyTransition({
		0.01f,
		0.03f,
		12.0f,
		0.5f,
		4.0f,
		0.2f,
		1.0f,
		4.0f,
		0.99f,
		0.0f,
		true
	});

	expect(std::fabs(result.quiet_gain_floor_after - 4.0f) < 1.0e-6f, "quiet floor should stay unchanged when the 30 second peak window has already reached the ceiling target");
}

void testQuietFloorReleaseAndSilenceDecayWork()
{
	auto release_result = FxSound::VolumeLevelingQuietPolicy::applyTransition({
		0.01f,
		0.08f,
		12.0f,
		0.5f,
		1.0f,
		0.2f,
		1.0f,
		4.0f,
		0.5f,
		0.0f,
		false
	});

	auto silence_result = FxSound::VolumeLevelingQuietPolicy::applyTransition({
		0.001f,
		0.01f,
		12.0f,
		0.5f,
		1.0f,
		0.2f,
		1.0f,
		4.0f,
		0.0f,
		0.0f,
		false
	});

	expect(release_result.quiet_gain_floor_after < 4.0f, "release decay should reduce the retained quiet floor");
	expect(silence_result.quiet_gain_floor_after < 4.0f, "silence decay should reduce the retained quiet floor");
}

void testScanProcessingOutputsPrefersTargetedOutput()
{
	std::vector<SoundDevice> sound_devices {
		makeOutput(L"spk", L"Speakers", L"Built-in", true, true, false, L"c-spk"),
		makeOutput(L"usb", L"USB DAC", L"USB Audio", true, false, true, L"c-usb")
	};

	auto snapshot = FxSound::OutputDeviceSelection::scanProcessingOutputs(sound_devices);

	expect(!snapshot.dfx_enabled, "processing scan should not report dfx enabled without the virtual endpoint");
	expect(snapshot.has_synced_output, "processing scan should resolve an output when an active real device exists");
	expect(snapshot.synced_output.pwszID == L"usb", "processing scan should prefer the targeted playback device");
}

void testScanProcessingOutputsSkipsMonoDefaultWithoutFallback()
{
	auto mono_default = makeOutput(L"mono", L"Headset Chat", L"Chat", true, true, false, L"c-chat");
	mono_default.deviceNumChannel = 1;

	std::vector<SoundDevice> sound_devices {
		mono_default,
		makeOutput(L"spk", L"Speakers", L"Built-in", true, false, false, L"c-spk")
	};

	auto snapshot = FxSound::OutputDeviceSelection::scanProcessingOutputs(sound_devices);

	expect(!snapshot.has_synced_output, "processing scan should skip mono defaults when no targeted or default stereo output exists");
}

void testScanProcessingOutputsDetectsDfxEndpoint()
{
	SoundDevice dfx_device;
	dfx_device.isRealDevice = false;
	dfx_device.deviceFriendlyName = L"FxSound Audio Enhancer";

	std::vector<SoundDevice> sound_devices {
		makeOutput(L"spk", L"Speakers", L"Built-in", true, true, true, L"c-spk"),
		dfx_device
	};

	auto snapshot = FxSound::OutputDeviceSelection::scanProcessingOutputs(sound_devices);

	expect(snapshot.dfx_enabled, "processing scan should detect the FxSound virtual endpoint");
	expect(snapshot.has_synced_output, "processing scan should keep the real output alongside the virtual endpoint");
}

void testBuildInitialOutputPrioritiesKeepsDefaultFirst()
{
	std::vector<SoundDevice> sound_devices {
		makeOutput(L"hdmi", L"Monitor", L"HDMI", true, false, false, L"c-hdmi"),
		makeOutput(L"spk", L"Speakers", L"Built-in", true, true, true, L"c-spk"),
		makeOutput(L"usb", L"USB DAC", L"USB Audio", false, false, false, L"c-usb")
	};

	auto priorities = FxSound::OutputDeviceSelection::buildInitialOutputPriorities(sound_devices);

	expect(priorities.size() == 3, "initial priorities should include each real output once");
	expect(priorities[0].device_id == L"spk", "default or targeted output should lead the initial priority order");
}

void testBuildInitialOutputPrioritiesDropsDuplicatesByName()
{
	std::vector<SoundDevice> sound_devices {
		makeOutput(L"dac-old", L"USB DAC", L"USB Audio", true, false, false, L"c-dac"),
		makeOutput(L"dac-new", L"USB DAC", L"USB Audio", true, false, false, L"c-dac")
	};

	auto priorities = FxSound::OutputDeviceSelection::buildInitialOutputPriorities(sound_devices);

	expect(priorities.size() == 1, "initial priorities should not duplicate the same device name");
	expect(priorities[0].device_name == L"USB DAC", "initial priorities should preserve the device name");
}

void testBuildInitialOutputPrioritiesKeepsSameNameDifferentContainers()
{
	std::vector<SoundDevice> sound_devices {
		makeOutput(L"dac-a", L"USB DAC", L"USB Audio", true, false, false, L"c-dac-a"),
		makeOutput(L"dac-b", L"USB DAC", L"USB Audio", true, false, false, L"c-dac-b")
	};

	auto priorities = FxSound::OutputDeviceSelection::buildInitialOutputPriorities(sound_devices);

	expect(priorities.size() == 2, "initial priorities should keep same-name devices from different containers");
	expect(priorities[0].container_id != priorities[1].container_id, "priority entries should preserve distinct container ids");
}

void testBuildInitialOutputPrioritiesSkipsMonoDevices()
{
	auto mono_output = makeOutput(L"chat", L"Headset Chat", L"Chat", true, false, false, L"c-chat");
	mono_output.deviceNumChannel = 1;

	std::vector<SoundDevice> sound_devices {
		mono_output,
		makeOutput(L"spk", L"Speakers", L"Built-in", true, true, true, L"c-spk")
	};

	auto priorities = FxSound::OutputDeviceSelection::buildInitialOutputPriorities(sound_devices);

	expect(priorities.size() == 1, "initial priorities should skip mono outputs");
	expect(priorities[0].device_id == L"spk", "initial priorities should keep stereo outputs only");
}

void testMergeOutputPrioritiesAppendsNewOutputs()
{
	std::vector<FxSound::OutputDeviceSelection::PriorityEntry> existing_priorities {
		{L"spk", L"Speakers"}
	};
	std::vector<SoundDevice> sound_devices {
		makeOutput(L"spk", L"Speakers", L"Built-in", true, true, true, L"c-spk"),
		makeOutput(L"hdmi", L"Monitor", L"HDMI", true, false, false, L"c-hdmi")
	};

	auto merge_result = FxSound::OutputDeviceSelection::mergeOutputPriorities(existing_priorities, sound_devices);

	expect(merge_result.changed, "merge should report changes when a new output appears");
	expect(merge_result.priorities.size() == 2, "merge should append newly discovered outputs");
	expect(merge_result.priorities[1].device_id == L"hdmi", "merge should append the new output at the end");
}

void testMergeOutputPrioritiesPrependsNewOutputsWhenPrioritized()
{
	std::vector<FxSound::OutputDeviceSelection::PriorityEntry> existing_priorities {
		{L"spk", L"Speakers"},
		{L"dac", L"USB DAC"}
	};
	std::vector<SoundDevice> sound_devices {
		makeOutput(L"spk", L"Speakers", L"Built-in", true, true, true, L"c-spk"),
		makeOutput(L"hdmi", L"Monitor", L"HDMI", true, false, false, L"c-hdmi"),
		makeOutput(L"dac", L"USB DAC", L"USB Audio", true, false, false, L"c-dac"),
		makeOutput(L"bt", L"Bluetooth Headphones", L"Bluetooth", true, false, false, L"c-bt")
	};

	auto merge_result = FxSound::OutputDeviceSelection::mergeOutputPriorities(existing_priorities, sound_devices, true);

	expect(merge_result.changed, "merge should report changes when prioritized new outputs appear");
	expect(merge_result.priorities.size() == 4, "merge should keep existing outputs and add new ones");
	expect(merge_result.priorities[0].device_id == L"hdmi", "merge should prepend the first new output when prioritization is enabled");
	expect(merge_result.priorities[1].device_id == L"bt", "merge should preserve discovery order for prepended outputs");
	expect(merge_result.priorities[2].device_id == L"spk", "merge should keep the original priority order after prepended outputs");
	expect(merge_result.priorities[3].device_id == L"dac", "merge should keep later existing outputs after prepended outputs");
}

void testMergeOutputPrioritiesRefreshesReconnectedIds()
{
	std::vector<FxSound::OutputDeviceSelection::PriorityEntry> existing_priorities {
		{L"dac-old", L"USB DAC", L"c-dac"}
	};
	std::vector<SoundDevice> sound_devices {
		makeOutput(L"dac-new", L"USB DAC", L"USB Audio", true, false, false, L"c-dac")
	};

	auto merge_result = FxSound::OutputDeviceSelection::mergeOutputPriorities(existing_priorities, sound_devices);

	expect(merge_result.changed, "merge should report changes when a known output reconnects with a new endpoint id");
	expect(merge_result.priorities.size() == 1, "merge should preserve the original priority entry count");
	expect(merge_result.priorities[0].device_id == L"dac-new", "merge should refresh the stored endpoint id for the known output");
}

void testMergeOutputPrioritiesMatchesRenamedDeviceByContainer()
{
	std::vector<FxSound::OutputDeviceSelection::PriorityEntry> existing_priorities {
		{L"dac-old", L"USB DAC", L"c-dac"}
	};
	std::vector<SoundDevice> sound_devices {
		makeOutput(L"dac-new", L"USB DAC 2", L"USB Audio", true, false, false, L"c-dac")
	};

	auto merge_result = FxSound::OutputDeviceSelection::mergeOutputPriorities(existing_priorities, sound_devices);

	expect(merge_result.changed, "merge should report changes when a device reconnects with a renamed endpoint");
	expect(merge_result.priorities.size() == 1, "merge should keep the existing priority entry when only the endpoint name changes");
	expect(merge_result.priorities[0].device_id == L"dac-new", "merge should refresh the endpoint id when matching by container id");
	expect(merge_result.priorities[0].device_name == L"USB DAC 2", "merge should refresh the stored name for the renamed device");
}

void testMergeOutputPrioritiesDropsKnownMonoOutputs()
{
	std::vector<FxSound::OutputDeviceSelection::PriorityEntry> existing_priorities {
		{L"chat", L"Headset Chat"},
		{L"spk", L"Speakers"}
	};
	auto mono_output = makeOutput(L"chat", L"Headset Chat", L"Chat", true, false, false, L"c-chat");
	mono_output.deviceNumChannel = 1;
	std::vector<SoundDevice> sound_devices {
		mono_output,
		makeOutput(L"spk", L"Speakers", L"Built-in", true, true, true, L"c-spk")
	};

	auto merge_result = FxSound::OutputDeviceSelection::mergeOutputPriorities(existing_priorities, sound_devices);

	expect(merge_result.changed, "merge should report changes when a known mono output is removed from priorities");
	expect(merge_result.priorities.size() == 1, "merge should drop known mono outputs from priorities");
	expect(merge_result.priorities[0].device_id == L"spk", "merge should keep stereo priorities after dropping mono outputs");
}

void testAreSameOutputDeviceMatchesReconnectedEndpoint()
{
	auto selected_output = makeOutput(L"dac-old", L"USB DAC", L"USB Audio", false, false, false, L"c-dac");
	auto reconnected_output = makeOutput(L"dac-new", L"USB DAC", L"USB Audio", true, false, true, L"c-dac");

	expect(FxSound::OutputDeviceSelection::areSameOutputDevice(selected_output, reconnected_output),
		"container id and friendly name should match reconnected output");
}

void testResolveSelectedOutputReturnsReconnectedDevice()
{
	auto selected_output = makeOutput(L"dac-old", L"USB DAC", L"USB Audio", false, false, false, L"c-dac");
	std::vector<SoundDevice> output_devices {
		makeOutput(L"spk", L"Speakers", L"Built-in", true, true, false, L"c-spk"),
		makeOutput(L"dac-new", L"USB DAC", L"USB Audio", true, false, true, L"c-dac")
	};
	std::vector<PriorityEntry> priorities {
		{L"dac-old", L"USB DAC"},
		{L"spk", L"Speakers"}
	};

	auto resolved_output = FxSound::OutputDeviceSelection::resolveSelectedOutput(
		output_devices,
		makeTestOutputResolutionContext(selected_output, L"Speakers", priorities));

	expect(resolved_output.pwszID == L"dac-new", "reconnected selected output should win over output name fallback");
}

void testGetPreferredOutputUsesConfiguredPriority()
{
	std::vector<SoundDevice> output_devices {
		makeOutput(L"spk", L"Speakers", L"Built-in", true, true, false, L"c-spk"),
		makeOutput(L"hdmi", L"Monitor", L"HDMI", true, false, true, L"c-hdmi")
	};
	std::vector<PriorityEntry> priorities {
		{L"hdmi", L"Monitor"},
		{L"spk", L"Speakers"}
	};

	auto preferred_output = FxSound::OutputDeviceSelection::getPreferredOutput(output_devices, priorities);
	expect(preferred_output.pwszID == L"hdmi", "preferred output should follow configured priority");
}

void testGetPreferredOutputFallsBackToContainerWhenNameChanges()
{
	std::vector<SoundDevice> output_devices {
		makeOutput(L"spk", L"Speakers", L"Built-in", true, true, false, L"c-spk"),
		makeOutput(L"dac-new", L"USB DAC 2", L"USB Audio", true, false, true, L"c-dac")
	};
	std::vector<PriorityEntry> priorities {
		{L"dac-old", L"USB DAC", L"c-dac"},
		{L"spk", L"Speakers", L"c-spk"}
	};

	auto preferred_output = FxSound::OutputDeviceSelection::getPreferredOutput(output_devices, priorities);

	expect(preferred_output.pwszID == L"dac-new", "preferred output should keep following the same container when the friendly name changes");
}

void testShouldIgnoreDeviceChangeForUnselectedActiveDevice()
{
	auto selected_output = makeOutput(L"spk", L"Speakers", L"Built-in", true, true, true, L"c-spk");
	std::vector<SoundDevice> sound_devices {
		selected_output,
		makeOutput(L"hdmi", L"Monitor", L"HDMI", true, false, false, L"c-hdmi")
	};

	auto ignored = FxSound::OutputDeviceSelection::shouldIgnoreDeviceChange(
		AudioDeviceChangeKind::DeviceStateChanged,
		L"hdmi",
		selected_output,
		sound_devices);

	expect(ignored, "device changes for unselected active outputs should be ignored");
}

void testShouldNotIgnoreDeviceChangeForSelectedOutput()
{
	auto selected_output = makeOutput(L"spk", L"Speakers", L"Built-in", true, true, true, L"c-spk");
	std::vector<SoundDevice> sound_devices {
		selected_output,
		makeOutput(L"hdmi", L"Monitor", L"HDMI", true, false, false, L"c-hdmi")
	};

	auto ignored = FxSound::OutputDeviceSelection::shouldIgnoreDeviceChange(
		AudioDeviceChangeKind::DeviceStateChanged,
		L"spk",
		selected_output,
		sound_devices);

	expect(!ignored, "selected output changes should not be ignored");
}

void testShouldNotIgnoreDeviceChangeWhenSelectedOutputIsInactive()
{
	auto selected_output = makeOutput(L"spk", L"Speakers", L"Built-in", false, false, false, L"c-spk");
	std::vector<SoundDevice> sound_devices {
		selected_output,
		makeOutput(L"hdmi", L"Monitor", L"HDMI", true, false, true, L"c-hdmi")
	};

	auto ignored = FxSound::OutputDeviceSelection::shouldIgnoreDeviceChange(
		AudioDeviceChangeKind::DeviceStateChanged,
		L"hdmi",
		selected_output,
		sound_devices);

	expect(!ignored, "inactive selected output should not suppress device changes");
}

void testBuildSyncDecisionRequestsRoutingForActiveUntargetedOutput()
{
	auto selected_output = makeOutput(L"spk", L"Speakers", L"Built-in", true, true, false, L"c-spk");
	std::vector<SoundDevice> output_devices { selected_output };
	std::vector<PriorityEntry> priorities { {L"spk", L"Speakers"} };

	auto decision = FxSound::OutputDeviceSelection::buildSyncDecision(
		output_devices,
		makeTestOutputResolutionContext(selected_output, L"Speakers", priorities),
		true);

	expect(decision.has_resolved_output, "sync decision should resolve active output");
	expect(decision.routing_changed, "untargeted active output should require routing");
	expect(decision.routing_actions.should_retarget_playback, "timer-running sync should apply routing when target differs");
	expect(!decision.should_mute, "active output should not be muted");
}

void testBuildSyncDecisionMutesInactiveSelectedOutput()
{
	auto selected_output = makeOutput(L"dac-old", L"USB DAC", L"USB Audio", false, false, false, L"c-dac");
	std::vector<SoundDevice> output_devices { selected_output };
	std::vector<PriorityEntry> priorities { {L"dac-old", L"USB DAC"} };

	auto decision = FxSound::OutputDeviceSelection::buildSyncDecision(
		output_devices,
		makeTestOutputResolutionContext(selected_output, L"USB DAC", priorities),
		true);

	expect(decision.has_resolved_output, "sync decision should preserve inactive selected output");
	expect(decision.should_mute, "inactive selected output should mute processing");
	expect(!decision.routing_actions.should_retarget_playback, "inactive selected output should not retarget playback");
}

void testBuildSyncDecisionFallsBackToPreferredOutput()
{
	SoundDevice selected_output = makeOutput(L"missing", L"Missing DAC", L"USB Audio", false, false, false, L"c-old");
	std::vector<SoundDevice> output_devices {
		makeOutput(L"spk", L"Speakers", L"Built-in", true, true, false, L"c-spk"),
		makeOutput(L"hdmi", L"Monitor", L"HDMI", true, false, true, L"c-hdmi")
	};
	std::vector<PriorityEntry> priorities {
		{L"spk", L"Speakers"},
		{L"hdmi", L"Monitor"}
	};

	auto decision = FxSound::OutputDeviceSelection::buildSyncDecision(
		output_devices,
		makeTestOutputResolutionContext(selected_output, L"", priorities),
		true);

	expect(decision.has_resolved_output, "sync decision should fall back to a preferred active output");
	expect(decision.resolved_output.pwszID == L"spk", "fallback should follow configured priority");
	expect(decision.output_changed, "fallback to another output should count as an output change");
	expect(decision.routing_actions.should_retarget_playback, "fallback to a new active output should apply routing");
}

void testBuildInitDecisionKeepsSelectedInactiveOutput()
{
	std::vector<SoundDevice> sound_devices {
		makeOutput(L"spk", L"Speakers", L"Built-in", true, true, true, L"c-spk")
	};
	SoundDevice selected_output = makeOutput(L"dac-old", L"USB DAC", L"USB Audio", false, false, false, L"c-dac");
	std::vector<SoundDevice> output_devices = FxSound::OutputDeviceSelection::buildVisibleOutputDevices(
		sound_devices,
		selected_output,
		{{L"dac-old", L"USB DAC"}, {L"spk", L"Speakers"}},
		true);

	auto decision = FxSound::OutputDeviceSelection::buildInitDecision(
		sound_devices,
		output_devices,
		makeTestOutputResolutionContext(selected_output, L"USB DAC", {{L"dac-old", L"USB DAC"}, {L"spk", L"Speakers"}}));

	expect(decision.has_resolved_output, "init should resolve the selected inactive output");
	expect(decision.resolved_output.pwszID == L"dac-old", "init should preserve the selected inactive output");
	expect(decision.should_mute, "inactive selected output should stay muted on startup");
	expect(!decision.should_apply_output, "inactive selected output should not be auto-applied on startup");
}

void testBuildInitDecisionFallsBackToActiveDefaultOutput()
{
	std::vector<SoundDevice> sound_devices {
		makeOutput(L"spk", L"Speakers", L"Built-in", true, true, true, L"c-spk"),
		makeOutput(L"hdmi", L"Monitor", L"HDMI", true, false, false, L"c-hdmi")
	};
	std::vector<SoundDevice> output_devices = FxSound::OutputDeviceSelection::buildVisibleOutputDevices(
		sound_devices,
		SoundDevice(),
		{{L"spk", L"Speakers"}, {L"hdmi", L"Monitor"}},
		true);

	auto decision = FxSound::OutputDeviceSelection::buildInitDecision(
		sound_devices,
		output_devices,
		makeTestOutputResolutionContext(SoundDevice(), L"", {{L"spk", L"Speakers"}, {L"hdmi", L"Monitor"}}));

	expect(decision.has_resolved_output, "init should resolve a startup output");
	expect(decision.resolved_output.pwszID == L"spk", "init should prefer the active default output when nothing is selected");
	expect(decision.should_apply_output, "active default output should be applied on startup");
}

void testBuildInitDecisionResolvesReconnectedSelectedOutput()
{
	SoundDevice selected_output = makeOutput(L"dac-old", L"USB DAC", L"USB Audio", false, false, false, L"c-dac");
	std::vector<SoundDevice> sound_devices {
		makeOutput(L"spk", L"Speakers", L"Built-in", true, true, false, L"c-spk"),
		makeOutput(L"dac-new", L"USB DAC", L"USB Audio", true, false, true, L"c-dac")
	};
	std::vector<SoundDevice> output_devices = FxSound::OutputDeviceSelection::buildVisibleOutputDevices(
		sound_devices,
		selected_output,
		{{L"dac-old", L"USB DAC"}, {L"spk", L"Speakers"}},
		true);

	auto decision = FxSound::OutputDeviceSelection::buildInitDecision(
		sound_devices,
		output_devices,
		makeTestOutputResolutionContext(selected_output, L"USB DAC", {{L"dac-old", L"USB DAC"}, {L"spk", L"Speakers"}}));

	expect(decision.has_resolved_output, "init should resolve a reconnected selected output");
	expect(decision.resolved_output.pwszID == L"dac-new", "init should pick the reconnected endpoint for the selected output");
	expect(decision.should_apply_output, "reconnected selected output should be applied on startup");
}

void testBuildIdleSyncDecisionKeepsInactiveSelectedOutput()
{
	SoundDevice selected_output = makeOutput(L"dac-old", L"USB DAC", L"USB Audio", false, false, false, L"c-dac");
	std::vector<SoundDevice> output_devices {
		selected_output,
		makeOutput(L"spk", L"Speakers", L"Built-in", true, true, false, L"c-spk")
	};

	auto decision = FxSound::OutputDeviceSelection::buildIdleSyncDecision(
		output_devices,
		makeTestOutputResolutionContext(selected_output, L"USB DAC", {{L"dac-old", L"USB DAC"}, {L"spk", L"Speakers"}}));

	expect(decision.has_resolved_output, "idle sync should resolve the selected inactive output");
	expect(decision.resolved_output.pwszID == L"dac-old", "idle sync should preserve the selected inactive output");
	expect(decision.should_notify_error, "idle sync should report the inactive selected output");
}

void testBuildIdleSyncDecisionResolvesReconnectedSelectedOutput()
{
	SoundDevice selected_output = makeOutput(L"dac-old", L"USB DAC", L"USB Audio", false, false, false, L"c-dac");
	std::vector<SoundDevice> output_devices {
		makeOutput(L"spk", L"Speakers", L"Built-in", true, true, false, L"c-spk"),
		makeOutput(L"dac-new", L"USB DAC", L"USB Audio", true, false, false, L"c-dac")
	};

	auto decision = FxSound::OutputDeviceSelection::buildIdleSyncDecision(
		output_devices,
		makeTestOutputResolutionContext(selected_output, L"USB DAC", {{L"dac-old", L"USB DAC"}, {L"spk", L"Speakers"}}));

	expect(decision.has_resolved_output, "idle sync should resolve a reconnected selected output");
	expect(decision.resolved_output.pwszID == L"dac-new", "idle sync should resolve to the reconnected endpoint");
	expect(!decision.should_notify_error, "idle sync should not report an error for an active reconnected output");
}

void testManualSelectionDecisionRestartsProcessingAfterInactiveSelection()
{
	auto previous_selected_output = makeOutput(L"dac-old", L"USB DAC", L"USB Audio", false, false, false, L"c-dac");
	std::vector<SoundDevice> sound_devices {
		makeOutput(L"spk", L"Speakers", L"Built-in", true, true, false, L"c-spk")
	};

	auto decision = FxSound::OutputDeviceSelection::buildManualSelectionDecision(
		sound_devices,
		L"spk",
		previous_selected_output,
		true,
		true,
		false);

	expect(decision.found_output, "manual selection should find active speaker output");
	expect(decision.routing_actions.should_retarget_playback, "manual selection should retarget untargeted output");
	expect(decision.routing_actions.should_restart_processing, "manual selection should restart processing after inactive selection");
	expect(decision.routing_actions.should_begin_grace_period, "manual selection should begin grace period when processing restarts");
}

void testManualSelectionDecisionLeavesDefaultOutputUntouchedWhenProcessingIsOff()
{
	auto previous_selected_output = makeOutput(L"spk", L"Speakers", L"Built-in", true, true, true, L"c-spk");
	std::vector<SoundDevice> sound_devices {
		makeOutput(L"spk", L"Speakers", L"Built-in", true, true, true, L"c-spk")
	};

	auto decision = FxSound::OutputDeviceSelection::buildManualSelectionDecision(
		sound_devices,
		L"spk",
		previous_selected_output,
		false,
		false,
		true);

	expect(decision.found_output, "manual selection should resolve default output");
	expect(!decision.routing_actions.should_retarget_playback, "default output should not be retargeted while processing is off");
	expect(!decision.routing_actions.should_restart_processing, "processing-off selection should not restart processing");
	expect(!decision.routing_actions.should_begin_grace_period, "processing-off selection should not start a grace period");
}

void testManualSelectionDecisionPowersOffWhenOutputIsMissing()
{
	auto previous_selected_output = makeOutput(L"spk", L"Speakers", L"Built-in", true, true, true, L"c-spk");
	std::vector<SoundDevice> sound_devices {
		makeOutput(L"hdmi", L"Monitor", L"HDMI", true, false, false, L"c-hdmi")
	};

	auto decision = FxSound::OutputDeviceSelection::buildManualSelectionDecision(
		sound_devices,
		L"missing",
		previous_selected_output,
		true,
		true,
		true);

	expect(!decision.found_output, "manual selection should report missing outputs");
	expect(decision.should_power_off, "missing output should request power off");
}

void testManualSelectionDecisionRejectsMonoOutput()
{
	auto mono_output = makeOutput(L"chat", L"Headset Chat", L"Chat", true, false, false, L"c-chat");
	mono_output.deviceNumChannel = 1;
	std::vector<SoundDevice> sound_devices { mono_output };

	auto decision = FxSound::OutputDeviceSelection::buildManualSelectionDecision(
		sound_devices,
		L"chat",
		{},
		true,
		true,
		true);

	expect(!decision.found_output, "manual selection should reject mono outputs");
	expect(decision.should_power_off, "manual selection should treat mono outputs as unavailable");
}

void testScenarioKeepsSelectedActiveOutputAcrossUnrelatedReconnect()
{
	ScenarioState state;
	state.selected_output = makeOutput(L"spk", L"Speakers", L"Built-in", true, true, true, L"c-spk");
	state.output_name = L"Speakers";
	std::vector<PriorityEntry> priorities {
		{L"spk", L"Speakers"},
		{L"hdmi", L"Monitor"}
	};
	std::vector<SoundDevice> sound_devices {
		makeOutput(L"spk", L"Speakers", L"Built-in", true, true, true, L"c-spk"),
		makeOutput(L"hdmi", L"Monitor", L"HDMI", true, false, false, L"c-hdmi")
	};

	applyDeviceChange(state, AudioDeviceChangeKind::DeviceAdded, L"hdmi", sound_devices, priorities);

	expect(!state.restarted_processing, "unrelated reconnect should not restart processing");
	expect(state.selected_output.pwszID == L"spk", "selected active output should stay selected");
	expect(!state.muted, "selected active output should keep playback unmuted");
}

void testScenarioRestoresReconnectedSelectedOutput()
{
	ScenarioState state;
	state.selected_output = makeOutput(L"dac-old", L"USB DAC", L"USB Audio", false, false, false, L"c-dac");
	state.output_name = L"USB DAC";
	state.playback_device_available = false;
	state.muted = true;
	std::vector<PriorityEntry> priorities {
		{L"dac-old", L"USB DAC"},
		{L"spk", L"Speakers"}
	};

	std::vector<SoundDevice> inactive_devices {
		makeOutput(L"spk", L"Speakers", L"Built-in", true, true, true, L"c-spk")
	};
	applyRefresh(state, inactive_devices, priorities);
	expect(!state.visible_outputs.empty(), "selected inactive output should remain visible in the scenario");
	expect(state.muted, "inactive selected output should remain muted");

	std::vector<SoundDevice> reconnected_devices {
		makeOutput(L"spk", L"Speakers", L"Built-in", true, true, false, L"c-spk"),
		makeOutput(L"dac-new", L"USB DAC", L"USB Audio", true, false, false, L"c-dac")
	};
	applyDeviceChange(state, AudioDeviceChangeKind::DeviceStateChanged, L"dac-new", reconnected_devices, priorities);

	expect(state.restarted_processing, "reconnected selected output should trigger a refresh path");
	expect(state.selected_output.pwszID == L"dac-new", "reconnected selected output should be matched by container id");
	expect(!state.muted, "reconnected selected output should resume playback");
}

void testScenarioManualSelectionRecoversFromInactiveOutput()
{
	ScenarioState state;
	state.selected_output = makeOutput(L"dac-old", L"USB DAC", L"USB Audio", false, false, false, L"c-dac");
	state.output_name = L"USB DAC";
	state.playback_device_available = false;
	state.muted = true;

	std::vector<SoundDevice> sound_devices {
		makeOutput(L"spk", L"Speakers", L"Built-in", true, true, false, L"c-spk"),
		makeOutput(L"hdmi", L"Monitor", L"HDMI", true, false, false, L"c-hdmi")
	};

	applyManualSelection(state, L"spk", sound_devices);

	expect(state.selected_output.pwszID == L"spk", "manual selection should switch to the active device");
	expect(state.restarted_processing, "manual selection should restart processing after inactive selection");
	expect(!state.muted, "manual selection of an active device should restore playback");
}

void testRuntimeDeviceChangeIgnoresUnrelatedReconnect()
{
	RuntimeHarness harness;
	harness.state.selected_output = makeOutput(L"spk", L"Speakers", L"Built-in", true, true, true, L"c-spk");
	harness.state.output_name = L"Speakers";
	harness.audio.sound_devices = {
		makeOutput(L"spk", L"Speakers", L"Built-in", true, true, true, L"c-spk"),
		makeOutput(L"hdmi", L"Monitor", L"HDMI", true, false, false, L"c-hdmi")
	};
	harness.audio.playback_device_available = true;
	harness.priorities = {
		{L"spk", L"Speakers"},
		{L"hdmi", L"Monitor"}
	};

	applyRuntimeDeviceChange(harness, AudioDeviceChangeKind::DeviceAdded, L"hdmi");

	expect(harness.audio.restart_call_count == 0, "ignored device change should not restart processing");
	expect(harness.audio.set_playback_call_count == 0, "ignored device change should not retarget playback");
	expect(harness.audio.mute_call_count == 0, "ignored device change should not touch mute state");
	expect(harness.state.selected_output.pwszID == L"spk", "ignored device change should keep the selected output");
}

void testRuntimeDeviceChangeRestoresReconnectedSelectedOutput()
{
	RuntimeHarness harness;
	harness.state.selected_output = makeOutput(L"dac-old", L"USB DAC", L"USB Audio", false, false, false, L"c-dac");
	harness.state.output_name = L"USB DAC";
	harness.state.playback_device_available = false;
	harness.state.muted = true;
	harness.audio.sound_devices = {
		makeOutput(L"spk", L"Speakers", L"Built-in", true, true, true, L"c-spk"),
		makeOutput(L"dac-new", L"USB DAC", L"USB Audio", true, false, false, L"c-dac")
	};
	harness.audio.playback_device_available = false;
	harness.audio.muted = true;
	harness.priorities = {
		{L"dac-old", L"USB DAC"},
		{L"spk", L"Speakers"}
	};

	applyRuntimeDeviceChange(harness, AudioDeviceChangeKind::DeviceStateChanged, L"dac-new");

	expect(harness.audio.restart_call_count == 1, "reconnected selected output should restart processing");
	expect(harness.audio.set_playback_call_count == 1, "reconnected selected output should retarget playback");
	expect(harness.audio.last_playback_device_id == L"dac-new", "retargeting should point at the reconnected device");
	expect(harness.audio.mute_false_call_count == 1, "reconnected selected output should unmute playback");
	expect(harness.state.selected_output.pwszID == L"dac-new", "selected output should resolve to the reconnected endpoint");
}

void testRuntimeManualSelectionRecoversThroughAudioPassthru()
{
	RuntimeHarness harness;
	harness.state.selected_output = makeOutput(L"dac-old", L"USB DAC", L"USB Audio", false, false, false, L"c-dac");
	harness.state.output_name = L"USB DAC";
	harness.state.playback_device_available = false;
	harness.state.muted = true;
	harness.audio.sound_devices = {
		makeOutput(L"spk", L"Speakers", L"Built-in", true, true, false, L"c-spk"),
		makeOutput(L"hdmi", L"Monitor", L"HDMI", true, false, false, L"c-hdmi")
	};
	harness.audio.playback_device_available = false;
	harness.audio.muted = true;

	applyRuntimeManualSelection(harness, L"spk");

	expect(harness.audio.set_playback_call_count == 1, "manual selection should retarget playback");
	expect(harness.audio.restart_call_count == 1, "manual selection should restart processing after inactive output");
	expect(harness.audio.mute_false_call_count == 1, "manual selection should unmute the recovered output");
	expect(harness.state.selected_output.pwszID == L"spk", "manual selection should switch to the chosen active output");
	expect(!harness.state.muted, "manual selection should leave playback unmuted");
}

void testRuntimeStartupPreservesSelectedInactiveOutput()
{
	RuntimeHarness harness;
	harness.state.selected_output = makeOutput(L"dac-old", L"USB DAC", L"USB Audio", false, false, false, L"c-dac");
	harness.state.output_name = L"USB DAC";
	harness.audio.sound_devices = {
		makeOutput(L"spk", L"Speakers", L"Built-in", true, true, true, L"c-spk")
	};
	harness.audio.playback_device_available = true;
	harness.priorities = {
		{L"dac-old", L"USB DAC"},
		{L"spk", L"Speakers"}
	};

	applyRuntimeStartup(harness);

	expect(harness.audio.set_playback_call_count == 0, "startup should not retarget away from an inactive selected output");
	expect(harness.audio.restart_call_count == 0, "startup should not restart processing for an inactive selected output");
	expect(harness.audio.mute_true_call_count == 1, "startup should mute when the selected output is inactive");
	expect(harness.state.selected_output.pwszID == L"dac-old", "startup should preserve the selected inactive output");
	expect(harness.state.muted, "startup should leave the inactive selected output muted");
}

void testRuntimeStartupRecoversReconnectedSelectedOutput()
{
	RuntimeHarness harness;
	harness.state.selected_output = makeOutput(L"dac-old", L"USB DAC", L"USB Audio", false, false, false, L"c-dac");
	harness.state.output_name = L"USB DAC";
	harness.state.playback_device_available = false;
	harness.state.muted = true;
	harness.audio.sound_devices = {
		makeOutput(L"spk", L"Speakers", L"Built-in", true, true, false, L"c-spk"),
		makeOutput(L"dac-new", L"USB DAC", L"USB Audio", true, false, false, L"c-dac")
	};
	harness.audio.playback_device_available = false;
	harness.audio.muted = true;
	harness.priorities = {
		{L"dac-old", L"USB DAC"},
		{L"spk", L"Speakers"}
	};

	applyRuntimeStartup(harness);

	expect(harness.audio.set_playback_call_count == 1, "startup should retarget to the reconnected selected output");
	expect(harness.audio.restart_call_count == 1, "startup should restart processing for the reconnected selected output");
	expect(harness.audio.last_playback_device_id == L"dac-new", "startup should route playback to the reconnected endpoint");
	expect(harness.audio.mute_false_call_count == 1, "startup should unmute the recovered selected output");
	expect(harness.state.selected_output.pwszID == L"dac-new", "startup should resolve the selected output to the reconnected endpoint");
}

void testRuntimeIdleSyncPreservesInactiveSelectedOutputWithoutAudioCalls()
{
	RuntimeHarness harness;
	harness.state.selected_output = makeOutput(L"dac-old", L"USB DAC", L"USB Audio", false, false, false, L"c-dac");
	harness.state.output_name = L"USB DAC";
	harness.audio.sound_devices = {
		makeOutput(L"spk", L"Speakers", L"Built-in", true, true, false, L"c-spk")
	};
	harness.priorities = {
		{L"dac-old", L"USB DAC"},
		{L"spk", L"Speakers"}
	};

	applyRuntimeIdleSync(harness);

	expect(harness.audio.set_playback_call_count == 0, "idle sync should not retarget playback");
	expect(harness.audio.restart_call_count == 0, "idle sync should not restart processing");
	expect(harness.audio.mute_call_count == 0, "idle sync should not touch backend mute state");
	expect(harness.state.selected_output.pwszID == L"dac-old", "idle sync should preserve the selected inactive output");
	expect(!harness.state.playback_device_available, "idle sync should mark the inactive selected output unavailable");
}

void testRuntimeIdleSyncRecoversReconnectedSelectedOutputWithoutAudioCalls()
{
	RuntimeHarness harness;
	harness.state.selected_output = makeOutput(L"dac-old", L"USB DAC", L"USB Audio", false, false, false, L"c-dac");
	harness.state.output_name = L"USB DAC";
	harness.state.playback_device_available = false;
	harness.audio.sound_devices = {
		makeOutput(L"spk", L"Speakers", L"Built-in", true, true, false, L"c-spk"),
		makeOutput(L"dac-new", L"USB DAC", L"USB Audio", true, false, false, L"c-dac")
	};
	harness.priorities = {
		{L"dac-old", L"USB DAC"},
		{L"spk", L"Speakers"}
	};

	applyRuntimeIdleSync(harness);

	expect(harness.audio.set_playback_call_count == 0, "idle sync should not retarget playback while processing is off");
	expect(harness.audio.restart_call_count == 0, "idle sync should not restart processing while processing is off");
	expect(harness.state.selected_output.pwszID == L"dac-new", "idle sync should resolve to the reconnected selected output");
	expect(harness.state.playback_device_available, "idle sync should mark the recovered selected output available");
}

void testAudioSignalPolicyUsesCaptureOnlyForSignalPresence()
{
	const int64_t now_ms = 2000;
	expect(
		FxSound::AudioSignalPolicy::isCaptureSignalPresent(now_ms, 1700, -80.0f),
		"recent capture above threshold should count as signal");
	expect(
		!FxSound::AudioSignalPolicy::isCaptureSignalPresent(now_ms, 1400, -80.0f),
		"stale capture should not count as signal");
	expect(
		!FxSound::AudioSignalPolicy::isCaptureSignalPresent(now_ms, 1700, -120.0f),
		"quiet capture should not count as signal");
}

void testAudioSignalPolicyGraceResetsSignalCounters()
{
	const auto counters = FxSound::AudioSignalPolicy::advanceSignalCounters(true, true, 4, 3);
	expect(counters.signal_present_after == 0, "grace period should clear signal present counter");
	expect(counters.signal_absent_after == 0, "grace period should clear signal absent counter");
}

void testAudioSignalPolicyEnablesDspOnFirstSignalTick()
{
	auto counters = FxSound::AudioSignalPolicy::CounterState {};
	counters = FxSound::AudioSignalPolicy::advanceSignalCounters(true, false, counters.signal_present_after, counters.signal_absent_after);

	expect(
		FxSound::AudioSignalPolicy::shouldEnableDsp(counters.signal_present_after, false),
		"first signal tick should enable dsp");
}

void testAudioSignalPolicyDisablesDspAfterFiveSilentTicks()
{
	auto counters = FxSound::AudioSignalPolicy::CounterState {};
	for (int index = 0; index < 5; ++index)
	{
		counters = FxSound::AudioSignalPolicy::advanceSignalCounters(false, false, counters.signal_present_after, counters.signal_absent_after);
	}

	expect(
		FxSound::AudioSignalPolicy::shouldDisableDsp(counters.signal_absent_after, true),
		"five silent ticks should disable dsp");
}

void testAudioSignalPolicyDetectsPlaybackStallWithLiveCapture()
{
	expect(
		FxSound::AudioSignalPolicy::shouldDetectPlaybackStall(0, true, 5000, 4200, 3000, true),
		"audible live capture with stale playback should trigger stall detection");
}

void testAudioSignalPolicySkipsRestartWithoutAudibleCapture()
{
	expect(
		!FxSound::AudioSignalPolicy::shouldDetectPlaybackStall(0, true, 5000, 4200, 3000, false),
		"silent capture should not trigger playback stall recovery");
}

void runTest(const std::string& name, const std::function<void()>& test)
{
	test();
	std::cout << "[pass] " << name << std::endl;
}
}

int main()
{
	try
	{
		runTest("build visible outputs keeps selected inactive", testBuildVisibleOutputsKeepsSelectedInactive);
		runTest("build visible outputs drops unselected inactive", testBuildVisibleOutputsDropsUnselectedInactive);
		runTest("restore persisted output returns empty when state is empty", testRestorePersistedOutputReturnsEmptyWhenStateIsEmpty);
		runTest("persisted output round trip preserves identity", testPersistedOutputRoundTripPreservesIdentity);
		runTest("auto preset decision applies when triggered", testAutoPresetDecisionAppliesWhenTriggered);
		runTest("auto preset decision skips modified preset", testAutoPresetDecisionSkipsModifiedPreset);
		runTest("auto preset decision skips when not triggered", testAutoPresetDecisionSkipsWhenNotTriggered);
		runTest("auto preset decision skips empty preset", testAutoPresetDecisionSkipsEmptyPreset);
		runTest("auto eq policy resets after preset load", testAutoEqPolicyResetsAnalysisAfterPresetLoad);
		runTest("auto eq policy resets after filter Q change", testAutoEqPolicyResetsAnalysisAfterFilterQChange);
		runTest("auto eq policy resets after band count change", testAutoEqPolicyResetsAnalysisAfterBandCountChange);
		runTest("auto eq policy resets after band frequency change", testAutoEqPolicyResetsAnalysisAfterBandFrequencyChange);
		runTest("auto eq policy disables after manual band gain edit", testAutoEqPolicyDisablesAfterManualBandGainEdit);
		runTest("auto eq policy disables after manual band frequency edit", testAutoEqPolicyDisablesAfterManualBandFrequencyEdit);
		runTest("preset switch decision autosaves modified current preset", testPresetSwitchDecisionAutoSavesModifiedCurrentPreset);
		runTest("preset switch decision skips autosave when selection does not change", testPresetSwitchDecisionSkipsAutoSaveWhenSelectionDoesNotChange);
		runTest("autosave cleanup keeps case-insensitive preset matches", testAutoSaveCleanupKeepsCaseInsensitivePresetMatch);
		runTest("autosave cleanup drops unknown presets", testAutoSaveCleanupDropsUnknownPreset);
		runTest("startup option policy finds exact output latency flag", testStartupOptionPolicyFindsExactOutputLatencyFlag);
		runTest("startup option policy ignores similar output latency flags", testStartupOptionPolicyIgnoresSimilarOutputLatencyFlags);
		runTest("preset apply requires ids to be missing before using name fallback", testPresetApplyRequiresIdsToBeMissingBeforeUsingNameFallback);
		runTest("preset apply uses name fallback only for legacy entries", testPresetApplyUsesNameFallbackOnlyForLegacyEntries);
		runTest("configured preset restore applies existing preset", testConfiguredPresetRestoreDecisionAppliesExistingPreset);
		runTest("configured preset restore clears missing preset", testConfiguredPresetRestoreDecisionClearsMissingPreset);
		runTest("configured preset restore falls back when preset is unset", testConfiguredPresetRestoreDecisionFallsBackWhenPresetIsUnset);
		runTest("audio passthru cleanup continues after restore failure", testAudioPassthruCleanupContinuesAfterRestoreFailure);
		runTest("audio passthru cleanup stops after thread shutdown timeout", testAudioPassthruCleanupStopsAfterThreadShutdownTimeout);
		runTest("buffer policy migrates legacy machine default", testBufferPolicyMigratesLegacyMachineDefaultToLowLatencyDefault);
		runTest("buffer policy preserves explicit modern machine default", testBufferPolicyPreservesExplicitModernMachineDefault);
		runTest("buffer policy falls back when machine default missing", testBufferPolicyFallsBackWhenMachineDefaultMissing);
		runTest("buffer policy clamps out of range values", testBufferPolicyClampsOutOfRangeValues);
		runTest("quiet floor retains boost after prolonged low output", testQuietFloorRetainsBoostAfterProlongedLowOutput);
		runTest("quiet floor raise stops near ceiling", testQuietFloorRaiseStopsNearCeiling);
		runTest("quiet floor release and silence decay work", testQuietFloorReleaseAndSilenceDecayWork);
		runTest("processing scan prefers targeted output", testScanProcessingOutputsPrefersTargetedOutput);
		runTest("processing scan skips mono default without fallback", testScanProcessingOutputsSkipsMonoDefaultWithoutFallback);
		runTest("processing scan detects dfx endpoint", testScanProcessingOutputsDetectsDfxEndpoint);
		runTest("build initial output priorities keeps default first", testBuildInitialOutputPrioritiesKeepsDefaultFirst);
		runTest("build initial output priorities drops duplicates by name", testBuildInitialOutputPrioritiesDropsDuplicatesByName);
		runTest("build initial output priorities keeps same-name different containers", testBuildInitialOutputPrioritiesKeepsSameNameDifferentContainers);
		runTest("build initial output priorities skips mono devices", testBuildInitialOutputPrioritiesSkipsMonoDevices);
		runTest("merge output priorities appends new outputs", testMergeOutputPrioritiesAppendsNewOutputs);
		runTest("merge output priorities prepends new outputs when prioritized", testMergeOutputPrioritiesPrependsNewOutputsWhenPrioritized);
		runTest("merge output priorities refreshes reconnected ids", testMergeOutputPrioritiesRefreshesReconnectedIds);
		runTest("merge output priorities matches renamed device by container", testMergeOutputPrioritiesMatchesRenamedDeviceByContainer);
		runTest("merge output priorities drops known mono outputs", testMergeOutputPrioritiesDropsKnownMonoOutputs);
		runTest("same output matches reconnected endpoint", testAreSameOutputDeviceMatchesReconnectedEndpoint);
		runTest("resolve selected output returns reconnected device", testResolveSelectedOutputReturnsReconnectedDevice);
		runTest("preferred output uses configured priority", testGetPreferredOutputUsesConfiguredPriority);
		runTest("preferred output falls back to container when name changes", testGetPreferredOutputFallsBackToContainerWhenNameChanges);
		runTest("ignore device change for unselected active device", testShouldIgnoreDeviceChangeForUnselectedActiveDevice);
		runTest("do not ignore device change for selected output", testShouldNotIgnoreDeviceChangeForSelectedOutput);
		runTest("do not ignore device change when selected output is inactive", testShouldNotIgnoreDeviceChangeWhenSelectedOutputIsInactive);
		runTest("sync decision routes active untargeted output", testBuildSyncDecisionRequestsRoutingForActiveUntargetedOutput);
		runTest("sync decision mutes inactive selected output", testBuildSyncDecisionMutesInactiveSelectedOutput);
		runTest("sync decision falls back to preferred output", testBuildSyncDecisionFallsBackToPreferredOutput);
		runTest("init decision keeps selected inactive output", testBuildInitDecisionKeepsSelectedInactiveOutput);
		runTest("init decision falls back to active default output", testBuildInitDecisionFallsBackToActiveDefaultOutput);
		runTest("init decision resolves reconnected selected output", testBuildInitDecisionResolvesReconnectedSelectedOutput);
		runTest("idle sync decision keeps inactive selected output", testBuildIdleSyncDecisionKeepsInactiveSelectedOutput);
		runTest("idle sync decision resolves reconnected selected output", testBuildIdleSyncDecisionResolvesReconnectedSelectedOutput);
		runTest("manual selection restarts processing after inactive selection", testManualSelectionDecisionRestartsProcessingAfterInactiveSelection);
		runTest("manual selection leaves default output untouched when processing is off", testManualSelectionDecisionLeavesDefaultOutputUntouchedWhenProcessingIsOff);
		runTest("manual selection powers off when output is missing", testManualSelectionDecisionPowersOffWhenOutputIsMissing);
		runTest("manual selection rejects mono output", testManualSelectionDecisionRejectsMonoOutput);
		runTest("scenario keeps selected active output across unrelated reconnect", testScenarioKeepsSelectedActiveOutputAcrossUnrelatedReconnect);
		runTest("scenario restores reconnected selected output", testScenarioRestoresReconnectedSelectedOutput);
		runTest("scenario manual selection recovers from inactive output", testScenarioManualSelectionRecoversFromInactiveOutput);
		runTest("runtime device change ignores unrelated reconnect", testRuntimeDeviceChangeIgnoresUnrelatedReconnect);
		runTest("runtime device change restores reconnected selected output", testRuntimeDeviceChangeRestoresReconnectedSelectedOutput);
		runTest("runtime manual selection recovers through audio passthru", testRuntimeManualSelectionRecoversThroughAudioPassthru);
		runTest("runtime startup preserves selected inactive output", testRuntimeStartupPreservesSelectedInactiveOutput);
		runTest("runtime startup recovers reconnected selected output", testRuntimeStartupRecoversReconnectedSelectedOutput);
		runTest("runtime idle sync preserves inactive selected output without audio calls", testRuntimeIdleSyncPreservesInactiveSelectedOutputWithoutAudioCalls);
		runTest("runtime idle sync recovers reconnected selected output without audio calls", testRuntimeIdleSyncRecoversReconnectedSelectedOutputWithoutAudioCalls);
		runTest("audio signal policy uses capture only for signal presence", testAudioSignalPolicyUsesCaptureOnlyForSignalPresence);
		runTest("audio signal policy grace resets signal counters", testAudioSignalPolicyGraceResetsSignalCounters);
		runTest("audio signal policy enables dsp on first signal tick", testAudioSignalPolicyEnablesDspOnFirstSignalTick);
		runTest("audio signal policy disables dsp after five silent ticks", testAudioSignalPolicyDisablesDspAfterFiveSilentTicks);
		runTest("audio signal policy detects playback stall with live capture", testAudioSignalPolicyDetectsPlaybackStallWithLiveCapture);
		runTest("audio signal policy skips restart without audible capture", testAudioSignalPolicySkipsRestartWithoutAudibleCapture);
	}
	catch (const std::exception& exception)
	{
		std::cerr << "[fail] " << exception.what() << std::endl;
		return 1;
	}

	return 0;
}
