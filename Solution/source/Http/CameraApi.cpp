/*
* Menyoo PC - Grand Theft Auto V single-player trainer mod
*/
#include "CameraApi.h"

#include "Json.h"

#include "../Submenus/Spooner/CameraPath.h"
#include "../Submenus/Spooner/CameraPathFile.h"
#include "../Submenus/Spooner/CameraPathPlayer.h"

#include <cmath>
#include <mutex>
#include <vector>

using json = nlohmann::json;
using namespace sub::Spooner::CameraPaths;

namespace Http::CameraApi
{
	namespace
	{
		Response Ok(const json& payload) { return Response{ 200, Http::Json::Dump(payload) }; }

		Response Fail(int status, const std::string& message)
		{
			return Response{ status, Http::Json::Dump(json{ { "error", message } }) };
		}

		json DescribeKey(const CameraKey& key)
		{
			return json{
				{ "time", key.time },
				{ "position", { { "x", key.position.x }, { "y", key.position.y }, { "z", key.position.z } } },
				{ "rotation", { { "pitch", key.rotation.x }, { "roll", key.rotation.y }, { "yaw", key.rotation.z } } },
				{ "fov", key.fov },
				{ "easing", EasingName(key.easing) },
			};
		}

		json DescribePath(const PlayerState& state)
		{
			json keys = json::array();
			for (const CameraKey& key : state.path.keys)
				keys.push_back(DescribeKey(key));

			const char* transport = "stopped";
			if (state.transport == sub::Spooner::CameraPaths::Transport::Playing) transport = "playing";
			else if (state.transport == sub::Spooner::CameraPaths::Transport::Paused) transport = "paused";

			return json{
				{ "name", state.path.name },
				{ "loop", state.path.loop },
				{ "constantSpeed", state.path.constantSpeed },
				{ "duration", state.path.Duration() },
				{ "time", state.time },
				{ "speed", state.speed },
				{ "transport", transport },
				{ "keyCount", state.path.keys.size() },
				{ "keys", std::move(keys) },
				{ "status", state.status },
			};
		}

		// Throws ApiError on bad input, which is safe: this never runs on the fiber.
		CameraKey ParseKey(const json& item)
		{
			using namespace Http::Json;
			if (!item.is_object())
				throw ApiError(400, "every key must be an object");

			CameraKey key;
			key.time = Number(item, "time");

			const json& position = Field(item, "position");
			if (!position.is_object())
				throw ApiError(400, "\"position\" must be an object with x, y and z");
			key.position = Vector3(Number(position, "x"), Number(position, "y"), Number(position, "z"));

			if (item.contains("rotation"))
			{
				const json& rotation = item.at("rotation");
				if (!rotation.is_object())
					throw ApiError(400, "\"rotation\" must be an object with pitch, roll and yaw");
				key.rotation = Vector3(
					OptionalNumber(rotation, "pitch", 0.0f),
					OptionalNumber(rotation, "roll", 0.0f),
					OptionalNumber(rotation, "yaw", 0.0f));
			}

			key.fov = OptionalNumber(item, "fov", 50.0f);
			key.easing = EasingFromName(OptionalString(item, "easing", EasingName(EasingFromIndex(DefaultEasingIndex()))));
			return key;
		}
	}

	Response GetPath()
	{
		std::lock_guard<std::mutex> lock(StateMutex());
		return Ok(DescribePath(State()));
	}

	Response ReplacePath(const json& body)
	{
		const json& keys = Http::Json::Field(body, "keys");
		if (!keys.is_array())
			throw ApiError(400, "\"keys\" must be an array");

		std::vector<CameraKey> parsed;
		parsed.reserve(keys.size());
		for (const json& item : keys)
			parsed.push_back(ParseKey(item));

		std::lock_guard<std::mutex> lock(StateMutex());
		PlayerState& state = State();
		state.path.keys = std::move(parsed);
		state.path.name = Http::Json::OptionalString(body, "name", state.path.name);
		state.path.loop = Http::Json::OptionalBool(body, "loop", state.path.loop);
		state.path.constantSpeed = Http::Json::OptionalBool(body, "constantSpeed", state.path.constantSpeed);
		state.path.Rebuild();
		state.time = 0.0f;
		state.selectedKey = -1;
		state.status = "path replaced over HTTP";
		return Ok(DescribePath(state));
	}

	Response AppendKey(const json& body)
	{
		std::lock_guard<std::mutex> lock(StateMutex());
		PlayerState& state = State();

		// With no body fields, take the key from wherever the user's camera is:
		// the script thread fills that in on its next tick.
		if (!body.contains("position"))
		{
			state.requestAddKeyAtCamera = true;
			return Response{ 202, Http::Json::Dump(json{
				{ "queued", "key will be added at the current camera on the next frame" } }) };
		}

		state.path.AddKey(ParseKey(body));
		state.status = "key added over HTTP";
		return Ok(DescribePath(state));
	}

	Response DeleteKey(int index)
	{
		std::lock_guard<std::mutex> lock(StateMutex());
		PlayerState& state = State();
		if (index < 0 || index >= static_cast<int>(state.path.keys.size()))
			return Fail(404, "no key at index " + std::to_string(index));
		state.path.RemoveKey(static_cast<size_t>(index));
		state.status = "key removed over HTTP";
		return Ok(DescribePath(state));
	}

	Response Transport(const std::string& action, float time)
	{
		std::lock_guard<std::mutex> lock(StateMutex());
		PlayerState& state = State();

		if (action == "play")
		{
			if (state.path.keys.size() < 2)
				return Fail(422, "a path needs at least two keys before it can play");
			state.requestPlay = true;
		}
		else if (action == "pause") state.requestPause = true;
		else if (action == "stop")  state.requestStop = true;
		else if (action == "seek")
		{
			state.time = time;
			state.requestSeek = true;
		}
		else return Fail(400, "action must be play, pause, stop or seek");

		return Ok(json{ { "queued", action }, { "time", state.time } });
	}

	Response Look(const json& body)
	{
		using namespace Http::Json; // HTTP thread: Field/Number may throw ApiError here
		const json& position = Field(body, "position");
		if (!position.is_object())
			return Fail(400, "\"position\" must be an object with x, y and z");
		CameraPose pose;
		pose.position = Vector3(Number(position, "x"), Number(position, "y"), Number(position, "z"));
		pose.fov = OptionalNumber(body, "fov", 50.0f);

		if (body.contains("at"))
		{
			const json& at = body.at("at");
			if (!at.is_object())
				return Fail(400, "\"at\" must be an object with x, y and z");
			const float dx = Number(at, "x") - pose.position.x;
			const float dy = Number(at, "y") - pose.position.y;
			const float dz = Number(at, "z") - pose.position.z;
			const float flat = std::sqrt(dx * dx + dy * dy);
			if (flat < 0.001f && std::fabs(dz) < 0.001f)
				return Fail(400, "\"at\" is the camera position itself");
			// Game heading: 0 looks along +y, 90 along -x, so yaw = atan2(-dx, dy).
			pose.rotation = Vector3(
				static_cast<float>(std::atan2(dz, flat) * 180.0 / MATH_PI), 0.0f,
				static_cast<float>(std::atan2(-dx, dy) * 180.0 / MATH_PI));
		}
		else
		{
			const json& rotation = Field(body, "rotation");
			if (!rotation.is_object())
				return Fail(400, "give \"at\" or \"rotation\" {pitch, roll, yaw}");
			pose.rotation = Vector3(Number(rotation, "pitch"), OptionalNumber(rotation, "roll", 0.0f), Number(rotation, "yaw"));
		}

		std::lock_guard<std::mutex> lock(StateMutex());
		PlayerState& state = State();
		state.lookPose = pose;
		state.requestLook = true;
		return Ok(json{
			{ "queued", "look" },
			{ "position", { { "x", pose.position.x }, { "y", pose.position.y }, { "z", pose.position.z } } },
			{ "rotation", { { "pitch", pose.rotation.x }, { "roll", pose.rotation.y }, { "yaw", pose.rotation.z } } },
			{ "fov", pose.fov },
			{ "note", "held until POST /camera/stop (gives the view back) or play/seek" },
		});
	}

	Response ListSaved()
	{
		json names = json::array();
		for (const std::string& name : Files::List())
			names.push_back(name);
		return Ok(json{ { "directory", Files::Directory() }, { "paths", std::move(names) } });
	}

	Response SavePath(const std::string& name)
	{
		if (!Files::NameIsSafe(name))
			return Fail(400, "a path name may contain only letters, digits, spaces, dash and underscore");
		std::lock_guard<std::mutex> lock(StateMutex());
		PlayerState& state = State();
		if (state.path.Empty())
			return Fail(422, "there is nothing to save");
		state.requestSaveName = name;
		return Response{ 202, Http::Json::Dump(json{ { "queued", "save " + name } }) };
	}

	Response LoadPath(const std::string& name)
	{
		if (!Files::NameIsSafe(name))
			return Fail(400, "a path name may contain only letters, digits, spaces, dash and underscore");
		std::lock_guard<std::mutex> lock(StateMutex());
		State().requestLoadName = name;
		return Response{ 202, Http::Json::Dump(json{ { "queued", "load " + name } }) };
	}
}
