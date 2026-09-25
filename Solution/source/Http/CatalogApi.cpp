/*
* Menyoo PC - Grand Theft Auto V single-player trainer mod
*/
#include "CatalogApi.h"

#include "../macros.h"
#include "../Natives/natives2.h"
#include "../Scripting/Model.h"
#include "../Scripting/ModelNames.h"
#include "../Util/StringManip.h"
#include "../Submenus/PedAnimation.h"

#include <json/single_include/nlohmann/json.hpp>

#include <cctype>
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

using json = nlohmann::json;
using GTAmodel::Model;

namespace Http::CatalogApi
{
	namespace
	{
		constexpr int kMaxLimit = 500;
		constexpr int kDefaultLimit = 50;

		std::string Serialise(const json& payload)
		{
			return payload.dump(2, ' ', false, json::error_handler_t::replace);
		}

		Response Ok(const json& payload) { return Response{ 200, Serialise(payload) }; }

		std::string Lower(std::string value)
		{
			std::transform(value.begin(), value.end(), value.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return value;
		}

		bool Contains(const std::string& haystack, const std::string& needleLower)
		{
			return needleLower.empty() || Lower(haystack).find(needleLower) != std::string::npos;
		}

		int EffectiveLimit(const Query& query)
		{
			if (query.limit <= 0) return kDefaultLimit;
			return query.limit > kMaxLimit ? kMaxLimit : query.limit;
		}

		// Collects hits while counting every match, so a caller can page and
		// still know how much is behind the window.
		struct Collector
		{
			const Query& query;
			const std::string needle;
			const int limit;
			int matched = 0;
			json items = json::array();

			explicit Collector(const Query& q)
				: query(q), needle(Lower(q.text)), limit(EffectiveLimit(q))
			{
			}

			bool Take(json&& item)
			{
				++matched;
				if (matched <= query.offset) return true;
				if (static_cast<int>(items.size()) >= limit) return true;
				items.push_back(std::move(item));
				return true;
			}

			json Finish(const char* kind) const
			{
				return json{
					{ "kind", kind },
					{ "query", query.text },
					{ "matched", matched },
					{ "returned", items.size() },
					{ "offset", query.offset },
					{ "limit", limit },
					{ "items", items },
				};
			}
		};

		json ModelEntry(const std::string& name, const std::string& label, bool verify)
		{
			const Model model(GET_HASH_KEY(name.c_str()));
			json entry{
				{ "name", name },
				{ "label", label },
				{ "hash", IntToHexString(model.hash, true) },
			};
			if (verify)
				entry["installed"] = model.IsInCdImage();
			return entry;
		}
	}

	Response Peds(const Query& query)
	{
		Collector collector(query);
		for (const auto& entry : g_pedModels)
		{
			if (!Contains(entry.first, collector.needle) && !Contains(entry.second, collector.needle))
				continue;
			collector.Take(ModelEntry(entry.first, entry.second, query.verifyInstalled));
		}
		return Ok(collector.Finish("peds"));
	}

	Response Vehicles(const Query& query)
	{
		const std::vector<std::pair<std::string, std::vector<Model>*>> classes{
			{ "openwheel", &g_vehHashes_OPENWHEEL }, { "super", &g_vehHashes_SUPER },
			{ "sport", &g_vehHashes_SPORT }, { "sportsclassic", &g_vehHashes_SPORTSCLASSIC },
			{ "coupe", &g_vehHashes_COUPE }, { "muscle", &g_vehHashes_MUSCLE },
			{ "offroad", &g_vehHashes_OFFROAD }, { "suv", &g_vehHashes_SUV },
			{ "sedan", &g_vehHashes_SEDAN }, { "compact", &g_vehHashes_COMPACT },
			{ "van", &g_vehHashes_VAN }, { "service", &g_vehHashes_SERVICE },
			{ "train", &g_vehHashes_TRAIN }, { "emergency", &g_vehHashes_EMERGENCY },
			{ "motorcycle", &g_vehHashes_MOTORCYCLE }, { "bicycle", &g_vehHashes_BICYCLE },
			{ "plane", &g_vehHashes_PLANE }, { "helicopter", &g_vehHashes_HELICOPTER },
			{ "boat", &g_vehHashes_BOAT }, { "industrial", &g_vehHashes_INDUSTRIAL },
			{ "commercial", &g_vehHashes_COMMERCIAL }, { "utility", &g_vehHashes_UTILITY },
			{ "military", &g_vehHashes_MILITARY }, { "other", &g_vehHashes_OTHER },
		};

		Collector collector(query);
		const std::string wantedClass = Lower(query.filter);

		for (const auto& group : classes)
		{
			if (!wantedClass.empty() && group.first != wantedClass)
				continue;
			for (const auto& model : *group.second)
			{
				const std::string label = get_vehicle_model_label(model, true);
				if (!Contains(label, collector.needle))
					continue;
				json entry{
					{ "label", label },
					{ "class", group.first },
					{ "hash", IntToHexString(model.hash, true) },
				};
				if (query.verifyInstalled)
					entry["installed"] = model.IsInCdImage();
				collector.Take(std::move(entry));
			}
		}

		json payload = collector.Finish("vehicles");
		json classNames = json::array();
		for (const auto& group : classes) classNames.push_back(group.first);
		payload["classes"] = std::move(classNames);
		return Ok(payload);
	}

	Response Props(const Query& query)
	{
		Collector collector(query);
		for (const auto& name : objectModels)
		{
			if (!Contains(name, collector.needle))
				continue;
			collector.Take(ModelEntry(name, name, query.verifyInstalled));
		}
		return Ok(collector.Finish("props"));
	}

	Response Scenarios(const Query& query)
	{
		Collector collector(query);
		for (const auto& scenario : sub::AnimationTaskScenarios::vNamedScenarios)
		{
			if (!Contains(scenario.name, collector.needle) && !Contains(scenario.label, collector.needle))
				continue;
			collector.Take(json{ { "name", scenario.name }, { "label", scenario.label } });
		}
		return Ok(collector.Finish("scenarios"));
	}

	Response Animations(const Query& query)
	{
		Collector collector(query);
		const std::string wantedDict = Lower(query.filter);

		for (const auto& dict : sub::AnimationMenu::allPedAnims)
		{
			if (!wantedDict.empty() && Lower(dict.first).find(wantedDict) == std::string::npos)
				continue;
			const bool dictMatches = Contains(dict.first, collector.needle);
			for (const auto& animation : dict.second)
			{
				if (!dictMatches && !Contains(animation, collector.needle))
					continue;
				collector.Take(json{ { "dict", dict.first }, { "name", animation } });
			}
		}
		return Ok(collector.Finish("animations"));
	}
}
