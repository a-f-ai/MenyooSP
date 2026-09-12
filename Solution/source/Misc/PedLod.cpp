/*
* Menyoo PC - Grand Theft Auto V single-player trainer mod
*/
#include "PedLod.h"

#include "..\macros.h"
#include "..\Natives\natives2.h"
#include "..\Memory\GTAmemory.h"

#include <atomic>
#include <vector>

namespace PedLod
{
	namespace
	{
		// Ten times the game's distances keeps the author's addon peds on the
		// full mesh across the whole of a stunt map.
		std::atomic<float> g_multiplier{ 10.0f };
	}

	float Multiplier()
	{
		return g_multiplier.load();
	}

	void SetMultiplier(float value)
	{
		if (value < kGameDefault) value = kGameDefault;
		if (value > kMax) value = kMax;
		g_multiplier.store(value);
	}

	void Tick()
	{
		const float multiplier = g_multiplier.load();
		std::vector<Entity> peds;
		GTAmemory::GetPedHandles(peds);
		for (Entity ped : peds)
			SET_PED_LOD_MULTIPLIER(ped, multiplier);
	}
}
