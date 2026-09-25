/*
* Menyoo PC - Grand Theft Auto V single-player trainer mod
*/
#include "ModelApi.h"

#include "../macros.h"
#include "../Natives/natives2.h"
#include "../Scripting/Model.h"
#include "../Util/StringManip.h"

#include <algorithm>
#include <string>
#include <unordered_set>
#include <vector>

using json = nlohmann::json;
using GTAmodel::Model;

namespace Http::ModelApi
{
	namespace
	{
		constexpr DWORD kLoadTimeoutMs = 6000;

		// A decal or a painted line is legitimately flat, so one positive
		// extent is enough to call the box real. All three at zero means the
		// game has nothing for this model.
		constexpr float kMinExtent = 0.001f;

		std::string Serialise(const json& payload)
		{
			return payload.dump(2, ' ', false, json::error_handler_t::replace);
		}

		json Point(const Vector3& v)
		{
			return json{ { "x", v.x }, { "y", v.y }, { "z", v.z } };
		}
	}

	Box GetBox(unsigned long model)
	{
		Vector3_t min{}, max{};
		GET_MODEL_DIMENSIONS(static_cast<Hash>(model), &min, &max);

		Box box;
		box.min = min;
		box.max = max;
		box.valid = (max.x - min.x) > kMinExtent
			|| (max.y - min.y) > kMinExtent
			|| (max.z - min.z) > kMinExtent;
		return box;
	}

	void AddGeometry(json& target, int entityHandle, unsigned long model)
	{
		const Box box = GetBox(model);
		if (!box.valid)
		{
			target["geometryError"] = "the game reports an empty bounding box for model " +
				IntToHexString(model, true);
			return;
		}

		target["size"] = Point(box.max - box.min);

		// The eight corners through the entity's own transform, so a rotated
		// object reports the world box it actually occupies. One native per
		// corner is unambiguous about which axis is which, unlike reading the
		// entity matrix and deciding whether the first vector is right or
		// forward.
		Vector3 lo, hi;
		for (int corner = 0; corner < 8; ++corner)
		{
			const Vector3 world = GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS(entityHandle,
				(corner & 1) ? box.max.x : box.min.x,
				(corner & 2) ? box.max.y : box.min.y,
				(corner & 4) ? box.max.z : box.min.z);

			if (corner == 0)
			{
				lo = world;
				hi = world;
				continue;
			}
			// Written out rather than with std::min/std::max: Windows.h is in
			// this translation unit without NOMINMAX, so those names are macros.
			if (world.x < lo.x) lo.x = world.x;
			if (world.y < lo.y) lo.y = world.y;
			if (world.z < lo.z) lo.z = world.z;
			if (world.x > hi.x) hi.x = world.x;
			if (world.y > hi.y) hi.y = world.y;
			if (world.z > hi.z) hi.z = world.z;
		}

		target["bounds"] = json{ { "min", Point(lo) }, { "max", Point(hi) } };
	}

	Response GetDimensions(const std::vector<ModelRef>& models)
	{
		json failures = json::object();
		std::unordered_set<Hash> requested;
		// Models the query pulled in are released again afterwards; ones that
		// were already resident are left alone, since something else needs them.
		std::unordered_set<Hash> borrowed;

		for (const auto& ref : models)
		{
			const Hash hash = static_cast<Hash>(ref.hash);
			if (!requested.insert(hash).second)
				continue;

			const Model model(hash);
			if (!model.IsInCdImage())
			{
				failures[ref.label] = "model is not in the game files; install the addon or check the name";
				continue;
			}
			if (!model.IsLoaded())
			{
				borrowed.insert(hash);
				model.Load();
			}
		}

		const DWORD deadline = GetTickCount() + kLoadTimeoutMs;
		for (;;)
		{
			const bool allReady = std::all_of(borrowed.begin(), borrowed.end(),
				[](Hash hash) { return Model(hash).IsLoaded(); });
			if (allReady || GetTickCount() > deadline)
				break;
			WAIT(0);
		}

		json described = json::object();
		for (const auto& ref : models)
		{
			if (failures.contains(ref.label) || described.contains(ref.label))
				continue;

			const Hash hash = static_cast<Hash>(ref.hash);
			if (!Model(hash).IsLoaded())
			{
				failures[ref.label] = "model did not stream in within " +
					std::to_string(kLoadTimeoutMs) + "ms";
				continue;
			}

			const Box box = GetBox(hash);
			if (!box.valid)
			{
				failures[ref.label] = "the game reports an empty bounding box";
				continue;
			}

			described[ref.label] = json{
				{ "hash", IntToHexString(hash, true) },
				{ "min", Point(box.min) },
				{ "max", Point(box.max) },
				{ "size", Point(box.max - box.min) },
				{ "restZOffset", -box.min.z },
			};
		}

		for (Hash hash : borrowed)
			Model(hash).Unload();

		json payload{
			{ "requested", models.size() },
			{ "models", std::move(described) },
			{ "note", "model space: x across, y along the model's facing, z up. Spawn at "
					  "z = groundZ + restZOffset to seat the model on a surface. A model's box "
					  "never changes, so these are worth caching" },
		};

		if (failures.empty())
			return Response{ 200, Serialise(payload) };

		const bool nothingAnswered = payload["models"].empty();
		payload["failures"] = std::move(failures);
		return Response{ nothingAnswered ? 422 : 207, Serialise(payload) };
	}
}
