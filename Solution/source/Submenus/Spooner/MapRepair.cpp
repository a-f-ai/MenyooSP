/*
* Menyoo PC - Grand Theft Auto V single-player trainer mod
*/
#include "MapRepair.h"

#include "../../macros.h"
#include "../../Natives/natives2.h"
#include "../../Util/ExePath.h"
#include "../../Util/FileLogger.h"
#include "../../Util/NameEncoding.h"

#include <pugixml/src/pugixml.hpp>

#include <system_error>

namespace sub::Spooner::MapRepair
{
	namespace
	{
		// A file name for display and logging. u8string converts the native
		// wide path straight to UTF-8 and never consults the ANSI code page, so
		// unlike string() it cannot throw on a name the page has no room for.
		std::string DisplayName(const std::filesystem::path& path)
		{
			const std::u8string utf8 = path.filename().u8string();
			return std::string(utf8.begin(), utf8.end());
		}

		std::filesystem::path SpoonerDirectory()
		{
			// The narrow form is Menyoo's own and is ASCII, so this conversion
			// is the one place where it is safe.
			return std::filesystem::path(GetPathffA(Pathff::Spooner, true));
		}
	}

	bool Unwind(const std::string& text, std::string& out, int& rounds)
	{
		return ige::NameEncoding::Unwind(text, out, rounds);
	}

	FileResult RepairOne(const std::filesystem::path& path, bool apply)
	{
		FileResult result;
		result.name = DisplayName(path);

		std::error_code sizeError;
		const auto before = std::filesystem::file_size(path, sizeError);
		if (sizeError)
		{
			result.failure = "cannot stat: " + sizeError.message();
			return result;
		}
		result.bytesBefore = static_cast<long long>(before);

		// The wide overload: handing pugixml a narrow path would convert
		// through the code page, which is what killed the first version.
		pugi::xml_document doc;
		const pugi::xml_parse_result parsed = doc.load_file(path.c_str());
		if (parsed.status != pugi::status_ok)
		{
			result.failure = std::string("cannot parse: ") + parsed.description();
			return result;
		}

		auto root = doc.child("SpoonerPlacements");
		if (!root)
		{
			result.failure = "not a spooner map: no <SpoonerPlacements>";
			return result;
		}

		for (auto placement = root.child("Placement"); placement;
			placement = placement.next_sibling("Placement"))
		{
			auto node = placement.child("HashName");
			if (!node)
				continue;

			const std::string original = node.text().as_string();
			std::string recovered;
			int rounds = 0;
			if (!Unwind(original, recovered, rounds))
				continue;

			++result.namesAffected;
			if (result.sample.empty())
				result.sample = recovered;
			if (apply)
				node.text() = recovered.c_str();
		}

		if (!apply || result.namesAffected == 0)
			return result;

		std::filesystem::path backup = path;
		backup += ".bak";
		std::error_code copyError;
		std::filesystem::copy_file(path, backup,
			std::filesystem::copy_options::overwrite_existing, copyError);
		if (copyError)
		{
			// Reported, not swallowed: without a backup nothing is written, and
			// the caller is told which file was left alone and why.
			result.failure = "backup failed, left untouched: " + copyError.message();
			result.namesAffected = 0;
			return result;
		}

		if (!doc.save_file(path.c_str()))
		{
			result.failure = "could not write the repaired file";
			result.namesAffected = 0;
			return result;
		}

		std::error_code afterError;
		const auto after = std::filesystem::file_size(path, afterError);
		result.bytesAfter = afterError ? result.bytesBefore : static_cast<long long>(after);
		return result;
	}

	Report ScanSavedMaps(bool apply)
	{
		Report report;
		report.applied = apply;

		const std::filesystem::path directory = SpoonerDirectory();
		std::error_code walkError;
		std::filesystem::directory_iterator entry(directory, walkError);
		if (walkError)
		{
			FileResult failure;
			failure.name = DisplayName(directory);
			failure.failure = "cannot open the Spooner folder: " + walkError.message();
			report.failed.push_back(std::move(failure));
			return report;
		}

		const std::filesystem::directory_iterator end;
		for (; entry != end; entry.increment(walkError))
		{
			if (walkError)
			{
				FileResult failure;
				failure.name = "<directory>";
				failure.failure = "walk stopped: " + walkError.message();
				report.failed.push_back(std::move(failure));
				break;
			}

			const std::filesystem::path path = entry->path();
			if (path.extension() != ".xml")
				continue;

			std::error_code kindError;
			if (!std::filesystem::is_regular_file(path, kindError) || kindError)
				continue;

			++report.filesSeen;

			FileResult file = RepairOne(path, apply);
			if (!file.failure.empty())
			{
				addlog(ige::LogType::LOG_WARNING,
					"MapRepair: " + file.name + " - " + file.failure);
				report.failed.push_back(std::move(file));
			}
			else if (file.namesAffected > 0)
			{
				report.namesAffected += file.namesAffected;
				if (apply)
				{
					report.bytesSaved += file.bytesBefore - file.bytesAfter;
					addlog(ige::LogType::LOG_INFO, "MapRepair: " + file.name + " - " +
						std::to_string(file.namesAffected) + " name(s), " +
						std::to_string(file.bytesBefore - file.bytesAfter) + " bytes recovered");
				}
				report.affected.push_back(std::move(file));
			}

			// One file per frame. Hundreds of maps otherwise stall the game for
			// as long as the whole folder takes to parse.
			WAIT(0);
		}

		return report;
	}

	std::string Report::Summary() const
	{
		std::string text;

		if (namesAffected == 0)
			text = "Checked " + std::to_string(filesSeen) + " map(s): all names are clean";
		else
		{
			text = (applied ? "Repaired " : "Found ") + std::to_string(namesAffected) +
				" inflated name(s) in " + std::to_string(affected.size()) +
				" of " + std::to_string(filesSeen) + " map(s)";
			if (applied && bytesSaved > 0)
				text += ", " + std::to_string(bytesSaved / 1024) + " KB recovered";
			if (!affected.empty() && !affected.front().sample.empty())
				text += ", e.g. \"" + affected.front().sample + "\"";
		}

		if (!failed.empty())
		{
			text += ". ~r~" + std::to_string(failed.size()) + " file(s) failed: " +
				failed.front().name + " - " + failed.front().failure;
			if (failed.size() > 1)
				text += " (see menyoolog.txt for the rest)";
		}
		return text;
	}
}
