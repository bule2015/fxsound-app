/*
FxSound
Copyright (C) 2025  FxSound LLC

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

#include "sndDevices.h"

namespace FxSound
{
	namespace SndDevicesBufferPolicy
	{
		inline bool isLegacyDefaultBufferSize(int buffer_size_msecs)
		{
			switch (buffer_size_msecs)
			{
			case SND_DEVICES_CAPTURE_BUFFER_DEFAULT_SIZE_MILLI_SECS_32BIT_OS_32BIT_CPU:
			case SND_DEVICES_CAPTURE_BUFFER_DEFAULT_SIZE_MILLI_SECS_32BIT_VISTA_32BIT_CPU:
			case SND_DEVICES_CAPTURE_BUFFER_DEFAULT_SIZE_MILLI_SECS_32BIT_OS_64BIT_CPU:
			case SND_DEVICES_CAPTURE_BUFFER_DEFAULT_SIZE_MILLI_SECS_64BIT_OS:
				return true;

			default:
				return false;
			}
		}

		inline int clampBufferSizeOrDefault(int buffer_size_msecs)
		{
			if ((buffer_size_msecs < SND_DEVICES_CAPTURE_BUFFER_MIN_SIZE_MILLI_SECS) ||
				(buffer_size_msecs > SND_DEVICES_CAPTURE_BUFFER_MAX_SIZE_MILLI_SECS))
			{
				return SND_DEVICES_CAPTURE_BUFFER_DEFAULT_SIZE_MILLI_SECS;
			}

			return buffer_size_msecs;
		}

		inline int resolveEffectiveDefaultBufferSize(bool has_machine_default_setting, int machine_default_buffer_size_msecs)
		{
			if ((!has_machine_default_setting) || isLegacyDefaultBufferSize(machine_default_buffer_size_msecs))
			{
				return SND_DEVICES_CAPTURE_BUFFER_DEFAULT_SIZE_MILLI_SECS;
			}

			return clampBufferSizeOrDefault(machine_default_buffer_size_msecs);
		}
	}
}
