/*
* Menyoo PC - Grand Theft Auto V single-player trainer mod
*
* The GTA-facing half of the HTTP bridge. Every function here runs on the
* ScriptHookV fiber (the CommandQueue guarantees it) and may call natives.
* Nothing here knows about HTTP.
*
* None of these may throw: an exception on the fiber kills the script. They
* report failure through Response::status instead.
*/
#pragma once

#include "CommandQueue.h"

#include <optional>
#include <string>

namespace Http::EntityApi
{
	struct Vec3
	{
		float x, y, z;
	};

	struct CreateRequest
	{
		int type;              // 1 = PED, 2 = VEHICLE, 3 = PROP
		unsigned long model;
		std::string name;      // optional label shown in the spooner
		Vec3 position;
		Vec3 rotation;         // pitch, roll, yaw
		bool dynamic;
		bool placeOnGround;
	};

	struct PatchRequest
	{
		std::optional<Vec3> position;
		std::optional<Vec3> rotation;
	};

	Response ListEntities();
	Response GetEntity(int id);
	Response CreateEntity(const CreateRequest& request);
	Response PatchEntity(int id, const PatchRequest& request);
	Response DeleteEntity(int id);
}
