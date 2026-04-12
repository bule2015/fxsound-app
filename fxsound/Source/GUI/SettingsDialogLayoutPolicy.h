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

namespace FxSound::SettingsDialogLayoutPolicy
{
	enum class PaneId : int
	{
		Audio = 0,
		Equalizer,
		General,
		Help
	};

	inline int getPreferredHeight(int min_height,
		PaneId active_pane,
		int audio_height,
		int equalizer_height,
		int general_height,
		int help_height)
	{
		int preferred_height = min_height;

		switch (active_pane)
		{
		case PaneId::Audio:
			preferred_height = audio_height;
			break;

		case PaneId::Equalizer:
			preferred_height = equalizer_height;
			break;

		case PaneId::General:
			preferred_height = general_height;
			break;

		case PaneId::Help:
			preferred_height = help_height;
			break;
		}

		return preferred_height >= min_height ? preferred_height : min_height;
	}

	inline bool shouldResizeWindow(int current_width, int current_height, int target_width, int target_height)
	{
		return current_width != target_width || current_height != target_height;
	}
}
