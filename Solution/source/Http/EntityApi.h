/*
* Menyoo PC - Grand Theft Auto V single-player trainer mod
*
* The GTA-facing half of the HTTP bridge. Every function here runs on the
* ScriptHookV fiber (the CommandQueue guarantees it) and may call natives.
* Nothing here knows about HTTP.
*/
#pragma once

#include <json/single_include/nlohmann/json.hpp>

#include <string>

namespace Http::EntityApi
{
	// Requests, already parsed and validated by the HTTP layer.
	struct Transform
	{
		float x, y, z;
		float pitch, roll, yaw;
	};

	struct CreateRequest
	{
		int type;              // 1 = PED, 2 = VEHICLE, 3 = PROP
		unsigned long model;
		std::string name;      // optional label shown in the spooner
		Transform transform;
		bool dynamic;
		bool placeOnGround;
	};

	nlohmann::json ListEntities();
	nlohmann::json GetEntity(int id);
	nlohmann::json CreateEntity(const CreateRequest& request);
	nlohmann::json SetTransform(int id, const Transform& transform);
	nlohmann::json DeleteEntity(int id);
}
