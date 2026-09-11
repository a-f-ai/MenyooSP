/*
* Menyoo PC - Grand Theft Auto V single-player trainer mod
*
* Persisting a scene as a Menyoo map XML in menyooStuff/Spooner, which is the
* same format and directory the trainer's own menu reads and writes.
*/
#pragma once

#include "CommandQueue.h"

#include <string>

namespace Http::MapApi
{
	Response ListMaps();
	Response SaveMap(const std::string& name);
	Response LoadMap(const std::string& name);
	Response ClearSpawned();
}
