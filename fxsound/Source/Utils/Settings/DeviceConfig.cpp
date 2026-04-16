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

#include "DeviceConfig.h"
#include "Settings.h"
#include "../../GUI/OutputDeviceSelection.h"

namespace FxSound
{
    namespace
    {
        bool matchesDeviceConfig(const DeviceConfig& device_config, const SoundDevice& sound_device)
        {
            return OutputDeviceSelection::matchesStoredOutputIdentity(
                OutputDeviceSelection::StoredOutputIdentity {
                    std::wstring(device_config.device_id.toWideCharPointer()),
                    std::wstring(device_config.device_name.toWideCharPointer()),
                    std::wstring(device_config.container_id.toWideCharPointer())
                },
                sound_device);
        }

        bool matchesDeviceConfigKey(const DeviceConfig& lhs, const DeviceConfig& rhs)
        {
            return (!lhs.device_id.isEmpty() && lhs.device_id == rhs.device_id) ||
                (!lhs.container_id.isEmpty() &&
                 !rhs.container_id.isEmpty() &&
                 lhs.container_id == rhs.container_id &&
                 lhs.device_name == rhs.device_name) ||
                (lhs.container_id.isEmpty() &&
                 rhs.container_id.isEmpty() &&
                 lhs.device_name == rhs.device_name);
        }
    }

    void DeviceConfig::initDeviceConfigs(Settings& settings, const std::vector<SoundDevice>& sound_devices)
    {
        juce::Array<DeviceConfig> device_configs;
        auto priorities = OutputDeviceSelection::buildInitialOutputPriorities(sound_devices);
        for (const auto& priority : priorities)
        {
            DeviceConfig device_config = { priority.device_id.c_str(), priority.device_name.c_str(), priority.container_id.c_str(), "" };
            device_configs.add(device_config);
        }

        saveDeviceConfigs(settings, "device_configs", device_configs);
    }

    void DeviceConfig::updateDeviceConfigs(Settings& settings, const std::vector<SoundDevice>& sound_devices)
    {
        juce::Array<DeviceConfig> device_configs = loadDeviceConfigs(settings, "device_configs");
        std::vector<OutputDeviceSelection::PriorityEntry> existing_priorities;
        existing_priorities.reserve(static_cast<size_t>(device_configs.size()));
        for (const auto& device_config : device_configs)
        {
            existing_priorities.push_back({
                device_config.device_id.toWideCharPointer(),
                device_config.device_name.toWideCharPointer(),
                device_config.container_id.toWideCharPointer()
                });
        }

        auto merge_result = OutputDeviceSelection::mergeOutputPriorities(existing_priorities, sound_devices);
        if (merge_result.changed)
        {
            juce::Array<DeviceConfig> merged_device_configs;
            merged_device_configs.ensureStorageAllocated(static_cast<int>(merge_result.priorities.size()));
            for (const auto& priority : merge_result.priorities)
            {
                auto existing_config = std::find_if(device_configs.begin(), device_configs.end(),
                    [&priority](const DeviceConfig& device_config)
                    {
                        SoundDevice sound_device;
                        sound_device.pwszID = priority.device_id;
                        sound_device.deviceFriendlyName = priority.device_name;
                        sound_device.containerId = priority.container_id;
                        return matchesDeviceConfig(device_config, sound_device);
                    });

                DeviceConfig device_config {
                    priority.device_id.c_str(),
                    priority.device_name.c_str(),
                    priority.container_id.c_str(),
                    existing_config != device_configs.end() ? existing_config->preset : juce::String()
                };
                merged_device_configs.add(device_config);
            }

            device_configs = merged_device_configs;
            saveDeviceConfigs(settings, "device_configs", device_configs);
        }
    }

    DeviceConfig DeviceConfig::getDeviceConfig(Settings& settings, const SoundDevice& sound_device)
    {
        juce::Array<DeviceConfig> device_configs = loadDeviceConfigs(settings, "device_configs");

        for (const auto& device_config : device_configs)
        {
            if (matchesDeviceConfig(device_config, sound_device))
            {
                return device_config;
            }
        }

        return {};
    }

    juce::var DeviceConfig::toJson(const DeviceConfig& device_config)
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty("device_id", device_config.device_id);
        obj->setProperty("device_name", device_config.device_name);
        obj->setProperty("container_id", device_config.container_id);
        obj->setProperty("preset", device_config.preset);

        return juce::var(obj);
    }

    DeviceConfig DeviceConfig::fromJson(const juce::var& v)
    {
        DeviceConfig device_config;

        if (auto* obj = v.getDynamicObject())
        {
            device_config.device_id = obj->getProperty("device_id").toString();
            device_config.device_name = obj->getProperty("device_name").toString();
            device_config.container_id = obj->getProperty("container_id").toString();
            device_config.preset = obj->getProperty("preset").toString();
        }

        return device_config;
    }

    juce::Array<DeviceConfig> DeviceConfig::removeDuplicates(const juce::Array<DeviceConfig>& device_configs)
    {
        juce::Array<DeviceConfig> result;
        for (const auto& device_config : device_configs)
        {
            auto existing = std::find_if(result.begin(), result.end(),
                [&device_config](const DeviceConfig& existing_config)
                {
                    return matchesDeviceConfigKey(existing_config, device_config);
                });

            if (existing == result.end())
            {
                result.add(device_config);
            }
        }

        return result;
    }

    juce::Array<DeviceConfig> DeviceConfig::loadDeviceConfigs(Settings& settings, juce::StringRef key)
    {
        juce::Array<DeviceConfig> device_configs;

        auto json = settings.getJson(key);
        if (!json.isArray())
            return device_configs;

        const auto& jsonArray = *json.getArray();

        for (const auto& device_config : jsonArray)
        {
            device_configs.add(fromJson(device_config));
        }

        return removeDuplicates(device_configs);
    }

    void DeviceConfig::saveDeviceConfigs(Settings& settings, juce::StringRef key, const juce::Array<DeviceConfig>& device_configs)
    {
        juce::Array<juce::var> jsonArray;
        jsonArray.ensureStorageAllocated(device_configs.size());

        for (const auto& device_config : device_configs)
        {
            jsonArray.add(toJson(device_config));
        }

        settings.setJson(key, juce::var(jsonArray));
    }
}
