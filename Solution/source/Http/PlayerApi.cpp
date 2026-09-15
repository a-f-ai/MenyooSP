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
#include "../Submenus/VehicleOptions.h"

#include <json/single_include/nlohmann/json.hpp>

using json = nlohmann::json;

namespace Http::PlayerApi
{
	namespace
	{
		struct ControlDiagnostic
		{
			int control;
			float requestedValue;
			bool lastInjectionAttempted;
			bool lastInjectionAccepted;
		};

		std::vector<ControlRequest> g_heldControls;
		std::vector<ControlDiagnostic> g_lastControls;
		int g_controlReleaseTime = 0;

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

	Response ApplyControls(const std::vector<ControlRequest>& controls, int holdMilliseconds)
	{
		for (const ControlRequest& control : controls)
		{
			if (!PlayerInput::IsControlIdValid(control.control) || !PlayerInput::IsValueValid(control.value))
				return Fail(400, "controls contain an invalid control id or value");
		}
		if (!PlayerInput::IsHoldMillisecondsValid(holdMilliseconds))
			return Fail(400, "holdMilliseconds must be between 1 and 1000");

		g_heldControls = controls;
		g_lastControls.clear();
		g_lastControls.reserve(controls.size());
		for (const ControlRequest& control : controls)
			g_lastControls.push_back(ControlDiagnostic{ control.control, control.value, false, false });
		g_controlReleaseTime = GET_GAME_TIMER() + holdMilliseconds;

		return Serialise(202, json{
			{ "status", "holding" },
			{ "controls", controls.size() },
			{ "holdMilliseconds", holdMilliseconds },
		});
	}

	Response ReleaseControls()
	{
		g_heldControls.clear();
		g_controlReleaseTime = 0;
		return Serialise(200, json{ { "status", "released" } });
	}

	Response GetControlDiagnostic(const ControlDiagnosticRequest& request)
	{
		if (!PlayerInput::IsControlGroupValid(request.controlGroup))
			return Fail(400, "controlGroup must be between 0 and 2");
		if (!PlayerInput::IsControlIdValid(request.control))
			return Fail(400, "control must be between 0 and 337");

		for (const ControlDiagnostic& control : g_lastControls)
		{
			if (control.control != request.control)
				continue;

			const bool holding = !g_heldControls.empty() && GET_GAME_TIMER() < g_controlReleaseTime;
			return Serialise(200, json{
				{ "controlGroup", request.controlGroup },
				{ "control", request.control },
				{ "injectedControlGroup", PlayerInput::kInjectedControlGroup },
				{ "requestedValue", control.requestedValue },
				{ "holding", holding },
				{ "lastInjectionAttempted", control.lastInjectionAttempted },
				{ "lastInjectionAccepted", control.lastInjectionAccepted },
				{ "controlEnabled", PAD::IS_CONTROL_ENABLED(request.controlGroup, request.control) != 0 },
				{ "controlPressed", PAD::IS_CONTROL_PRESSED(request.controlGroup, request.control) != 0 },
				{ "controlNormal", PAD::GET_CONTROL_NORMAL(request.controlGroup, request.control) },
			});
		}

		return Fail(404, "the requested control was not part of the last input hold");
	}

	Response DriveTo(const DriveToRequest& request)
	{
		GTAped player = Player();
		if (!player.Exists())
			return Fail(503, "the player ped does not exist yet; the game may still be loading");

		GTAvehicle vehicle = player.CurrentVehicle();
		if (!vehicle.Exists())
			return Fail(409, "the player must be driving a vehicle before starting auto drive");
		if (!vehicle.GetDriveable())
			return Fail(422, "the current vehicle is not driveable");

		sub::VehicleAutoDrive::Start(Vector3(request.x, request.y, request.z), request.speed, request.drivingStyle, request.pushEntities);
		return Serialise(202, json{
			{ "status", "driving" },
			{ "destination", { { "x", request.x }, { "y", request.y }, { "z", request.z } } },
			{ "speedMetresPerSecond", request.speed },
		});
	}

	Response StopDriving()
	{
		sub::VehicleAutoDrive::Stop();
		return Serialise(200, json{ { "status", "stopped" } });
	}

	void TickControls()
	{
		if (g_heldControls.empty())
			return;

		if (GET_GAME_TIMER() >= g_controlReleaseTime)
		{
			g_heldControls.clear();
			g_controlReleaseTime = 0;
			return;
		}

		for (size_t index = 0; index < g_heldControls.size(); ++index)
		{
			const ControlRequest& control = g_heldControls[index];
			g_lastControls[index].lastInjectionAttempted = true;
			g_lastControls[index].lastInjectionAccepted =
				PAD::SET_CONTROL_VALUE_NEXT_FRAME(PlayerInput::kInjectedControlGroup, control.control, control.value) != 0;
		}
	}
}
