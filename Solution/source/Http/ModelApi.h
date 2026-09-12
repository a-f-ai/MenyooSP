/*
* Menyoo PC - Grand Theft Auto V single-player trainer mod
*
* Model geometry: how big a thing is, and where its origin sits inside its
* own box. Without this a caller can only guess a footprint from the model
* name, which is how props end up overlapping and peds end up in the ground.
*
* Runs on the fiber and may call natives. Nothing here throws: an exception
* on the fiber kills the script (see EntityApi.h).
*/
#pragma once

#include "CommandQueue.h"

#include "../Util/GTAmath.h"

#include <json/single_include/nlohmann/json.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace Http::ModelApi
{
	// Loading models is the expensive part of a dimensions query, so the
	// caller is told to split rather than handed a request that stalls the
	// fiber for seconds.
	constexpr std::size_t kMaxModelsPerQuery = 64;

	struct ModelRef
	{
		unsigned long hash;
		std::string label; // what the caller wrote, and the key in the reply
	};

	// Model-space bounds, signed. The sign is the whole point: min.z near zero
	// means the origin sits at the model's base, min.z near -height/2 means it
	// sits in the middle, and that is what decides the spawn height.
	//
	// Menyoo's own Model::Dimensions() cannot answer this - it takes abs() of
	// all six numbers - so this calls GET_MODEL_DIMENSIONS directly.
	struct Box
	{
		Vector3 min;
		Vector3 max;
		bool valid;
	};

	Box GetBox(unsigned long model);

	// Adds "size" and world-space "bounds" to an entity description, or
	// "geometryError" when the game reports no usable box for the model.
	void AddGeometry(nlohmann::json& target, int entityHandle, unsigned long model);

	Response GetDimensions(const std::vector<ModelRef>& models);
}
