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
	Response GetWater(float x, float y, float probeZ);

	// Where the camera is pointing: the spooner camera when it is up, the
	// gameplay camera otherwise.
	Response Aim(float maxDistance);

	struct RayRequest
	{
		float fromX, fromY, fromZ;
		float toX, toY, toZ;
		int flags;         // IntersectOptions bits; what the probe is allowed to hit
		int ignoreEntity;  // 0 for none
	};

	// A line probe against the world, static map geometry included. This is the
	// only way to see a wall, a roof or a slope: /world/nearby lists entities,
	// and a building is not an entity.
	Response Raycast(const RayRequest& request);

	Response GetNearby(float x, float y, float z, float radius, const std::string& type, int limit);
}
