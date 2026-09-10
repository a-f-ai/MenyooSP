/*
* Menyoo PC - Grand Theft Auto V single-player trainer mod
*/
#include "EntityApi.h"

#include "CommandQueue.h"

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
		// anything the caller can address.
		SpoonerEntity& FindOrThrow(int id)
		{
			auto found = std::find_if(EntityDb.begin(), EntityDb.end(),
				[id](const SpoonerEntity& candidate) {
					return candidate.handle.GetHandle() == id;
				});

			if (found == EntityDb.end())
				throw ApiError(404, "no entity with id " + std::to_string(id));

			// A handle can outlive the entity when the game streams it out or
			// a script deletes it; report that instead of touching dead memory.
			if (!found->handle.Exists())
				throw ApiError(410, "entity " + std::to_string(id) + " no longer exists in the world");

			return *found;
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

		void ApplyTransform(SpoonerEntity& entity, const Transform& transform)
		{
			entity.handle.SetPosition(Vector3(transform.x, transform.y, transform.z));
			entity.handle.SetRotation(Vector3(transform.pitch, transform.roll, transform.yaw));
		}
	}

	json ListEntities()
	{
		json entities = json::array();
		for (auto& entity : EntityDb)
		{
			if (!entity.handle.Exists())
				continue;
			entities.push_back(Describe(entity));
		}
		return json{ { "count", entities.size() }, { "entities", std::move(entities) } };
	}

	json GetEntity(int id)
	{
		return Describe(FindOrThrow(id));
	}

	json CreateEntity(const CreateRequest& request)
	{
		Model model(static_cast<Hash>(request.model));

		if (!model.IsInCdImage())
		{
			throw ApiError(422, "model " + IntToHexString(request.model, true) +
				" is not in the game files; install the addon or check the hash");
		}
		if (!model.Load(kModelLoadTimeoutMs))
		{
			throw ApiError(422, "model " + IntToHexString(request.model, true) +
				" failed to load within " + std::to_string(kModelLoadTimeoutMs) + "ms");
		}

		const Vector3 position(request.transform.x, request.transform.y, request.transform.z);
		const Vector3 rotation(request.transform.pitch, request.transform.roll, request.transform.yaw);

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
			throw ApiError(400, "type must be 1 (ped), 2 (vehicle) or 3 (prop)");
		}

		model.Unload();

		if (!spawned.handle.Exists())
		{
			throw ApiError(500, "the game refused to create the entity; the world may be at its "
				"entity limit or the position may be unstreamed");
		}

		spawned.type = type;
		spawned.dynamic = request.dynamic;
		spawned.hashName = request.name.empty()
			? IntToHexString(request.model, true)
			: request.name;
		spawned.handle.FreezePosition(!request.dynamic);
		spawned.handle.SetMissionEntity(true);

		sub::Spooner::EntityManagement::AddEntityToDb(spawned);

		return Describe(spawned);
	}

	json SetTransform(int id, const Transform& transform)
	{
		SpoonerEntity& entity = FindOrThrow(id);
		ApplyTransform(entity, transform);
		return Describe(entity);
	}

	json DeleteEntity(int id)
	{
		SpoonerEntity& entity = FindOrThrow(id);
		// DeleteEntity detaches anything bound to this entity and removes it
		// from the spooner database, so the handle must not be used after this.
		sub::Spooner::EntityManagement::DeleteEntity(entity);
		return json{ { "deleted", id } };
	}
}
