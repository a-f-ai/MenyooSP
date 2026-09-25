/*
* Menyoo PC - Grand Theft Auto V single-player trainer mod
*
* Pure HTTP-boundary parsing for player voice commands. This unit contains no
* natives, so malformed requests and alias files are testable off-game.
*/
#pragma once

#include "ApiError.h"

#include <json/single_include/nlohmann/json.hpp>

#include <string>

namespace Http::PlayerCommand
{
	using json = nlohmann::json;

	struct ModelInput
	{
		std::string model;
		std::string alias;
		std::string variant;
		bool hasNumericModel = false;
		unsigned long numericModel = 0;
	};

	struct ResolvedAlias
	{
		unsigned long hash;
		std::string model;
		std::string alias;
		std::string variant;
	};

	struct VehiclePlacement
	{
		bool hasPosition = false;
		float x = 0.0f;
		float y = 0.0f;
		float z = 0.0f;
		bool hasHeading = false;
		float heading = 0.0f;
	};

	ModelInput ParseModelInput(const json& body);
	VehiclePlacement ParseVehiclePlacement(const json& body);
	ResolvedAlias ResolveCharacterAlias(const json& document, const ModelInput& input);
	ResolvedAlias ResolveVehicleAlias(const json& document, const ModelInput& input);
}
