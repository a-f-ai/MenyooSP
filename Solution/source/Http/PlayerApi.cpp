/*
* Menyoo PC - Grand Theft Auto V single-player trainer mod
*/
#include "PlayerApi.h"

#include "PlayerInput.h"

#include "../Natives/natives2.h"
#include "../Scripting/GTAped.h"
#include "../Scripting/GTAvehicle.h"
#include "../Scripting/Tasks.h"
#include "../Scripting/enums.h"

#include <json/single_include/nlohmann/json.hpp>

using json = nlohmann::json;

namespace Http::PlayerApi
{
	namespace
	{
		Response Serialise(int status, const json& payload)
		{
			return Response{ status, payload.dump(2, ' ', false, json::error_handler_t::replace) };
		}

		Response Fail(int status, const char* message)
		{
			return Serialise(status, json{ { "error", message } });
		}

		GTAped Player()
		{
			return GTAped(PLAYER_PED_ID());
		}
	}

	Response EnterVehicle(const EnterVehicleRequest& request)
	{
		GTAped player = Player();
		if (!player.Exists())
			return Fail(503, "the player ped does not exist yet; the game may still be loading");

		if (player.CurrentVehicle().Exists())
			return Fail(409, "the player is already in a vehicle; leave it before starting another entry task");

		if (GET_ENTITY_TYPE(request.vehicleId) != 2)
			return Fail(422, "vehicleId does not identify a live vehicle");

		GTAvehicle vehicle(request.vehicleId);
		if (!vehicle.Exists())
			return Fail(422, "vehicleId does not identify a live vehicle");

		player.Task().EnterVehicle(vehicle, SEAT_DRIVER);
		return Serialise(202, json{
			{ "status", "entering" },
			{ "vehicleId", request.vehicleId },
			{ "seat", "driver" },
		});
	}

	Response Teleport(const TeleportRequest& request)
	{
		GTAped player = Player();
		if (!player.Exists())
			return Fail(503, "the player ped does not exist yet; the game may still be loading");

		player.SetPosition(Vector3(request.x, request.y, request.z));
		return Serialise(200, json{
			{ "position", { { "x", request.x }, { "y", request.y }, { "z", request.z } } },
		});
	}

	Response ApplyControls(const std::vector<ControlRequest>& controls)
	{
		for (const ControlRequest& control : controls)
		{
			if (!PlayerInput::IsControlIdValid(control.control) || !PlayerInput::IsValueValid(control.value))
				return Fail(400, "controls contain an invalid control id or value");
			if (!PAD::SET_CONTROL_VALUE_NEXT_FRAME(0, control.control, control.value))
				return Fail(422, "GTA refused a virtual control value");
		}

		return Serialise(202, json{ { "status", "queuedForNextFrame" }, { "controls", controls.size() } });
	}
}
