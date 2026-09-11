/*
* Menyoo PC - Grand Theft Auto V single-player trainer mod
*
* The timeline window. Runs on the render thread inside the existing ImGui
* frame, so it may only read and write CameraPaths::State() under the mutex
* and raise requests; the script thread does everything that touches the game.
*/
#pragma once

namespace sub::Spooner::CameraPathUI
{
	void Draw();
}
