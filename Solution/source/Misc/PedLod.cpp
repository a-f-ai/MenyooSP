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
		std::atomic<float> g_multiplier{ kGameDefault };
		bool g_overrideApplied = false;
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
		if (multiplier == kGameDefault && !g_overrideApplied) return;

		std::vector<Entity> peds;
		GTAmemory::GetPedHandles(peds);
		for (Entity ped : peds)
			SET_PED_LOD_MULTIPLIER(ped, multiplier);
		// Restore an earlier boost once; subsequent default ticks do not enumerate peds.
		g_overrideApplied = multiplier != kGameDefault;
	}
}
