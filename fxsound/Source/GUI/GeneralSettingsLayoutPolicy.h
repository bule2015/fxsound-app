#pragma once

namespace FxSound::GeneralSettingsLayoutPolicy
{
	inline int getPreferredWidth(
		int x_margin,
		int language_switch_width,
		int widest_toggle_width,
		int hotkey_label_x,
		int widest_hotkey_label_width,
		int hotkey_column_gap,
		int widest_hotkey_editor_width,
		int trailing_margin)
	{
		auto preferred_width = x_margin + language_switch_width + trailing_margin;

		const auto toggle_width = x_margin + widest_toggle_width + trailing_margin;
		if (toggle_width > preferred_width)
		{
			preferred_width = toggle_width;
		}

		const auto hotkey_width =
			hotkey_label_x +
			widest_hotkey_label_width +
			hotkey_column_gap +
			widest_hotkey_editor_width +
			trailing_margin;
		if (hotkey_width > preferred_width)
		{
			preferred_width = hotkey_width;
		}

		return preferred_width;
	}
}
