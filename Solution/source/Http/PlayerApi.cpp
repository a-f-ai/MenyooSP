/*
* Menyoo PC - Grand Theft Auto V single-player trainer mod
*/
#include "PlayerApi.h"

#include "PlayerInput.h"

#include "../Natives/natives2.h"
#include "../Scripting/GTAped.h"
#include "../Scripting/GTAvehicle.h"
#include "../Scripting/Model.h"
#include "../Scripting/Tasks.h"
#include "../Scripting/enums.h"
#include "../Submenus/PedModelChanger.h"
#include "../Submenus/VehicleOptions.h"
#include "../Util/StringManip.h"

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

	Response SetModel(const SetModelRequest& request)
	{
		if (NETWORK_IS_IN_SESSION())
			return Fail(409, "player model changes through this endpoint are single-player only");

		GTAped player = Player();
		if (!player.Exists())
			return Fail(503, "the player ped does not exist yet; the game may still be loading");

		const GTAmodel::Model model(request.model);
		if (!model.IsInCdImage())
			return Fail(422, "the requested player model is not installed in the game files");
		if (!model.IsPed())
			return Fail(422, "the requested player model is installed but is not a ped");
		if (!model.Load(4000))
			return Fail(503, "the requested player model did not stream in within 4000 ms");

		if (player.Model().hash == request.model)
		{
			model.Unload();
			json payload{
				{ "status", "unchanged" },
				{ "model", request.modelLabel },
				{ "hash", IntToHexString(request.model, true) },
			};
			if (!request.alias.empty())
				payload["alias"] = request.alias;
			return Serialise(200, payload);
		}

		sub::ChangeModel(model);
		player = Player();
		if (!player.Exists() || player.Model().hash != request.model)
			return Fail(500, "Menyoo completed the model change but the player model did not match the request");

		json payload{
			{ "status", "changed" },
			{ "playerId", player.GetHandle() },
			{ "model", request.modelLabel },
			{ "hash", IntToHexString(request.model, true) },
		};
		if (!request.alias.empty())
		{
			payload["alias"] = request.alias;
			payload["variant"] = request.variant;
		}
		return Serialise(200, payload);
	}

	Response SpawnVehicleAndSeat(const SpawnVehicleRequest& request)
	{
		if (NETWORK_IS_IN_SESSION())
			return Fail(409, "vehicle spawning through this endpoint is single-player only");

		GTAped player = Player();
		if (!player.Exists())
			return Fail(503, "the player ped does not exist yet; the game may still be loading");

		const GTAmodel::Model model(request.model);
		if (!model.IsInCdImage())
			return Fail(422, "the requested vehicle model is not installed in the game files");
		if (!model.IsVehicle())
			return Fail(422, "the requested vehicle model is installed but is not a vehicle");
		if (!model.Load(4000))
			return Fail(503, "the requested vehicle model did not stream in within 4000 ms");

		const Vector3 playerPosition = player.GetPosition();
		const float x = request.hasPosition ? request.x : playerPosition.x;
		const float y = request.hasPosition ? request.y : playerPosition.y;
		const float z = request.hasPosition ? request.z : playerPosition.z;
		const float heading = request.hasHeading ? request.heading : player.GetHeading();

		Vehicle handle = CREATE_VEHICLE(request.model, x, y, z, heading, true, true, false);
		model.Unload();
		GTAvehicle vehicle(handle);
		if (!vehicle.Exists())
			return Fail(500, "CREATE_VEHICLE did not produce a live vehicle");

		player.SetIntoVehicle(vehicle, SEAT_DRIVER);
		if (!player.CurrentVehicle().Exists() || player.CurrentVehicle().GetHandle() != handle)
		{
			DELETE_VEHICLE(&handle);
			return Fail(500, "the vehicle was created but the player could not be placed in the driver seat; the vehicle was removed");
		}

		SET_VEHICLE_IS_STOLEN(handle, false);
		json payload{
			{ "status", "seated" },
			{ "vehicleId", handle },
			{ "seat", "driver" },
			{ "model", request.modelLabel },
			{ "hash", IntToHexString(request.model, true) },
			{ "position", { { "x", x }, { "y", y }, { "z", z } } },
			{ "heading", heading },
		};
		if (!request.alias.empty())
			payload["alias"] = request.alias;
		return Serialise(201, payload);
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
