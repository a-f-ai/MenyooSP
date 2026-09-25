/*
* Menyoo PC - Grand Theft Auto V single-player trainer mod
*
* Camera paths on disk, in menyooStuff/CameraPaths. They live outside the map
* files on purpose: a move is worth reusing across scenes.
*/
#pragma once

#include <string>
#include <vector>

namespace sub::Spooner::CameraPaths
{
	class CameraPath;

	namespace Files
	{
		std::string Directory();

		// Names without the .xml, sorted.
		std::vector<std::string> List();

		bool Save(const std::string& name, const CameraPath& path, std::string& failure);
		bool Load(const std::string& name, CameraPath& path, std::string& failure);
		bool Delete(const std::string& name, std::string& failure);

		// Rejects anything that could escape the directory or name a non-path.
		bool NameIsSafe(const std::string& name);
	}
}
