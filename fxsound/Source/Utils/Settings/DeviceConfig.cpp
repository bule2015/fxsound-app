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
    void DeviceConfig::initDeviceConfigs(Settings& settings, std::vector<SoundDevice>& sound_devices)
    {
        juce::Array<DeviceConfig> device_configs;
        auto priorities = OutputDeviceSelection::buildInitialOutputPriorities(sound_devices);
        for (const auto& priority : priorities)
        {
            DeviceConfig device_config = { priority.device_id.c_str(), priority.device_name.c_str(), "" };
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
                device_config.device_name.toWideCharPointer()
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
                        return device_config.device_name == priority.device_name.c_str();
                    });

                DeviceConfig device_config {
                    priority.device_id.c_str(),
                    priority.device_name.c_str(),
                    existing_config != device_configs.end() ? existing_config->preset : juce::String()
                };
                merged_device_configs.add(device_config);
            }

            device_configs = merged_device_configs;
            saveDeviceConfigs(settings, "device_configs", device_configs);
        }
    }

    DeviceConfig DeviceConfig::getDeviceConfig(Settings& settings, juce::String device_name)
    {
        juce::Array<DeviceConfig> device_configs = loadDeviceConfigs(settings, "device_configs");

        for (auto device_config : device_configs)
        {
            if (device_config.device_name == device_name)
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
            device_config.preset = obj->getProperty("preset").toString();
        }

        return device_config;
    }

    juce::Array<DeviceConfig> DeviceConfig::removeDuplicates(const juce::Array<DeviceConfig>& device_configs)
    {
        juce::Array<DeviceConfig> result;
        juce::StringArray duplicate_names;

        for (const auto& device_config : device_configs)
        {
            if (!duplicate_names.contains(device_config.device_name))
            {
                duplicate_names.add(device_config.device_name);
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
