/*
* Menyoo PC - Grand Theft Auto V single-player trainer mod
*/
#include "EntityApi.h"

#include "../macros.h"
#include "../Natives/natives2.h"
#include "../Scripting/GTAentity.h"
#include "../Scripting/GTAped.h"
#include "../Scripting/GTAprop.h"
#include "../Scripting/GTAvehicle.h"
#include "../Scripting/Model.h"
#include "../Scripting/World.h"
#include "../Util/GTAmath.h"
#include "../Util/StringManip.h"
#include "../Submenus/Spooner/Databases.h"
#include "../Submenus/Spooner/EntityManagement.h"
#include "../Submenus/Spooner/SpoonerEntity.h"

#include <json/single_include/nlohmann/json.hpp>

#include <algorithm>
#include <string>

using json = nlohmann::json;
using GTAmodel::Model;
using sub::Spooner::SpoonerEntity;
using sub::Spooner::Databases::EntityDb;

namespace Http::EntityApi
{
	namespace
	{
		constexpr DWORD kModelLoadTimeoutMs = 2000;

		// Entity names come from the game and from map XML, which is declared
		// ISO-8859-1 and can hold arbitrary bytes. Replacing invalid sequences
		// keeps dump() from throwing on this fiber.
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

		// The live script handle is the identity we hand to callers. It is
		// unique while the entity exists, which is exactly the lifetime of
		// anything a caller can address.
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

			return json{
				{ "id", entity.handle.GetHandle() },
				{ "type", TypeName(entity.type) },
				{ "model", IntToHexString(entity.handle.Model().hash, true) },
				{ "name", entity.hashName },
				{ "dynamic", entity.dynamic },
				{ "frozen", entity.handle.IsPositionFrozen() },
				{ "attached", entity.attachmentArgs.isAttached },
				{ "position", { { "x", position.x }, { "y", position.y }, { "z", position.z } } },
				{ "rotation", { { "pitch", rotation.x }, { "roll", rotation.y }, { "yaw", rotation.z } } },
			};
		}
	}

	Response ListEntities()
	{
		json entities = json::array();
		for (auto& entity : EntityDb)
		{
			if (!entity.handle.Exists())
				continue;
			entities.push_back(Describe(entity));
		}
		return Ok(json{ { "count", entities.size() }, { "entities", std::move(entities) } });
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
		const std::string modelLabel = IntToHexString(request.model, true);

		if (!model.IsInCdImage())
		{
			return Fail(422, "model " + modelLabel +
				" is not in the game files; install the addon or check the hash");
		}
		if (!model.Load(kModelLoadTimeoutMs))
		{
			return Fail(422, "model " + modelLabel + " failed to load within " +
				std::to_string(kModelLoadTimeoutMs) + "ms");
		}

		const Vector3 position(request.position.x, request.position.y, request.position.z);
		const Vector3 rotation(request.rotation.x, request.rotation.y, request.rotation.z);

		SpoonerEntity spawned;
		const auto type = static_cast<EntityType>(request.type);

		switch (type)
		{
		case EntityType::PED:
			spawned.handle = World::CreatePed(model, position, rotation, request.placeOnGround);
			break;
		case EntityType::VEHICLE:
			spawned.handle = World::CreateVehicle(model, position, rotation, request.placeOnGround);
			break;
		case EntityType::PROP:
			spawned.handle = World::CreateProp(model, position, rotation, request.dynamic, request.placeOnGround);
			break;
		default:
			model.Unload();
			return Fail(400, "type must be 1 (ped), 2 (vehicle) or 3 (prop)");
		}

		model.Unload();

		if (!spawned.handle.Exists())
		{
			return Fail(500, "the game refused to create the entity; the world may be at its "
				"entity limit or the position may be unstreamed");
		}

		spawned.type = type;
		spawned.dynamic = request.dynamic;
		spawned.hashName = request.name.empty() ? modelLabel : request.name;
		spawned.handle.FreezePosition(!request.dynamic);
		spawned.handle.SetMissionEntity(true);

		sub::Spooner::EntityManagement::AddEntityToDb(spawned);

		return Response{ 201, Serialise(Describe(spawned)) };
	}

	Response PatchEntity(int id, const PatchRequest& request)
	{
		SpoonerEntity* entity = Find(id);
		if (entity == nullptr)
			return Fail(404, "no entity with id " + std::to_string(id));
		if (!entity->handle.Exists())
			return Fail(410, "entity " + std::to_string(id) + " no longer exists in the world");

		// Reading the current transform here, rather than on the HTTP thread,
		// keeps a partial patch to one round trip and leaves the untouched
		// axis exactly as the game has it.
		if (request.position.has_value())
		{
			const Vec3& target = *request.position;
			entity->handle.SetPosition(Vector3(target.x, target.y, target.z));
		}
		if (request.rotation.has_value())
		{
			const Vec3& target = *request.rotation;
			entity->handle.SetRotation(Vector3(target.x, target.y, target.z));
		}

		return Ok(Describe(*entity));
	}

	Response DeleteEntity(int id)
	{
		SpoonerEntity* entity = Find(id);
		if (entity == nullptr)
			return Fail(404, "no entity with id " + std::to_string(id));

		// DeleteEntity detaches anything bound to this entity and removes it
		// from the spooner database, so the handle must not be used after this.
		sub::Spooner::EntityManagement::DeleteEntity(*entity);
		return Ok(json{ { "deleted", id } });
	}
}
