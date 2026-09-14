/*
* Behaviour tests for the agent-visible control range.
*
* The game-native call itself can only run in GTA, but this boundary is pure
* C++ and prevents invalid control ids or analogue values reaching the fiber.
*/
#include "PlayerInput.h"

#include <cstdio>
#include <string>

namespace
{
	int g_failures = 0;

	void Check(bool condition, const std::string& what)
	{
		if (condition)
		{
			std::printf("  ok   %s\n", what.c_str());
			return;
		}
		std::printf("  FAIL %s\n", what.c_str());
		++g_failures;
	}

	void ControlIdsMatchTheGtaInputTable()
	{
		std::printf("control ids match the GTA input table\n");
		Check(Http::PlayerInput::IsControlIdValid(0), "the first control is valid");
		Check(Http::PlayerInput::IsControlIdValid(337), "the last known control is valid");
		Check(!Http::PlayerInput::IsControlIdValid(-1), "negative controls are rejected");
		Check(!Http::PlayerInput::IsControlIdValid(338), "controls beyond the table are rejected");
	}

	void AnalogueValuesStayInTheNativeRange()
	{
		std::printf("analogue values stay in the native range\n");
		Check(Http::PlayerInput::IsValueValid(-1.0f), "full negative axis is valid");
		Check(Http::PlayerInput::IsValueValid(0.0f), "neutral is valid");
		Check(Http::PlayerInput::IsValueValid(1.0f), "full positive axis is valid");
		Check(!Http::PlayerInput::IsValueValid(-1.01f), "value below the range is rejected");
		Check(!Http::PlayerInput::IsValueValid(1.01f), "value above the range is rejected");
	}

	void HoldDurationHasAnExplicitBound()
	{
		std::printf("input holds have an explicit bound\n");
		Check(Http::PlayerInput::IsHoldMillisecondsValid(1), "a one millisecond hold is valid");
		Check(Http::PlayerInput::IsHoldMillisecondsValid(1000), "a one second hold is valid");
		Check(!Http::PlayerInput::IsHoldMillisecondsValid(0), "a zero length hold is rejected");
		Check(!Http::PlayerInput::IsHoldMillisecondsValid(1001), "holds above one second are rejected");
	}
}

int main()
{
	ControlIdsMatchTheGtaInputTable();
	AnalogueValuesStayInTheNativeRange();
	HoldDurationHasAnExplicitBound();

	if (g_failures == 0)
	{
		std::printf("\nall checks passed\n");
		return 0;
	}
	std::printf("\n%d check(s) failed\n", g_failures);
	return 1;
}
