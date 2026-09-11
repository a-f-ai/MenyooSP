/*
* Menyoo PC - Grand Theft Auto V single-player trainer mod
*
* Repairing entity names that earlier builds inflated.
*
* Those builds declared ISO-8859-1 in the XML and then wrote UTF-8. pugixml
* reads that attribute on load, so it re-decoded every byte as latin1 and
* re-encoded it, doubling the length of any non-ASCII name on every single
* load. Eleven cycles turned "Детская машинка" into 57345 characters.
*
* The writers now declare UTF-8, so nothing grows any more, but files written
* before that cannot repair themselves.
*/
#pragma once

#include <string>
#include <vector>

namespace sub::Spooner::MapRepair
{
	struct FileResult
	{
		std::string name;
		int namesAffected = 0;
		long long bytesBefore = 0;
		long long bytesAfter = 0;   // only set when the repair was applied
		std::string sample;         // what one of the names unwinds to
	};

	struct Report
	{
		int filesScanned = 0;
		std::vector<FileResult> affected;
		int namesAffected = 0;
		long long bytesSaved = 0;
		bool applied = false;

		std::string Summary() const;
	};

	// Undoes the doubling. Returns false when `text` is not inflated, which
	// includes every correctly encoded name: a real Cyrillic letter decodes to
	// a code point above 0xFF and so cannot have come from a latin1 round,
	// which is what makes this safe to run on anything without a threshold.
	bool Unwind(const std::string& text, std::string& out, int& rounds);

	// Walks menyooStuff/Spooner. With apply=false nothing is written.
	Report ScanSavedMaps(bool apply);

	// One file, by name without the extension.
	FileResult RepairOne(const std::string& name, bool apply);
}
