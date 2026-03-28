#include <exception>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../../fxsound/Source/GUI/OutputDeviceSelection.h"

namespace
{
using FxSound::OutputDeviceSelection::PriorityEntry;

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
		state.selected_output,
		state.output_name,
		priorities,
		state.timer_running);

	if (!decision.has_resolved_output)
	{
		return;
	}

	state.selected_output = decision.resolved_output;
	state.output_name = decision.resolved_output.deviceFriendlyName;
	state.retargeted_playback = state.retargeted_playback || decision.should_apply_routing;

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
	state.retargeted_playback = decision.should_retarget_playback;
	state.restarted_processing = decision.should_restart_processing;

	if (decision.should_sync_processing_state)
	{
		state.playback_device_available = decision.selected_output.isActive;
		state.muted = !state.playback_device_available;
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
		selected_output,
		L"Speakers",
		priorities);

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
		selected_output,
		L"Speakers",
		priorities,
		true);

	expect(decision.has_resolved_output, "sync decision should resolve active output");
	expect(decision.routing_changed, "untargeted active output should require routing");
	expect(decision.should_apply_routing, "timer-running sync should apply routing when target differs");
	expect(!decision.should_mute, "active output should not be muted");
}

void testBuildSyncDecisionMutesInactiveSelectedOutput()
{
	auto selected_output = makeOutput(L"dac-old", L"USB DAC", L"USB Audio", false, false, false, L"c-dac");
	std::vector<SoundDevice> output_devices { selected_output };
	std::vector<PriorityEntry> priorities { {L"dac-old", L"USB DAC"} };

	auto decision = FxSound::OutputDeviceSelection::buildSyncDecision(
		output_devices,
		selected_output,
		L"USB DAC",
		priorities,
		true);

	expect(decision.has_resolved_output, "sync decision should preserve inactive selected output");
	expect(decision.should_mute, "inactive selected output should mute processing");
	expect(!decision.should_apply_routing, "inactive selected output should not retarget playback");
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
		selected_output,
		L"",
		priorities,
		true);

	expect(decision.has_resolved_output, "sync decision should fall back to a preferred active output");
	expect(decision.resolved_output.pwszID == L"spk", "fallback should follow configured priority");
	expect(decision.output_changed, "fallback to another output should count as an output change");
	expect(decision.should_apply_routing, "fallback to a new active output should apply routing");
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
	expect(decision.should_retarget_playback, "manual selection should retarget untargeted output");
	expect(decision.should_restart_processing, "manual selection should restart processing after inactive selection");
	expect(decision.should_begin_grace_period, "manual selection should begin grace period when processing restarts");
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
	expect(!decision.should_retarget_playback, "default output should not be retargeted while processing is off");
	expect(!decision.should_restart_processing, "processing-off selection should not restart processing");
	expect(!decision.should_begin_grace_period, "processing-off selection should not start a grace period");
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
		runTest("same output matches reconnected endpoint", testAreSameOutputDeviceMatchesReconnectedEndpoint);
		runTest("resolve selected output returns reconnected device", testResolveSelectedOutputReturnsReconnectedDevice);
		runTest("preferred output uses configured priority", testGetPreferredOutputUsesConfiguredPriority);
		runTest("ignore device change for unselected active device", testShouldIgnoreDeviceChangeForUnselectedActiveDevice);
		runTest("do not ignore device change for selected output", testShouldNotIgnoreDeviceChangeForSelectedOutput);
		runTest("do not ignore device change when selected output is inactive", testShouldNotIgnoreDeviceChangeWhenSelectedOutputIsInactive);
		runTest("sync decision routes active untargeted output", testBuildSyncDecisionRequestsRoutingForActiveUntargetedOutput);
		runTest("sync decision mutes inactive selected output", testBuildSyncDecisionMutesInactiveSelectedOutput);
		runTest("sync decision falls back to preferred output", testBuildSyncDecisionFallsBackToPreferredOutput);
		runTest("manual selection restarts processing after inactive selection", testManualSelectionDecisionRestartsProcessingAfterInactiveSelection);
		runTest("manual selection leaves default output untouched when processing is off", testManualSelectionDecisionLeavesDefaultOutputUntouchedWhenProcessingIsOff);
		runTest("manual selection powers off when output is missing", testManualSelectionDecisionPowersOffWhenOutputIsMissing);
		runTest("scenario keeps selected active output across unrelated reconnect", testScenarioKeepsSelectedActiveOutputAcrossUnrelatedReconnect);
		runTest("scenario restores reconnected selected output", testScenarioRestoresReconnectedSelectedOutput);
		runTest("scenario manual selection recovers from inactive output", testScenarioManualSelectionRecoversFromInactiveOutput);
	}
	catch (const std::exception& exception)
	{
		std::cerr << "[fail] " << exception.what() << std::endl;
		return 1;
	}

	return 0;
}
