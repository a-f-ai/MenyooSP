/*
* Menyoo PC - Grand Theft Auto V single-player trainer mod
*/
#include "WorldApi.h"

#include "ModelApi.h"

#include "../macros.h"
#include "../Natives/natives2.h"
#include "../Scripting/Camera.h"
#include "../Scripting/GameplayCamera.h"
#include "../Scripting/GTAentity.h"
#include "../Scripting/GTAped.h"
#include "../Scripting/GTAprop.h"
#include "../Scripting/GTAvehicle.h"
#include "../Scripting/Model.h"
#include "../Scripting/ModelNames.h"
#include "../Scripting/World.h"
#include "../Util/GTAmath.h"
#include "../Util/StringManip.h"
#include "../Submenus/Spooner/SpoonerMode.h"

#include <json/single_include/nlohmann/json.hpp>

#include <string>
#include <vector>

using json = nlohmann::json;

namespace Http::WorldApi
{
	namespace
	{
		std::string Serialise(const json& payload)
		{
			return payload.dump(2, ' ', false, json::error_handler_t::replace);
		}

		Response Ok(const json& payload) { return Response{ 200, Serialise(payload) }; }

		Response Fail(int status, const std::string& message)
		{
			return Response{ status, Serialise(json{ { "error", message } }) };
		}

		json Point(const Vector3& v)
		{
			return json{ { "x", v.x }, { "y", v.y }, { "z", v.z } };
		}

		json Angles(const Vector3& v)
		{
			return json{ { "pitch", v.x }, { "roll", v.y }, { "yaw", v.z } };
		}
	}

	Response GetPlayer()
	{
		GTAped player = PLAYER_PED_ID();
		if (!player.Exists())
			return Fail(503, "the player ped does not exist yet; the game may still be loading");

		const Vector3 position = player.GetPosition();
		Vector3 ground = position;
		const float groundHeight = World::GetGroundHeight(ground);

		json payload{
			{ "position", Point(position) },
			{ "rotation", Angles(player.GetRotation()) },
			{ "heading", player.GetHeading() },
			{ "health", player.GetHealth() },
			{ "groundHeight", groundHeight },
			{ "inVehicle", false },
		};

		GTAvehicle vehicle = player.CurrentVehicle();
		if (vehicle.Exists())
		{
			payload["inVehicle"] = true;
			payload["vehicle"] = json{
				{ "id", vehicle.GetHandle() },
				{ "model", IntToHexString(vehicle.Model().hash, true) },
				{ "name", get_vehicle_model_label(vehicle.Model(), true) },
			};
		}

		return Ok(payload);
	}

	Response GetCamera()
	{
		auto& spoonerCam = sub::Spooner::SpoonerMode::spoonerModeCamera;
		const bool spoonerActive = spoonerCam.IsActive();

		json payload{
			{ "spoonerModeEnabled", sub::Spooner::SpoonerMode::bEnabled },
			{ "spoonerCameraActive", spoonerActive },
			{ "gameplay", json{
				{ "position", Point(GameplayCamera::GetPosition()) },
				{ "rotation", Angles(GameplayCamera::GetRotation()) },
				{ "direction", Point(GameplayCamera::GetDirection()) },
			} },
		};

		if (spoonerActive)
		{
			payload["spooner"] = json{
				{ "position", Point(spoonerCam.GetPosition()) },
				{ "rotation", Angles(spoonerCam.GetRotation()) },
			};
		}

		return Ok(payload);
	}

	Response GetGround(float x, float y, float probeZ)
	{
		Vector3 probe(x, y, probeZ);
		const float found = World::GetGroundHeight(probe);
		if (found <= -1000.0f || found >= 10000.0f)
		{
			return Fail(422, "no ground found below z=" + std::to_string(probeZ) +
				"; the area is probably not streamed in, move the player or the spooner camera near it");
		}
		return Ok(json{
			{ "x", x }, { "y", y }, { "probeZ", probeZ },
			{ "groundZ", found },
			{ "note", "the cast stops at the first surface below probeZ, so a map-mod deck wins over the terrain under it" },
		});
	}

	Response Raycast(float maxDistance)
	{
		auto& spoonerCam = sub::Spooner::SpoonerMode::spoonerModeCamera;
		if (!spoonerCam.IsActive())
		{
			return Fail(409, "the spooner camera is not active; open Object Spooner in Menyoo, "
				"or use /world/player and /world/ground to pick a point instead");
		}

		const Vector3 hit = spoonerCam.RaycastForCoord(Vector2(0.0f, 0.0f), 0, maxDistance, maxDistance);
		return Ok(json{
			{ "point", Point(hit) },
			{ "cameraPosition", Point(spoonerCam.GetPosition()) },
			{ "maxDistance", maxDistance },
		});
	}

	Response GetNearby(float x, float y, float z, float radius, const std::string& type, int limit)
	{
		if (radius <= 0.0f || radius > 500.0f)
			return Fail(400, "radius must be between 0 and 500 metres");

		const Vector3 origin(x, y, z);
		const int cap = limit > 0 ? limit : 100;
		json found = json::array();

		const bool wantPeds = type.empty() || type == "ped";
		const bool wantVehicles = type.empty() || type == "vehicle";
		const bool wantProps = type.empty() || type == "prop";

		if (wantPeds)
		{
			std::vector<GTAped> peds;
			World::GetNearbyPeds(peds, origin, radius);
			for (auto& ped : peds)
			{
				if (static_cast<int>(found.size()) >= cap) break;
				json item{
					{ "id", ped.GetHandle() }, { "type", "ped" },
					{ "model", IntToHexString(ped.Model().hash, true) },
					{ "name", GetPedModelLabel(ped.Model(), true) },
					{ "position", Point(ped.GetPosition()) },
				};
				ModelApi::AddGeometry(item, ped.GetHandle(), ped.Model().hash);
				found.push_back(std::move(item));
			}
		}
		if (wantVehicles)
		{
			std::vector<GTAvehicle> vehicles;
			World::GetNearbyVehicles(vehicles, origin, radius);
			for (auto& vehicle : vehicles)
			{
				if (static_cast<int>(found.size()) >= cap) break;
				json item{
					{ "id", vehicle.GetHandle() }, { "type", "vehicle" },
					{ "model", IntToHexString(vehicle.Model().hash, true) },
					{ "name", get_vehicle_model_label(vehicle.Model(), true) },
					{ "position", Point(vehicle.GetPosition()) },
				};
				ModelApi::AddGeometry(item, vehicle.GetHandle(), vehicle.Model().hash);
				found.push_back(std::move(item));
			}
		}
		if (wantProps)
		{
			std::vector<GTAprop> props;
			World::GetNearbyProps(props, origin, radius);
			for (auto& prop : props)
			{
				if (static_cast<int>(found.size()) >= cap) break;
				json item{
					{ "id", prop.GetHandle() }, { "type", "prop" },
					{ "model", IntToHexString(prop.Model().hash, true) },
					{ "name", get_prop_model_label(prop.Model()) },
					{ "position", Point(prop.GetPosition()) },
				};
				ModelApi::AddGeometry(item, prop.GetHandle(), prop.Model().hash);
				found.push_back(std::move(item));
			}
		}

		return Ok(json{
			{ "origin", Point(origin) }, { "radius", radius },
			{ "count", found.size() }, { "entities", std::move(found) },
			{ "note", "these are world entities, not only the ones the spooner owns. \"size\" is the model box in metres, \"bounds\" the world box it occupies after rotation" },
		});
	}
}
