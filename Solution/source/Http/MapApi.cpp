/*
* Menyoo PC - Grand Theft Auto V single-player trainer mod
*/
#include "MapApi.h"

#include "../macros.h"
#include "../Natives/natives2.h"
#include "../Util/ExePath.h"
#include "../Submenus/Spooner/Databases.h"
#include "../Submenus/Spooner/EntityManagement.h"
#include "../Submenus/Spooner/FileManagement.h"
#include "../Submenus/Spooner/MapRepair.h"
#include "../Submenus/Spooner/SpoonerEntity.h"

#include <json/single_include/nlohmann/json.hpp>

#include <cctype>
#include <filesystem>
#include <string>
#include <system_error>

using json = nlohmann::json;

namespace Http::MapApi
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

		// A map name becomes a file name, so it must not be able to escape the
		// Spooner directory or name anything but a map.
		bool NameIsSafe(const std::string& name)
		{
			if (name.empty() || name.size() > 120)
				return false;
			if (name.find("..") != std::string::npos)
				return false;
			for (char c : name)
			{
				const bool allowed = std::isalnum(static_cast<unsigned char>(c)) ||
					c == '_' || c == '-' || c == ' ' || c == '.';
				if (!allowed)
					return false;
			}
			return true;
		}

		std::string MapPath(const std::string& name)
		{
			return GetPathffA(Pathff::Spooner, true) + name + ".xml";
		}
	}

	Response ListMaps()
	{
		const std::string directory = GetPathffA(Pathff::Spooner, true);
		json maps = json::array();

		std::error_code failure;
		for (const auto& entry : std::filesystem::directory_iterator(directory, failure))
		{
			if (failure)
				break;
			if (!entry.is_regular_file())
				continue;
			const auto path = entry.path();
			if (path.extension() != ".xml")
				continue;
			maps.push_back(json{
				{ "name", path.stem().string() },
				{ "bytes", static_cast<long long>(entry.file_size()) },
			});
		}

		if (failure)
			return Fail(500, "cannot read " + directory + ": " + failure.message());

		return Ok(json{ { "directory", directory }, { "count", maps.size() }, { "maps", std::move(maps) } });
	}

	Response SaveMap(const std::string& name)
	{
		if (!NameIsSafe(name))
			return Fail(400, "map name may contain only letters, digits, spaces, dot, dash and underscore");

		// false: never prompt in-game for a waypoint reference, which would
		// block the fiber until somebody picks one with a controller.
		if (!sub::Spooner::FileManagement::SaveDbToFile(MapPath(name), false))
			return Fail(500, "the map could not be written to " + MapPath(name));

		return Ok(json{
			{ "saved", name },
			{ "path", MapPath(name) },
			{ "entities", sub::Spooner::Databases::EntityDb.size() },
		});
	}

	Response LoadMap(const std::string& name)
	{
		if (!NameIsSafe(name))
			return Fail(400, "map name may contain only letters, digits, spaces, dot, dash and underscore");

		const std::string path = MapPath(name);
		if (!std::filesystem::exists(path))
			return Fail(404, "no map named \"" + name + "\" in " + GetPathffA(Pathff::Spooner, true));

		if (!sub::Spooner::FileManagement::LoadPlacementsFromFile(path))
			return Fail(422, "the map failed to load; the XML may be malformed");

		return Ok(json{
			{ "loaded", name },
			{ "entities", sub::Spooner::Databases::EntityDb.size() },
		});
	}

	Response RepairNames(bool apply)
	{
		const auto report = sub::Spooner::MapRepair::ScanSavedMaps(apply);

		json files = json::array();
		for (const auto& file : report.affected)
		{
			json entry{
				{ "map", file.name },
				{ "names", file.namesAffected },
				{ "recoversTo", file.sample },
				{ "bytesBefore", file.bytesBefore },
			};
			if (apply)
			{
				entry["bytesAfter"] = file.bytesAfter;
				entry["bytesSaved"] = file.bytesBefore - file.bytesAfter;
			}
			files.push_back(std::move(entry));
		}

		// Failures are part of the answer, not a footnote: a map that could not
		// be read or written is one this did not fix, and the caller has to
		// know which.
		json failures = json::array();
		for (const auto& file : report.failed)
			failures.push_back(json{ { "map", file.name }, { "error", file.failure } });

		const int status = report.failed.empty() ? 200 : 207;
		return Response{ status, Serialise(json{
			{ "applied", apply },
			{ "filesSeen", report.filesSeen },
			{ "namesAffected", report.namesAffected },
			{ "bytesSaved", report.bytesSaved },
			{ "files", std::move(files) },
			{ "failures", std::move(failures) },
			{ "summary", report.Summary() },
		}) };
	}

	Response ClearSpawned()
	{
		const size_t before = sub::Spooner::Databases::EntityDb.size();
		sub::Spooner::EntityManagement::DeleteAllEntitiesInDb();
		return Ok(json{ { "removed", before }, { "remaining", sub::Spooner::Databases::EntityDb.size() } });
	}
}
