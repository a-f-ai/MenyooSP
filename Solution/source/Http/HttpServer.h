/*
* Menyoo PC - Grand Theft Auto V single-player trainer mod
*
* Localhost HTTP control surface. Owns request parsing, validation and status
* codes; knows nothing about GTA natives. Route handlers hand their work to
* the CommandQueue so it runs on the ScriptHookV fiber.
*/
#pragma once

namespace Http
{
	namespace Server
	{
		// Starts the listener on its own thread. Safe to call once, from the
		// fiber, during plugin start-up.
		void Start();

		// Runs every command the HTTP thread has queued. Must be called from
		// the ScriptHookV fiber, once per frame.
		void DrainCommands();

		void Shutdown();
	}
}
