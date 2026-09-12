/*
* Menyoo PC - Grand Theft Auto V single-player trainer mod
*/
#include "EntityApi.h"

#include "ModelApi.h"

#include "../macros.h"
#include "../Natives/natives2.h"
#include "../Scripting/GTAentity.h"
#include "../Scripting/GTAped.h"
#include "../Scripting/GTAprop.h"
#include "../Scripting/GTAvehicle.h"
#include "../Scripting/Game.h"
#include "../Scripting/Model.h"
#include "../Scripting/Tasks.h"
#include "../Scripting/World.h"
#include "../Util/GTAmath.h"
#include "../Util/StringManip.h"
#include "../Submenus/Spooner/Databases.h"
#include "../Submenus/Spooner/EntityManagement.h"
#include "../Submenus/Spooner/SpoonerEntity.h"

#include <json/single_include/nlohmann/json.hpp>

#include <algorithm>
#include <string>
#include <unordered_set>
#include <vector>

using json = nlohmann::json;
using GTAmodel::Model;
using sub::Spooner::SpoonerEntity;
using sub::Spooner::Databases::EntityDb;

namespace Http::EntityApi
{
	namespace
	{
		constexpr DWORD kModelLoadTimeoutMs = 4000;
		constexpr int kDefaultListLimit = 200;

		std::string Serialise(const json& payload)
		{
			return payload.dump(2, ' ', false, json::error_handler_t::replace);
		}

		Response Ok(const json& payload)
		{
			return Response{ 200, Serialise(payload) };
		}

		Response Fail(int status, const std::string& message)
		{
			return Response{ status, Serialise(json{ { "error", message } }) };
		}

		std::string TypeName(EntityType type)
		{
			switch (type)
			{
			case EntityType::PED:     return "ped";
			case EntityType::VEHICLE: return "vehicle";
			case EntityType::PROP:    return "prop";
			default:                  return "unknown";
			}
		}

		SpoonerEntity* Find(int id)
		{
			auto found = std::find_if(EntityDb.begin(), EntityDb.end(),
				[id](const SpoonerEntity& candidate) {
					return candidate.handle.GetHandle() == id;
				});
			return found == EntityDb.end() ? nullptr : &*found;
		}

		json Describe(SpoonerEntity& entity)
		{
			const Vector3& position = entity.handle.GetPosition();
			const Vector3& rotation = entity.handle.GetRotation();

			json described{
				{ "id", entity.handle.GetHandle() },
				{ "type", TypeName(entity.type) },
				{ "model", IntToHexString(entity.handle.Model().hash, true) },
				{ "name", entity.hashName },
				{ "dynamic", entity.dynamic },
				{ "frozen", entity.handle.IsPositionFrozen() },
				{ "attached", entity.attachmentArgs.isAttached },
				{ "scenario", entity.currentScenario },
				{ "position", { { "x", position.x }, { "y", position.y }, { "z", position.z } } },
				{ "rotation", { { "pitch", rotation.x }, { "roll", rotation.y }, { "yaw", rotation.z } } },
			};

			ModelApi::AddGeometry(described, entity.handle.GetHandle(), entity.handle.Model().hash);
			return described;
		}

		// Casts down from the requested point. Passing a z above the surface is
		// what makes this work on map-mod decks floating over the terrain: the
		// cast stops at the first surface below, which is the deck, not the
		// island underneath it.
		bool ResolveGround(Vector3& position)
		{
			const float found = World::GetGroundHeight(position);
			if (found <= -1000.0f || found >= 10000.0f)
				return false;
			position.z = found;
			return true;
		}

		void ApplyPedBehaviour(GTAped ped, const std::string& scenario,
			const std::string& animDict, const std::string& animName, bool still)
		{
			ped.SetBlockPermanentEvent(still);
			SET_PED_CAN_PLAY_AMBIENT_ANIMS(ped.Handle(), true);
			SET_PED_CAN_PLAY_AMBIENT_BASE_ANIMS(ped.Handle(), true);
			SET_PED_CAN_PLAY_GESTURE_ANIMS(ped.Handle(), true);
			SET_PED_IS_IGNORED_BY_AUTO_OPEN_DOORS(ped.Handle(), true);

			if (!scenario.empty())
			{
				ped.Task().StartScenario(scenario);
			}
			else if (!animDict.empty() && !animName.empty())
			{
				Game::RequestAnimDict(animDict, 1500);
				ped.Task().PlayAnimation(animDict, animName);
			}
		}

		// Spawning leaves the entity registered with the spooner, so anything
		// placed through the API is visible in Menyoo's own menu and is written
		// out by its map save.
		bool Spawn(const CreateRequest& request, SpoonerEntity& out, std::string& failure)
		{
			Vector3 position(request.position.x, request.position.y, request.position.z);
			const Vector3 rotation(request.rotation.x, request.rotation.y, request.rotation.z);

			if (request.snapToGround && !ResolveGround(position))
			{
				failure = "no ground found below z=" + std::to_string(request.position.z) +
					" at (" + std::to_string(request.position.x) + ", " +
					std::to_string(request.position.y) + "); the area may not be streamed in";
				return false;
			}

			Model model(static_cast<Hash>(request.model));
			const auto type = static_cast<EntityType>(request.type);

			switch (type)
			{
			case EntityType::PED:
				out.handle = World::CreatePed(model, position, rotation, false);
				break;
			case EntityType::VEHICLE:
				out.handle = World::CreateVehicle(model, position, rotation, false);
				break;
			case EntityType::PROP:
				out.handle = World::CreateProp(model, position, rotation, request.dynamic, false);
				break;
			default:
				failure = "type must be 1 (ped), 2 (vehicle) or 3 (prop)";
				return false;
			}

			if (!out.handle.Exists())
			{
				failure = "the game refused to create the entity; the world may be at its entity "
					"limit or the position may be unstreamed";
				return false;
			}

			out.type = type;
			out.dynamic = request.dynamic;
			out.hashName = request.name.empty()
				? (request.modelLabel.empty() ? IntToHexString(request.model, true) : request.modelLabel)
				: request.name;
			out.handle.FreezePosition(!request.dynamic);
			out.handle.SetMissionEntity(true);
			out.handle.SetLODDistance(1000000);

			if (type == EntityType::PED)
			{
				out.isStill = request.still;
				out.currentScenario = request.scenario;
				ApplyPedBehaviour(out.handle, request.scenario, request.animDict, request.animName,
					request.still);
			}

			sub::Spooner::EntityManagement::AddEntityToDb(out);
			return true;
		}

		// Requesting every distinct model up front turns a batch's worst case
		// from "one blocking load per entity" into a single shared wait.
		json PreloadModels(const std::vector<CreateRequest>& requests,
			std::unordered_set<Hash>& usable)
		{
			std::unordered_set<Hash> wanted;
			json rejected = json::object();

			for (const auto& request : requests)
			{
				const Hash hash = static_cast<Hash>(request.model);
				if (wanted.count(hash))
					continue;
				wanted.insert(hash);

				Model model(hash);
				if (!model.IsInCdImage())
				{
					rejected[IntToHexString(hash, true)] =
						"model is not in the game files; install the addon or check the name";
					continue;
				}
				model.Load();
			}

			const DWORD deadline = GetTickCount() + kModelLoadTimeoutMs;
			for (;;)
			{
				bool allReady = true;
				for (Hash hash : wanted)
				{
					if (rejected.contains(IntToHexString(hash, true)))
						continue;
					if (!Model(hash).IsLoaded())
					{
						allReady = false;
						break;
					}
				}
				if (allReady || GetTickCount() > deadline)
					break;
				WAIT(0);
			}

			for (Hash hash : wanted)
			{
				const std::string key = IntToHexString(hash, true);
				if (rejected.contains(key))
					continue;
				if (Model(hash).IsLoaded())
					usable.insert(hash);
				else
					rejected[key] = "model did not stream in within " +
						std::to_string(kModelLoadTimeoutMs) + "ms";
			}

			return rejected;
		}

		bool MatchesFilter(SpoonerEntity& entity, const std::string& namePrefix, const std::string& type)
		{
			if (!namePrefix.empty() && entity.hashName.rfind(namePrefix, 0) != 0)
				return false;
			if (!type.empty() && TypeName(entity.type) != type)
				return false;
			return true;
		}
	}

	Response ListEntities(const ListQuery& query)
	{
		const int limit = query.limit > 0 ? query.limit : kDefaultListLimit;
		json entities = json::array();
		int matched = 0;

		for (auto& entity : EntityDb)
		{
			if (!entity.handle.Exists() || !MatchesFilter(entity, query.namePrefix, query.type))
				continue;
			++matched;
			if (matched <= query.offset)
				continue;
			if (static_cast<int>(entities.size()) >= limit)
				continue;
			entities.push_back(Describe(entity));
		}

		return Ok(json{
			{ "matched", matched },
			{ "returned", entities.size() },
			{ "offset", query.offset },
			{ "limit", limit },
			{ "entities", std::move(entities) },
		});
	}

	Response GetEntity(int id)
	{
		SpoonerEntity* entity = Find(id);
		if (entity == nullptr)
			return Fail(404, "no entity with id " + std::to_string(id));
		if (!entity->handle.Exists())
			return Fail(410, "entity " + std::to_string(id) + " no longer exists in the world");
		return Ok(Describe(*entity));
	}

	Response CreateEntity(const CreateRequest& request)
	{
		Model model(static_cast<Hash>(request.model));
		const std::string label = request.modelLabel.empty()
			? IntToHexString(request.model, true) : request.modelLabel;

		if (!model.IsInCdImage())
			return Fail(422, "model " + label + " is not in the game files; install the addon or check the name");
		if (!model.Load(kModelLoadTimeoutMs))
			return Fail(422, "model " + label + " failed to load within " +
				std::to_string(kModelLoadTimeoutMs) + "ms");

		SpoonerEntity spawned;
		std::string failure;
		const bool created = Spawn(request, spawned, failure);
		model.Unload();

		if (!created)
			return Fail(422, failure);
		return Response{ 201, Serialise(Describe(spawned)) };
	}

	Response CreateBatch(const std::vector<CreateRequest>& requests)
	{
		std::unordered_set<Hash> usable;
		const json rejectedModels = PreloadModels(requests, usable);

		json created = json::array();
		json failures = json::array();

		for (size_t index = 0; index < requests.size(); ++index)
		{
			const CreateRequest& request = requests[index];
			const Hash hash = static_cast<Hash>(request.model);
			const std::string label = request.modelLabel.empty()
				? IntToHexString(hash, true) : request.modelLabel;

			if (!usable.count(hash))
			{
				const std::string key = IntToHexString(hash, true);
				failures.push_back(json{
					{ "index", index },
					{ "model", label },
					{ "error", rejectedModels.contains(key)
						? rejectedModels.at(key).get<std::string>()
						: std::string("model unavailable") },
				});
				continue;
			}

			SpoonerEntity spawned;
			std::string failure;
			if (Spawn(request, spawned, failure))
				created.push_back(Describe(spawned));
			else
				failures.push_back(json{ { "index", index }, { "model", label }, { "error", failure } });

			// Yielding keeps a large batch from stalling the frame. Drain
			// guards against the nested call this causes.
			WAIT(0);
		}

		for (Hash hash : usable)
			Model(hash).Unload();

		const int status = failures.empty() ? 201 : 207;
		return Response{ status, Serialise(json{
			{ "requested", requests.size() },
			{ "created", created.size() },
			{ "failed", failures.size() },
			{ "entities", std::move(created) },
			{ "failures", std::move(failures) },
		}) };
	}

	Response PatchEntity(int id, const PatchRequest& request)
	{
		SpoonerEntity* entity = Find(id);
		if (entity == nullptr)
			return Fail(404, "no entity with id " + std::to_string(id));
		if (!entity->handle.Exists())
			return Fail(410, "entity " + std::to_string(id) + " no longer exists in the world");

		if (request.position.has_value())
		{
			const Vec3& target = *request.position;
			Vector3 position(target.x, target.y, target.z);
			if (request.snapToGround.value_or(false) && !ResolveGround(position))
				return Fail(422, "no ground found below z=" + std::to_string(target.z));
			entity->handle.SetPosition(position);
		}
		else if (request.snapToGround.value_or(false))
		{
			Vector3 position = entity->handle.GetPosition();
			if (!ResolveGround(position))
				return Fail(422, "no ground found below the entity");
			entity->handle.SetPosition(position);
		}

		if (request.rotation.has_value())
		{
			const Vec3& target = *request.rotation;
			entity->handle.SetRotation(Vector3(target.x, target.y, target.z));
		}

		if (entity->type == EntityType::PED)
		{
			if (request.scenario.has_value())
			{
				entity->currentScenario = *request.scenario;
				if (!request.scenario->empty())
					GTAped(entity->handle).Task().StartScenario(*request.scenario);
			}
			if (request.animDict.has_value() && request.animName.has_value() &&
				!request.animDict->empty() && !request.animName->empty())
			{
				Game::RequestAnimDict(*request.animDict, 1500);
				GTAped(entity->handle).Task().PlayAnimation(*request.animDict, *request.animName);
			}
		}

		return Ok(Describe(*entity));
	}

	Response DeleteEntity(int id)
	{
		SpoonerEntity* entity = Find(id);
		if (entity == nullptr)
			return Fail(404, "no entity with id " + std::to_string(id));

		sub::Spooner::EntityManagement::DeleteEntity(*entity);
		return Ok(json{ { "deleted", json::array({ id }) }, { "count", 1 } });
	}

	Response DeleteMatching(const std::string& namePrefix, const std::string& type)
	{
		// Collecting first, because deleting rewrites the database underneath us.
		std::vector<int> doomed;
		for (auto& entity : EntityDb)
		{
			if (MatchesFilter(entity, namePrefix, type))
				doomed.push_back(entity.handle.GetHandle());
		}

		json deleted = json::array();
		for (int id : doomed)
		{
			SpoonerEntity* entity = Find(id);
			if (entity == nullptr)
				continue;
			sub::Spooner::EntityManagement::DeleteEntity(*entity);
			deleted.push_back(id);
		}

		return Ok(json{ { "count", deleted.size() }, { "deleted", std::move(deleted) } });
	}
}
