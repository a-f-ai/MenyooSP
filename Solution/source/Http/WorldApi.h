/*
* Menyoo PC - Grand Theft Auto V single-player trainer mod
*
* Perception: what the agent needs in order to place things somewhere real
* instead of guessing coordinates. Runs on the fiber; never throws.
*/
#pragma once

#include "CommandQueue.h"

#include <string>

namespace Http::WorldApi
{
	Response GetPlayer();
	Response GetCamera();

	// Casts down from (x, y, probeZ) and reports the first surface below.
	Response GetGround(float x, float y, float probeZ);

	// Where the spooner camera is pointing. Requires spooner mode to be on.
	Response Raycast(float maxDistance);

	Response GetNearby(float x, float y, float z, float radius, const std::string& type, int limit);
}
