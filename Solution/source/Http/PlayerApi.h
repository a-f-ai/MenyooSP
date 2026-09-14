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

	Response EnterVehicle(const EnterVehicleRequest& request);
	Response Teleport(const TeleportRequest& request);
	Response ApplyControls(const std::vector<ControlRequest>& controls, int holdMilliseconds);
	Response ReleaseControls();
	void TickControls();
}
