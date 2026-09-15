/*
* Menyoo PC - Grand Theft Auto V single-player trainer mod
*
* Validation shared by the HTTP parser and the game-thread player API.
*/
#pragma once

#include <cmath>

namespace Http::PlayerInput
{
	constexpr int kInjectedControlGroup = 2;

	inline bool IsControlGroupValid(int controlGroup)
	{
		return controlGroup >= 0 && controlGroup <= 2;
	}

	inline bool IsControlIdValid(int control)
	{
		return control >= 0 && control <= 337;
	}

	inline bool IsValueValid(float value)
	{
		return std::isfinite(value) && value >= -1.0f && value <= 1.0f;
	}

	inline bool IsHoldMillisecondsValid(int milliseconds)
	{
		return milliseconds >= 1 && milliseconds <= 1000;
	}
}
