/*
* Menyoo PC - Grand Theft Auto V single-player trainer mod
*/
#include "MapRepair.h"

#include "../../Util/ExePath.h"
#include "../../Util/FileLogger.h"

#include <pugixml/src/pugixml.hpp>

#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>

namespace sub::Spooner::MapRepair
{
	namespace
	{
		constexpr int kMaxRounds = 64;

		// One layer off. Every byte must decode from a two-byte UTF-8 sequence
		// whose code point fits in a byte, because that is exactly what the
		// latin1 round produced; anything else means this is not inflated text.
		bool UnwindOnce(const std::string& in, std::string& out)
		{
			out.clear();
			out.reserve(in.size() / 2 + 1);

			for (size_t i = 0; i < in.size();)
			{
				const unsigned char lead = static_cast<unsigned char>(in[i]);
				if (lead < 0x80)
				{
					out += static_cast<char>(lead);
					++i;
					continue;
				}
				if ((lead & 0xE0) != 0xC0 || i + 1 >= in.size())
					return false;

				const unsigned char trail = static_cast<unsigned char>(in[i + 1]);
				if ((trail & 0xC0) != 0x80)
					return false;

				const unsigned code = ((lead & 0x1Fu) << 6) | (trail & 0x3Fu);
				if (code > 0xFF)
					return false;   // a genuine character, not a doubled byte

				out += static_cast<char>(code);
				i += 2;
			}
			return out != in;
		}

		// Whether a byte string is well-formed UTF-8. This is what separates
		// damage from correct text in the Latin-1 range: "Café" and one round
		// of damage to the byte 0xE9 look identical, but unwinding the real
		// "Café" lands on a lone 0xE9, which is not valid UTF-8, while
		// unwinding real damage lands on the text it started as.
		bool IsValidUtf8(const std::string& text)
		{
			for (size_t i = 0; i < text.size();)
			{
				const unsigned char lead = static_cast<unsigned char>(text[i]);
				int extra;
				if (lead < 0x80)               { ++i; continue; }
				else if ((lead & 0xE0) == 0xC0) extra = 1;
				else if ((lead & 0xF0) == 0xE0) extra = 2;
				else if ((lead & 0xF8) == 0xF0) extra = 3;
				else return false;

				if (i + extra >= text.size())
					return false;
				for (int k = 1; k <= extra; ++k)
				{
					if ((static_cast<unsigned char>(text[i + k]) & 0xC0) != 0x80)
						return false;
				}
				i += extra + 1;
			}
			return true;
		}

		std::string Directory()
		{
			return GetPathffA(Pathff::Spooner, true);
		}
	}

	bool Unwind(const std::string& text, std::string& out, int& rounds)
	{
		// Peel layers off, remembering the last one that is still well-formed
		// UTF-8. Damage always lands back on the text it started as, which is;
		// correct Latin-1-range text peels into a lone high byte, which is not,
		// and so is never accepted.
		std::string current = text;
		std::string next;
		std::string best;
		int bestRounds = 0;

		for (int round = 1; round <= kMaxRounds; ++round)
		{
			if (!UnwindOnce(current, next))
				break;
			current.swap(next);
			if (IsValidUtf8(current))
			{
				best = current;
				bestRounds = round;
			}
		}

		if (bestRounds == 0)
			return false;
		out = best;
		rounds = bestRounds;
		return true;
	}

	FileResult RepairOne(const std::string& name, bool apply)
	{
		FileResult result;
		result.name = name;

		const std::string path = Directory() + name + ".xml";
		std::error_code sizeError;
		result.bytesBefore = static_cast<long long>(std::filesystem::file_size(path, sizeError));

		pugi::xml_document doc;
		if (doc.load_file(path.c_str()).status != pugi::status_ok)
			return result;

		auto root = doc.child("SpoonerPlacements");
		if (!root)
			return result;

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

		// Keep the original next to the repaired file rather than in place:
		// this rewrites names the owner may want to check.
		std::error_code copyError;
		std::filesystem::copy_file(path, path + ".bak",
			std::filesystem::copy_options::overwrite_existing, copyError);
		if (copyError)
		{
			addlog(ige::LogType::LOG_ERROR,
				"MapRepair: could not back up " + path + ": " + copyError.message());
			result.namesAffected = 0;
			return result;
		}

		if (!doc.save_file(path.c_str()))
		{
			addlog(ige::LogType::LOG_ERROR, "MapRepair: could not write " + path);
			result.namesAffected = 0;
			return result;
		}

		result.bytesAfter = static_cast<long long>(std::filesystem::file_size(path, sizeError));
		addlog(ige::LogType::LOG_INFO, "MapRepair: " + name + " - " +
			std::to_string(result.namesAffected) + " name(s), " +
			std::to_string(result.bytesBefore - result.bytesAfter) + " bytes recovered");
		return result;
	}

	Report ScanSavedMaps(bool apply)
	{
		Report report;
		report.applied = apply;

		std::error_code walkError;
		for (const auto& entry : std::filesystem::directory_iterator(Directory(), walkError))
		{
			if (walkError)
				break;
			if (!entry.is_regular_file() || entry.path().extension() != ".xml")
				continue;

			++report.filesScanned;
			FileResult file = RepairOne(entry.path().stem().string(), apply);
			if (file.namesAffected == 0)
				continue;

			report.namesAffected += file.namesAffected;
			if (apply)
				report.bytesSaved += file.bytesBefore - file.bytesAfter;
			report.affected.push_back(std::move(file));
		}
		return report;
	}

	std::string Report::Summary() const
	{
		if (namesAffected == 0)
			return "Checked " + std::to_string(filesScanned) + " map(s): all names are clean.";

		std::string text = (applied ? "Repaired " : "Found ") +
			std::to_string(namesAffected) + " inflated name(s) in " +
			std::to_string(affected.size()) + " of " + std::to_string(filesScanned) + " map(s)";
		if (applied && bytesSaved > 0)
			text += ", " + std::to_string(bytesSaved / 1024) + " KB recovered";
		if (!affected.empty() && !affected.front().sample.empty())
			text += ". First: \"" + affected.front().sample + "\"";
		return text;
	}
}
