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
	inline int getPreferredHeight(int min_height, int active_pane_index, const std::array<int, 4>& pane_heights)
	{
		if (active_pane_index < 0 || active_pane_index >= static_cast<int>(pane_heights.size()))
		{
			return min_height;
		}

		auto preferred_height = pane_heights[static_cast<size_t>(active_pane_index)];
		return preferred_height >= min_height ? preferred_height : min_height;
	}

	inline bool shouldResizeWindow(int current_width, int current_height, int target_width, int target_height)
	{
		return current_width != target_width || current_height != target_height;
	}
}
