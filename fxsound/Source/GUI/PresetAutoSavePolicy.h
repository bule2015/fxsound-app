#pragma once

#include <algorithm>
#include <cwctype>
#include <string>
#include <vector>

namespace FxSound
{
	namespace PresetAutoSavePolicy
	{
		struct PresetSwitchDecision
		{
			bool should_auto_save_current = false;
			bool should_load_auto_saved_preset = false;
			bool should_mark_loaded_preset_modified = false;
		};

		inline PresetSwitchDecision buildPresetSwitchDecision(bool current_preset_modified,
			int current_preset_index,
			int target_preset_index,
			bool target_has_auto_save)
		{
			PresetSwitchDecision decision;
			decision.should_auto_save_current = current_preset_modified &&
				target_preset_index != current_preset_index;
			decision.should_load_auto_saved_preset = target_has_auto_save;
			decision.should_mark_loaded_preset_modified = target_has_auto_save;
			return decision;
		}

		inline bool shouldKeepAutoSavedPreset(const std::wstring& auto_save_name,
			const std::vector<std::wstring>& preset_names)
		{
			auto equals_ignore_case = [](const std::wstring& lhs, const std::wstring& rhs)
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
			};

			return std::any_of(preset_names.begin(), preset_names.end(),
				[&](const std::wstring& preset_name)
				{
					return equals_ignore_case(preset_name, auto_save_name);
				});
		}
	}
}
