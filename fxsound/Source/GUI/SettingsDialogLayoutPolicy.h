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

#pragma once

#include <array>

namespace FxSound::SettingsDialogLayoutPolicy
{
	inline int clampWidth(int preferred_width, int min_width, int max_width)
	{
		if (max_width < min_width)
		{
			return min_width;
		}

		if (preferred_width < min_width)
		{
			return min_width;
		}

		if (preferred_width > max_width)
		{
			return max_width;
		}

		return preferred_width;
	}

	inline int getPreferredWidth(int min_width, int active_pane_index, const std::array<int, 4>& pane_widths)
	{
		if (active_pane_index < 0 || active_pane_index >= static_cast<int>(pane_widths.size()))
		{
			return min_width;
		}

		auto preferred_width = pane_widths[static_cast<size_t>(active_pane_index)];
		return preferred_width >= min_width ? preferred_width : min_width;
	}

	inline int getPreferredWindowWidth(int min_width, int pane_chrome_width, int active_pane_width)
	{
		auto preferred_width = active_pane_width + pane_chrome_width;
		return preferred_width >= min_width ? preferred_width : min_width;
	}

	inline int getPreferredWindowWidth(int min_width, int pane_chrome_width, int active_pane_index, const std::array<int, 4>& pane_widths)
	{
		return getPreferredWindowWidth(min_width, pane_chrome_width, getPreferredWidth(0, active_pane_index, pane_widths));
	}

	inline int getClampedPreferredWindowWidth(int min_width, int max_width, int pane_chrome_width, int active_pane_width)
	{
		return clampWidth(
			getPreferredWindowWidth(min_width, pane_chrome_width, active_pane_width),
			min_width,
			max_width);
	}

	inline int getClampedPreferredWindowWidth(int min_width, int max_width, int pane_chrome_width, int active_pane_index, const std::array<int, 4>& pane_widths)
	{
		return getClampedPreferredWindowWidth(
			min_width,
			max_width,
			pane_chrome_width,
			getPreferredWidth(0, active_pane_index, pane_widths));
	}

	inline int getPreferredHeight(int min_height, int active_pane_height)
	{
		return active_pane_height >= min_height ? active_pane_height : min_height;
	}

	inline int getPreferredHeight(int min_height, int active_pane_index, const std::array<int, 4>& pane_heights)
	{
		if (active_pane_index < 0 || active_pane_index >= static_cast<int>(pane_heights.size()))
		{
			return min_height;
		}

		return getPreferredHeight(min_height, pane_heights[static_cast<size_t>(active_pane_index)]);
	}

	inline bool shouldResizeWindow(int current_width, int current_height, int target_width, int target_height)
	{
		return current_width != target_width || current_height != target_height;
	}
}
