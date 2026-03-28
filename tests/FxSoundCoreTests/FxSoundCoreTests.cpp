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
	}
	catch (const std::exception& exception)
	{
		std::cerr << "[fail] " << exception.what() << std::endl;
		return 1;
	}

	return 0;
}
