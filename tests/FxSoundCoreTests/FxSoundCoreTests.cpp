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
	}
	catch (const std::exception& exception)
	{
		std::cerr << "[fail] " << exception.what() << std::endl;
		return 1;
	}

	return 0;
}
