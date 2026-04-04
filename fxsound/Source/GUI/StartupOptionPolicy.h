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

#include <string>
#include <vector>

namespace FxSound
{
	namespace StartupOptionPolicy
	{
		inline bool containsExactOption(const std::vector<std::wstring>& arguments, const std::wstring& option)
		{
			for (const auto& argument : arguments)
			{
				if (argument == option)
				{
					return true;
				}
			}

			return false;
		}

		inline bool shouldEnableOutputLatencyLogging(const std::vector<std::wstring>& arguments)
		{
			return containsExactOption(arguments, L"--measure-output-latency");
		}
	}
}
