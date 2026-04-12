#pragma once

#include <cwctype>
#include <string>

namespace FxSound
{
    namespace DevicePresetAssignmentPolicy
    {
        inline bool equalsIgnoreCase(const std::wstring& lhs, const std::wstring& rhs)
        {
            if (lhs.size() != rhs.size())
            {
                return false;
            }

            for (size_t index = 0; index < lhs.size(); ++index)
            {
                if (std::towlower(lhs[index]) != std::towlower(rhs[index]))
                {
                    return false;
                }
            }

            return true;
        }

        inline std::wstring renameAssignedPreset(const std::wstring& assigned_preset_name,
            const std::wstring& old_preset_name,
            const std::wstring& new_preset_name)
        {
            if (old_preset_name.empty() ||
                new_preset_name.empty() ||
                equalsIgnoreCase(old_preset_name, new_preset_name))
            {
                return assigned_preset_name;
            }

            return equalsIgnoreCase(assigned_preset_name, old_preset_name)
                ? new_preset_name
                : assigned_preset_name;
        }
    }
}
