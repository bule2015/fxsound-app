#pragma once

#include <string>
#include <string_view>

namespace FxSound::OutputPresetSelectionPolicy
{
    constexpr int kNoPresetId = 1;

    template <typename GetPresetName>
    inline std::wstring getPresetNameForSelectedId(int selected_id,
        int preset_count,
        GetPresetName&& get_preset_name)
    {
        const auto preset_index = selected_id - kNoPresetId - 1;
        if (preset_index < 0 || preset_index >= preset_count)
        {
            return {};
        }

        return std::wstring(get_preset_name(preset_index));
    }

    template <typename GetPresetName>
    inline int getSelectedIdForPresetName(std::wstring_view preset_name,
        int preset_count,
        GetPresetName&& get_preset_name)
    {
        if (preset_name.empty())
        {
            return 0;
        }

        for (int preset_index = 0; preset_index < preset_count; ++preset_index)
        {
            if (get_preset_name(preset_index) == preset_name)
            {
                return preset_index + kNoPresetId + 1;
            }
        }

        return 0;
    }
}
