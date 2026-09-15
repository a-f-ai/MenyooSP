/*
* Menyoo PC - Grand Theft Auto V single-player trainer mod
*
* Player actions executed on the ScriptHookV fiber.
*/
#pragma once

#include "CommandQueue.h"

#include <vector>

namespace Http::PlayerApi
{
	struct EnterVehicleRequest
	{
		int vehicleId;
	};

	struct TeleportRequest
	{
		float x, y, z;
	};

	struct ControlRequest
	{
		int control;
		float value;
	};

	struct DriveToRequest
	{
		float x, y, z;
		float speed;
		int drivingStyle;
		bool pushEntities;
	};

	struct ControlDiagnosticRequest
	{
		int controlGroup;
		int control;
	};

	Response EnterVehicle(const EnterVehicleRequest& request);
	Response Teleport(const TeleportRequest& request);
	Response ApplyControls(const std::vector<ControlRequest>& controls, int holdMilliseconds);
	Response ReleaseControls();
	Response GetControlDiagnostic(const ControlDiagnosticRequest& request);
	Response DriveTo(const DriveToRequest& request);
	Response StopDriving();
	void TickControls();
}
