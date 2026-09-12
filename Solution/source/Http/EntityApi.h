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
#include <vector>

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
		std::string modelLabel; // what the caller wrote, for error messages
		std::string name;      // label shown in the spooner
		Vec3 position;
		Vec3 rotation;         // pitch, roll, yaw
		bool dynamic;
		// Replace position.z with the surface found by casting down from it.
		// This is why a caller never has to guess a height.
		bool snapToGround;
		bool still;            // peds only: hold position instead of wandering
		std::string scenario;  // peds only
		std::string animDict;  // peds only
		std::string animName;  // peds only
		// The surface the caller believes is under the entity. When set, the spawn
		// casts down from the final position and refuses if the surface it finds is
		// further than `tolerance` from this - the difference between "stood on the
		// platform" and "fell through to the deck below", which is otherwise invisible.
		std::optional<float> expectedSupportZ;
		float tolerance;
	};

	struct SettleQuery
	{
		std::string namePrefix;
		std::string type;
		int frames;      // how long to let physics act
		float epsilon;   // metres of movement that count as "moved"
	};

	struct PatchRequest
	{
		std::optional<Vec3> position;
		std::optional<Vec3> rotation;
		std::optional<bool> snapToGround;
		std::optional<std::string> scenario;
		std::optional<std::string> animDict;
		std::optional<std::string> animName;
	};

	struct ListQuery
	{
		std::string namePrefix;
		std::string type;      // "", "ped", "vehicle", "prop"
		int limit;
		int offset;
	};

	// For callers already on the script thread (the character picker): the same
	// spawn as POST /entities, without the JSON round trip.
	bool CreateDirect(const CreateRequest& request, int& idOut, std::string& failure);

	Response ListEntities(const ListQuery& query);
	Response GetEntity(int id);
	Response CreateEntity(const CreateRequest& request);
	Response CreateBatch(const std::vector<CreateRequest>& requests);
	Response PatchEntity(int id, const PatchRequest& request);
	Response DeleteEntity(int id);
	Response DeleteMatching(const std::string& namePrefix, const std::string& type);

	// Waits `frames` frames and reports which matching entities moved. A measurement
	// only: nothing is corrected, so a hovering or sinking ped shows up as movement.
	Response Settle(const SettleQuery& query);
}
