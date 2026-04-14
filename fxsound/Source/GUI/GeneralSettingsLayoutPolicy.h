#pragma once

namespace FxSound::GeneralSettingsLayoutPolicy
{
	struct Metrics
	{
		int language_switch_width = 0;
		int widest_toggle_width = 0;
		int widest_hotkey_label_width = 0;
		int widest_hotkey_editor_width = 0;
	};

	inline int getHotkeyRowWidth(const Metrics& metrics, int hotkey_column_gap)
	{
		return metrics.widest_hotkey_label_width +
			hotkey_column_gap +
			metrics.widest_hotkey_editor_width;
	}

	inline int getPreferredWidth(
		int x_margin,
		int hotkey_label_x,
		int hotkey_column_gap,
		int trailing_margin,
		const Metrics& metrics)
	{
		auto preferred_width = x_margin + metrics.language_switch_width + trailing_margin;

		const auto toggle_width = x_margin + metrics.widest_toggle_width + trailing_margin;
		if (toggle_width > preferred_width)
		{
			preferred_width = toggle_width;
		}

		const auto hotkey_width =
			hotkey_label_x +
			getHotkeyRowWidth(metrics, hotkey_column_gap) +
			trailing_margin;
		if (hotkey_width > preferred_width)
		{
			preferred_width = hotkey_width;
		}

		return preferred_width;
	}
}
