/*
* Menyoo PC - Grand Theft Auto V single-player trainer mod
*
* Searchable catalogues of what can be spawned or played. Menyoo already reads
* these from menyooStuff at start-up: PedList.xml, PropList.txt, PedAnimList.txt
* and the built-in vehicle and scenario tables.
*
* They are far too large to serve whole - nearly 20k props and 240k animation
* entries - so every endpoint here is a search with a hard result cap.
*/
#pragma once

#include "CommandQueue.h"

#include <string>

namespace Http::CatalogApi
{
	struct Query
	{
		std::string text;      // case-insensitive substring over name and label
		std::string filter;    // vehicle class, or animation dictionary
		int limit;
		int offset;
		bool verifyInstalled;  // check each hit against the game files
	};

	Response Peds(const Query& query);
	Response Vehicles(const Query& query);
	Response Props(const Query& query);
	Response Scenarios(const Query& query);
	Response Animations(const Query& query);
}
