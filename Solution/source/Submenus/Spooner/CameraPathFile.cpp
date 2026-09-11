/*
* Menyoo PC - Grand Theft Auto V single-player trainer mod
*/
#include "CameraPathFile.h"

#include "CameraPath.h"

#include "../../Util/ExePath.h"
#include "../../Util/FileLogger.h"

#include <pugixml/src/pugixml.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <system_error>

namespace sub::Spooner::CameraPaths::Files
{
	namespace
	{
		std::string PathFor(const std::string& name)
		{
			return Directory() + name + ".xml";
		}
	}

	std::string Directory()
	{
		const std::string directory = GetPathffA(Pathff::Main, true) + "CameraPaths\\";
		std::error_code ignored;
		std::filesystem::create_directories(directory, ignored);
		return directory;
	}

	bool NameIsSafe(const std::string& name)
	{
		if (name.empty() || name.size() > 120)
			return false;
		if (name.find("..") != std::string::npos)
			return false;
		for (char c : name)
		{
			const bool allowed = std::isalnum(static_cast<unsigned char>(c)) ||
				c == '_' || c == '-' || c == ' ';
			if (!allowed)
				return false;
		}
		return true;
	}

	std::vector<std::string> List()
	{
		std::vector<std::string> names;
		std::error_code failure;
		for (const auto& entry : std::filesystem::directory_iterator(Directory(), failure))
		{
			if (failure)
				break;
			if (!entry.is_regular_file())
				continue;
			if (entry.path().extension() != ".xml")
				continue;
			names.push_back(entry.path().stem().string());
		}
		std::sort(names.begin(), names.end());
		return names;
	}

	bool Save(const std::string& name, const CameraPath& path, std::string& failure)
	{
		if (!NameIsSafe(name))
		{
			failure = "a path name may contain only letters, digits, spaces, dash and underscore";
			return false;
		}

		pugi::xml_document doc;
		auto declaration = doc.append_child(pugi::node_declaration);
		declaration.append_attribute("version") = "1.0";
		declaration.append_attribute("encoding") = "UTF-8";

		auto root = doc.append_child("CameraPath");
		root.append_attribute("name") = path.name.c_str();
		root.append_attribute("loop") = path.loop;
		root.append_attribute("constantSpeed") = path.constantSpeed;
		root.append_attribute("smoothing") =
			path.smoothing == Smoothing::WholePath ? "WholePath" : "PerKey";
		root.append_attribute("pathEasing") = EasingName(path.pathEasing);

		for (const CameraKey& key : path.keys)
		{
			auto node = root.append_child("Key");
			node.append_attribute("time") = key.time;
			node.append_attribute("easing") = EasingName(key.easing);
			node.append_attribute("fov") = key.fov;

			auto position = node.append_child("Position");
			position.append_attribute("X") = key.position.x;
			position.append_attribute("Y") = key.position.y;
			position.append_attribute("Z") = key.position.z;

			auto rotation = node.append_child("Rotation");
			rotation.append_attribute("Pitch") = key.rotation.x;
			rotation.append_attribute("Roll") = key.rotation.y;
			rotation.append_attribute("Yaw") = key.rotation.z;
		}

		if (!doc.save_file(PathFor(name).c_str()))
		{
			failure = "could not write " + PathFor(name);
			return false;
		}
		addlog(ige::LogType::LOG_INFO, "Saved camera path to " + PathFor(name));
		return true;
	}

	bool Load(const std::string& name, CameraPath& path, std::string& failure)
	{
		if (!NameIsSafe(name))
		{
			failure = "a path name may contain only letters, digits, spaces, dash and underscore";
			return false;
		}

		pugi::xml_document doc;
		if (doc.load_file(PathFor(name).c_str()).status != pugi::status_ok)
		{
			failure = "cannot read " + PathFor(name);
			return false;
		}

		auto root = doc.child("CameraPath");
		if (!root)
		{
			failure = "not a camera path file: no <CameraPath> element";
			return false;
		}

		CameraPath loaded;
		loaded.name = root.attribute("name").as_string(name.c_str());
		loaded.loop = root.attribute("loop").as_bool(false);
		loaded.constantSpeed = root.attribute("constantSpeed").as_bool(true);
		// Files written before whole-path smoothing existed get it anyway: it
		// is what they were trying to look like.
		loaded.smoothing = std::string(root.attribute("smoothing").as_string("WholePath")) == "PerKey"
			? Smoothing::PerKey : Smoothing::WholePath;
		loaded.pathEasing = EasingFromName(root.attribute("pathEasing").as_string("In-Out Sine"));

		for (auto node = root.child("Key"); node; node = node.next_sibling("Key"))
		{
			CameraKey key;
			key.time = node.attribute("time").as_float();
			key.easing = EasingFromName(node.attribute("easing").as_string("Linear"));
			key.fov = node.attribute("fov").as_float(50.0f);

			auto position = node.child("Position");
			key.position = Vector3(position.attribute("X").as_float(),
				position.attribute("Y").as_float(), position.attribute("Z").as_float());

			auto rotation = node.child("Rotation");
			key.rotation = Vector3(rotation.attribute("Pitch").as_float(),
				rotation.attribute("Roll").as_float(), rotation.attribute("Yaw").as_float());

			loaded.keys.push_back(key);
		}

		if (loaded.keys.empty())
		{
			failure = "the file holds no keys";
			return false;
		}

		loaded.Rebuild();
		path = loaded;
		return true;
	}

	bool Delete(const std::string& name, std::string& failure)
	{
		if (!NameIsSafe(name))
		{
			failure = "a path name may contain only letters, digits, spaces, dash and underscore";
			return false;
		}
		std::error_code problem;
		if (!std::filesystem::remove(PathFor(name), problem))
		{
			failure = problem ? problem.message() : "no path named \"" + name + "\"";
			return false;
		}
		return true;
	}
}
