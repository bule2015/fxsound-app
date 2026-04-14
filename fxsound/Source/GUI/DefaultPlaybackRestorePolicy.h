#pragma once

#include "AudioPassthru.h"

namespace FxSound::DefaultPlaybackRestorePolicy
{
	inline bool shouldAttemptRestore(bool require_power_state, bool power_state)
	{
		return !require_power_state || power_state;
	}

	inline bool shouldMarkRestoreAttempted(const RestoreDefaultPlaybackDeviceResult& result)
	{
		return result.attempted && result.succeeded;
	}
}
