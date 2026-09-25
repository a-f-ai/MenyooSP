/*
* Menyoo PC - Grand Theft Auto V single-player trainer mod
*/
#include "PlayerCommand.h"

#include "Json.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <string>
#include <vector>

namespace Http::PlayerCommand
{
	namespace
	{
		std::string LowerAscii(std::string value)
		{
			std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
				return character < 0x80 ? static_cast<char>(std::tolower(character)) : static_cast<char>(character);
			});
			return value;
		}

		bool EqualAlias(const std::string& left, const std::string& right)
		{
			return LowerAscii(left) == LowerAscii(right);
		}

		unsigned long ParseConfiguredHash(const json& value, const std::string& subject)
		{
			if (!value.is_string())
				throw ApiError(500, subject + " has a non-string hash");

			const std::string raw = value.get<std::string>();
			if (raw.size() < 3 || raw[0] != '0' || (raw[1] != 'x' && raw[1] != 'X'))
				throw ApiError(500, subject + " has an invalid hash \"" + raw + "\"");

			size_t parsed = 0;
			unsigned long hash = 0;
			try
			{
				hash = std::stoul(raw, &parsed, 16);
			}
			catch (const std::exception&)
			{
				throw ApiError(500, subject + " has an invalid hash \"" + raw + "\"");
			}
			if (parsed != raw.size() || hash > std::numeric_limits<unsigned int>::max())
				throw ApiError(500, subject + " has an invalid hash \"" + raw + "\"");
			return hash;
		}

		ResolvedAlias ResolveCharacterVariant(const json& character, const ModelInput& input)
		{
			if (!character.contains("key") || !character["key"].is_string())
				throw ApiError(500, "Characters.json contains a character without a string key");
			if (!character.contains("variants") || !character["variants"].is_array() || character["variants"].empty())
				throw ApiError(500, "character \"" + character["key"].get<std::string>() + "\" has no variants");

			const std::string key = character["key"].get<std::string>();
			std::string requestedVariant = input.variant;
			bool selectedDefault = false;
			if (requestedVariant.empty())
			{
				if (character["variants"].size() == 1)
				{
					const json& only = character["variants"][0];
					if (!only.contains("model") || !only["model"].is_string())
						throw ApiError(500, "character \"" + key + "\" has a variant without a model");
					requestedVariant = only["model"].get<std::string>();
				}
				else
				{
					if (!character.contains("defaultVariant") || !character["defaultVariant"].is_string() ||
						character["defaultVariant"].get<std::string>().empty())
					{
						throw ApiError(409, "character alias \"" + input.alias +
							"\" has multiple variants and no defaultVariant; pass field \"variant\"");
					}
					requestedVariant = character["defaultVariant"].get<std::string>();
					selectedDefault = true;
				}
			}

			std::vector<const json*> matches;
			for (const json& variant : character["variants"])
			{
				if (!variant.is_object() || !variant.contains("model") || !variant["model"].is_string())
					throw ApiError(500, "character \"" + key + "\" has a malformed variant");
				const std::string model = variant["model"].get<std::string>();
				const std::string color = variant.contains("color") && variant["color"].is_string()
					? variant["color"].get<std::string>() : "";
				if (EqualAlias(model, requestedVariant) || (!color.empty() && EqualAlias(color, requestedVariant)))
					matches.push_back(&variant);
			}

			if (matches.empty() && selectedDefault)
				throw ApiError(500, "character \"" + key + "\" has defaultVariant \"" + requestedVariant +
					"\" but no variant with that model or color");
			if (matches.empty())
				throw ApiError(404, "variant \"" + requestedVariant + "\" was not found for character alias \"" + input.alias + "\"");
			if (matches.size() > 1)
				throw ApiError(409, "variant \"" + requestedVariant + "\" is ambiguous for character alias \"" + input.alias + "\"");

			const json& variant = *matches.front();
			const std::string model = variant["model"].get<std::string>();
			if (model.empty())
				throw ApiError(500, "character \"" + key + "\" has an empty variant model");
			if (!variant.contains("hash"))
				throw ApiError(500, "character variant \"" + model + "\" has no hash");
			return ResolvedAlias{
				ParseConfiguredHash(variant["hash"], "character variant \"" + model + "\""),
				model,
				key,
				requestedVariant,
			};
		}
	}

	ModelInput ParseModelInput(const json& body)
	{
		const bool hasModel = body.contains("model");
		const bool hasAlias = body.contains("alias");
		if (hasModel == hasAlias)
			throw ApiError(400, "give exactly one of \"model\" or \"alias\"");

		ModelInput input;
		if (hasModel)
		{
			if (body.contains("variant"))
				throw ApiError(400, "field \"variant\" is only valid with \"alias\", not an exact \"model\"");

			const json& model = body.at("model");
			if (model.is_number_unsigned())
			{
				input.hasNumericModel = true;
				input.numericModel = model.get<unsigned long>();
				return input;
			}
			if (!model.is_string() || model.get<std::string>().empty())
				throw ApiError(400, "field \"model\" must be a non-empty model name or unsigned hash");
			input.model = model.get<std::string>();
			return input;
		}

		const json& alias = body.at("alias");
		if (!alias.is_string() || alias.get<std::string>().empty())
			throw ApiError(400, "field \"alias\" must be a non-empty string");
		input.alias = alias.get<std::string>();
		input.variant = Json::OptionalString(body, "variant", "");
		if (body.contains("variant") && input.variant.empty())
			throw ApiError(400, "field \"variant\" must not be empty");
		return input;
	}

	VehiclePlacement ParseVehiclePlacement(const json& body)
	{
		VehiclePlacement placement;
		if (body.contains("position"))
		{
			const json& position = body.at("position");
			if (!position.is_object())
				throw ApiError(400, "field \"position\" must be an object with x, y and z");
			placement.hasPosition = true;
			placement.x = Json::Number(position, "x");
			placement.y = Json::Number(position, "y");
			placement.z = Json::Number(position, "z");
		}
		if (body.contains("heading"))
		{
			placement.hasHeading = true;
			placement.heading = Json::Number(body, "heading");
		}
		return placement;
	}

	ResolvedAlias ResolveCharacterAlias(const json& document, const ModelInput& input)
	{
		if (input.alias.empty())
			throw ApiError(500, "character alias resolution requires a non-empty alias");
		if (!document.is_object() || !document.contains("characters") || !document["characters"].is_array())
			throw ApiError(500, "Characters.json must contain a \"characters\" array");

		std::vector<const json*> matches;
		for (const json& character : document["characters"])
		{
			if (!character.is_object() || !character.contains("key") || !character["key"].is_string())
				throw ApiError(500, "Characters.json contains a malformed character");
			bool matched = EqualAlias(character["key"].get<std::string>(), input.alias);
			if (!matched && character.contains("label") && character["label"].is_string())
				matched = EqualAlias(character["label"].get<std::string>(), input.alias);
			if (!matched && character.contains("aliases"))
			{
				if (!character["aliases"].is_array())
					throw ApiError(500, "character \"" + character["key"].get<std::string>() + "\" has non-array aliases");
				for (const json& alias : character["aliases"])
				{
					if (!alias.is_string())
						throw ApiError(500, "character \"" + character["key"].get<std::string>() + "\" has a non-string alias");
					if (EqualAlias(alias.get<std::string>(), input.alias))
						matched = true;
				}
			}
			if (matched)
				matches.push_back(&character);
		}

		if (matches.empty())
			throw ApiError(404, "character alias \"" + input.alias + "\" was not found");
		if (matches.size() > 1)
			throw ApiError(409, "character alias \"" + input.alias + "\" is ambiguous");
		return ResolveCharacterVariant(*matches.front(), input);
	}

	ResolvedAlias ResolveVehicleAlias(const json& document, const ModelInput& input)
	{
		if (input.alias.empty())
			throw ApiError(500, "vehicle alias resolution requires a non-empty alias");
		if (!input.variant.empty())
			throw ApiError(400, "field \"variant\" is not valid for vehicle aliases");
		if (!document.is_object() || !document.contains("vehicles") || !document["vehicles"].is_array())
			throw ApiError(500, "VehicleShortlist.json must contain a \"vehicles\" array");

		std::vector<const json*> matches;
		for (const json& vehicle : document["vehicles"])
		{
			if (!vehicle.is_object() || !vehicle.contains("label") || !vehicle["label"].is_string())
				throw ApiError(500, "VehicleShortlist.json contains a vehicle without a string label");
			if (EqualAlias(vehicle["label"].get<std::string>(), input.alias))
				matches.push_back(&vehicle);
		}
		if (matches.empty())
			throw ApiError(404, "vehicle alias \"" + input.alias + "\" was not found");
		if (matches.size() > 1)
			throw ApiError(409, "vehicle alias \"" + input.alias + "\" is ambiguous");

		const json& vehicle = *matches.front();
		const std::string label = vehicle["label"].get<std::string>();
		if (!vehicle.contains("hash"))
			throw ApiError(500, "vehicle alias \"" + label + "\" has no hash");
		return ResolvedAlias{
			ParseConfiguredHash(vehicle["hash"], "vehicle alias \"" + label + "\""),
			label,
			label,
			"",
		};
	}
}
