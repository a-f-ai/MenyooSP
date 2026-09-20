/*
* Menyoo PC - Grand Theft Auto V single-player trainer mod
*
* Keeps every ped on its full-detail mesh further out than the game would.
* The game's ped LOD distances are tuned for play, and addon peds ship with
* auto-decimated lower meshes that read as featureless blobs in a wide shot.
* The multiplier scales those distances for every ped in the world, every
* frame, so it holds no matter how a ped was created: spooner, map load,
* picker or the HTTP bridge.
*/
#pragma once

namespace PedLod
{
	constexpr float kGameDefault = 1.0f;
	constexpr float kMax = 50.0f;

	float Multiplier();
	bool SetMultiplier(float value);
	bool Enabled();
	bool SetEnabled(bool enabled); // script thread: disabling restores existing peds once
	bool Configure(const char* enabled, const char* multiplier);
	const char* Error();

	void Tick(); // script thread, once per frame
}
