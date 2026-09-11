/*
* Menyoo PC - Grand Theft Auto V single-player trainer mod
*
* Camera paths over HTTP.
*
* Unlike the rest of the bridge these run on the HTTP thread. They only touch
* the mutex-guarded player state and raise requests that the script thread
* drains in its own tick, so no native is ever called from here and nothing
* needs to wait for a frame.
*/
#pragma once

#include "CommandQueue.h"

#include <json/single_include/nlohmann/json.hpp>

#include <string>

namespace Http::CameraApi
{
	Response GetPath();
	Response ReplacePath(const nlohmann::json& body);
	Response AppendKey(const nlohmann::json& body);
	Response DeleteKey(int index);
	Response Transport(const std::string& action, float time);
	Response ListSaved();
	Response SavePath(const std::string& name);
	Response LoadPath(const std::string& name);
}
