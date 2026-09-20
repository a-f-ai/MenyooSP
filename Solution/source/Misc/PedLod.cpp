/*
* Menyoo PC - Grand Theft Auto V single-player trainer mod
*/
#include "PedLod.h"

#include "..\macros.h"
#include "..\Natives\natives2.h"
#include "..\Memory\GTAmemory.h"

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <vector>

namespace PedLod
{
	namespace
	{
		std::atomic<float> g_multiplier{ kGameDefault };
		std::atomic<bool> g_enabled{ false };
		std::atomic<const char*> g_error{ "" };
		bool g_overrideApplied = false;
	}

	float Multiplier()
	{
		return g_multiplier.load();
	}

	const char* Error() { return g_error.load(); }
	bool Enabled() { return g_enabled.load(); }

	bool SetMultiplier(float value)
	{
		if (!std::isfinite(value) || value < kGameDefault || value > kMax)
		{
			g_error.store("PedLodMultiplier must be a finite number from 1 to 50; fix config and reload.");
			return false;
		}
		g_multiplier.store(value);
		g_error.store("");
		return true;
	}

	bool SetEnabled(bool enabled)
	{
		if (enabled && Error()[0] != '\0') return false;
		if (!enabled && g_overrideApplied)
		{
			std::vector<Entity> peds;
			GTAmemory::GetPedHandles(peds);
			for (Entity ped : peds) SET_PED_LOD_MULTIPLIER(ped, kGameDefault);
			g_overrideApplied = false;
		}
		g_enabled.store(enabled);
		return true;
	}

	bool Configure(const char* enabled, const char* multiplier)
	{
		SetEnabled(false);
		const bool enable = _stricmp(enabled, "true") == 0;
		if (!enable && _stricmp(enabled, "false") != 0)
		{
			g_error.store("EnablePedLodOverride must be true or false; fix config and reload.");
			return false;
		}
		char* end;
		const float value = std::strtof(multiplier, &end);
		while (std::isspace(static_cast<unsigned char>(*end))) ++end;
		if (end == multiplier || *end != '\0')
		{
			g_error.store("PedLodMultiplier is not a complete number; fix config and reload.");
			return false;
		}
		if (!SetMultiplier(value)) return false;
		return SetEnabled(enable);
	}

	void Tick()
	{
		if (!g_enabled.load()) return;
		if (Error()[0] != '\0') return;
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
