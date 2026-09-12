/*
* Menyoo PC - Grand Theft Auto V single-player trainer mod
*
* Repairing entity names that earlier builds inflated.
*
* Those builds declared ISO-8859-1 in the XML and then wrote UTF-8. pugixml
* reads that attribute on load, so it re-decoded every byte as latin1 and
* re-encoded it, doubling the length of any non-ASCII name on every load.
* Eleven cycles turned "Детская машинка" into 57345 characters.
*
* Three rules govern this, the first two learned the hard way.
*
* Nothing may throw. This runs on the ScriptHookV fiber, where raising a C++
* exception kills the script - the first version converted each path to a
* narrow string, which throws on a name the ANSI code page cannot represent,
* and died on the 82nd map. Paths stay as std::filesystem::path and every call
* uses its error_code overload.
*
* Nothing may run long without yielding. A Spooner folder can hold hundreds of
* maps, and parsing them in one go freezes the game.
*
* Nothing may be skipped or swallowed quietly. Every file is parsed, not
* guessed at from its bytes, and every failure is reported with the name of the
* file it happened to.
*/
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace sub::Spooner::MapRepair
{
	struct FileResult
	{
		std::string name;            // display only: UTF-8, never via the code page
		int namesAffected = 0;
		long long bytesBefore = 0;
		long long bytesAfter = 0;
		std::string sample;          // what one of the names unwinds to
		std::string failure;         // empty when the file was handled cleanly
	};

	struct Report
	{
		int filesSeen = 0;
		std::vector<FileResult> affected;
		std::vector<FileResult> failed;
		int namesAffected = 0;
		long long bytesSaved = 0;
		bool applied = false;

		std::string Summary() const;
	};

	// Undoes the doubling. Returns false when `text` is not inflated, which
	// includes correct text: peeling lands on well-formed UTF-8 only for real
	// damage, so "Café" - byte-identical to one damaged 0xE9 - is left alone.
	bool Unwind(const std::string& text, std::string& out, int& rounds);

	// Walks menyooStuff/Spooner, yielding a frame per file. apply=false reads
	// and reports without writing anything.
	Report ScanSavedMaps(bool apply);

	// One map. A failure is returned in FileResult::failure, never hidden.
	FileResult RepairOne(const std::filesystem::path& path, bool apply);
}
