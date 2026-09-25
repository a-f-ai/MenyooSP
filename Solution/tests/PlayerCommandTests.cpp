/*
* Behaviour tests for the HTTP request boundary used by voice commands.
*/
#include "PlayerCommand.h"

#include <cstdio>
#include <functional>
#include <string>

using Http::ApiError;
using Http::PlayerCommand::json;

namespace
{
	int g_failures = 0;

	void Check(bool condition, const std::string& what)
	{
		if (condition)
		{
			std::printf("  ok   %s\n", what.c_str());
			return;
		}
		std::printf("  FAIL %s\n", what.c_str());
		++g_failures;
	}

	void CheckApiError(const std::function<void()>& action, int status, const std::string& fragment,
		const std::string& what)
	{
		try
		{
			action();
			Check(false, what + " (no error)");
		}
		catch (const ApiError& error)
		{
			Check(error.Status() == status && std::string(error.what()).find(fragment) != std::string::npos, what);
		}
	}

	json CharacterDocument()
	{
		return json::parse(R"({
			"characters": [
				{
					"key": "spiderman",
					"label": "Spider-Man",
					"aliases": ["spidey", "человек-паук"],
					"defaultVariant": "o7",
					"variants": [
						{"model":"SpidermanGreen", "hash":"0x893a0bdf", "color":"green"},
						{"model":"o7", "hash":"0xe29147cc", "color":"red"}
					]
				},
				{
					"key": "hulk",
					"label": "Hulk",
					"aliases": ["strong"],
					"variants": [
						{"model":"HulkGreen", "hash":"0x00000011", "color":"green"},
						{"model":"HulkRed", "hash":"0x00000012", "color":"red"}
					]
				},
				{
					"key": "duplicate",
					"label": "Duplicate",
					"aliases": ["strong"],
					"variants": [{"model":"Duplicate", "hash":"0x00000013", "color":""}]
				}
			]
		})");
	}

	void TheRequestRequiresExactlyOneModelSelector()
	{
		std::printf("the request requires exactly one model selector\n");
		CheckApiError([] { Http::PlayerCommand::ParseModelInput(json::object()); }, 400,
			"exactly one", "missing model and alias are rejected");
		CheckApiError([] { Http::PlayerCommand::ParseModelInput(json{ { "model", "o7" }, { "alias", "spiderman" } }); },
			400, "exactly one", "model and alias together are rejected");
		CheckApiError([] { Http::PlayerCommand::ParseModelInput(json{ { "model", "o7" }, { "variant", "red" } }); },
			400, "variant", "variant cannot silently alter an exact model");

		const auto exact = Http::PlayerCommand::ParseModelInput(json{ { "model", "o7" } });
		Check(exact.model == "o7" && exact.alias.empty(), "an exact model name survives parsing");
	}

	void ACharacterAliasUsesOnlyItsExplicitDefault()
	{
		std::printf("a character alias uses only its explicit default\n");
		const auto resolved = Http::PlayerCommand::ResolveCharacterAlias(
			CharacterDocument(), Http::PlayerCommand::ModelInput{ "", "spiderman", "" });
		Check(resolved.model == "o7", "the configured default variant is selected");
		Check(resolved.hash == 0xe29147ccUL, "the configured model hash is returned");
		Check(resolved.alias == "spiderman", "the canonical alias is returned");

		const auto coloured = Http::PlayerCommand::ResolveCharacterAlias(
			CharacterDocument(), Http::PlayerCommand::ModelInput{ "", "spidey", "green" });
		Check(coloured.model == "SpidermanGreen", "an explicit colour selects that exact variant");
	}

	void AmbiguousOrIncompleteAliasesFailClearly()
	{
		std::printf("ambiguous or incomplete aliases fail clearly\n");
		CheckApiError([] {
			Http::PlayerCommand::ResolveCharacterAlias(
				CharacterDocument(), Http::PlayerCommand::ModelInput{ "", "hulk", "" });
		}, 409, "variant", "a multi-variant character without a default is rejected");

		CheckApiError([] {
			Http::PlayerCommand::ResolveCharacterAlias(
				CharacterDocument(), Http::PlayerCommand::ModelInput{ "", "strong", "" });
		}, 409, "ambiguous", "an alias shared by characters is rejected");

		CheckApiError([] {
			Http::PlayerCommand::ResolveCharacterAlias(
				CharacterDocument(), Http::PlayerCommand::ModelInput{ "", "spider", "" });
		}, 404, "not found", "substring matches are not accepted as aliases");
	}

	void BrokenAliasConfigurationIsAServiceError()
	{
		std::printf("broken alias configuration is a service error\n");
		CheckApiError([] {
			Http::PlayerCommand::ResolveCharacterAlias(json::parse(R"({"characters":[{
				"key":"spiderman", "label":"Spider-Man", "aliases":[], "defaultVariant":"missing",
				"variants":[
					{"model":"o6", "hash":"0xf05be361", "color":"green"},
					{"model":"o7", "hash":"0xe29147cc", "color":"red"}
				]
			}]})"), Http::PlayerCommand::ModelInput{ "", "spiderman", "" });
		}, 500, "defaultVariant", "a defaultVariant that names no model is a server configuration error");

		CheckApiError([] {
			Http::PlayerCommand::ResolveVehicleAlias(json::parse(R"({"vehicles":[
				{"label":"Bati 801"}
			]})"), Http::PlayerCommand::ModelInput{ "", "Bati 801", "" });
		}, 500, "hash", "a shortlist alias without a hash is a server configuration error");

		CheckApiError([] {
			Http::PlayerCommand::ResolveCharacterAlias(json::parse(R"({"characters":[{
				"key":"single", "label":"Single", "aliases":[],
				"variants":[{"model":"single_model", "color":""}]
			}]})"), Http::PlayerCommand::ModelInput{ "", "single", "" });
		}, 500, "hash", "a character variant without a hash is a server configuration error");
	}

	void VehicleAliasesAreExactAndReturnTheirConfiguredHash()
	{
		std::printf("vehicle aliases are exact and return their configured hash\n");
		const json document = json::parse(R"({"vehicles":[
			{"label":"Bati 801", "hash":"0xf9300cc5", "placements":845},
			{"label":"Bati 801RR", "hash":"0xcadd5d2d", "placements":69}
		]})");
		const auto resolved = Http::PlayerCommand::ResolveVehicleAlias(
			document, Http::PlayerCommand::ModelInput{ "", "Bati 801", "" });
		Check(resolved.hash == 0xf9300cc5UL, "Bati 801 resolves to its installed shortlist hash");
		CheckApiError([&] {
			Http::PlayerCommand::ResolveVehicleAlias(
				document, Http::PlayerCommand::ModelInput{ "", "Bati", "" });
		}, 404, "not found", "a partial vehicle label is rejected");
	}

	void ExplicitVehiclePositionsMustBeComplete()
	{
		std::printf("explicit vehicle positions must be complete\n");
		CheckApiError([] {
			Http::PlayerCommand::ParseVehiclePlacement(json{
				{ "model", "bati" }, { "position", { { "x", 1.0 }, { "y", 2.0 } } }
			});
		}, 400, "z", "a partial position is rejected rather than repaired");

		const auto current = Http::PlayerCommand::ParseVehiclePlacement(json{ { "model", "bati" } });
		Check(!current.hasPosition && !current.hasHeading, "omitted placement explicitly means the player position and heading");

		const auto explicitPlacement = Http::PlayerCommand::ParseVehiclePlacement(json{
			{ "model", "bati" },
			{ "position", { { "x", 1.0 }, { "y", 2.0 }, { "z", 3.0 } } },
			{ "heading", 90.0 },
		});
		Check(explicitPlacement.hasPosition && explicitPlacement.x == 1.0f && explicitPlacement.z == 3.0f,
			"a complete position survives parsing");
		Check(explicitPlacement.hasHeading && explicitPlacement.heading == 90.0f,
			"an explicit heading survives parsing");
	}
}

int main()
{
	TheRequestRequiresExactlyOneModelSelector();
	ACharacterAliasUsesOnlyItsExplicitDefault();
	AmbiguousOrIncompleteAliasesFailClearly();
	BrokenAliasConfigurationIsAServiceError();
	VehicleAliasesAreExactAndReturnTheirConfiguredHash();
	ExplicitVehiclePositionsMustBeComplete();

	if (g_failures == 0)
	{
		std::printf("\nall checks passed\n");
		return 0;
	}
	std::printf("\n%d check(s) failed\n", g_failures);
	return 1;
}
