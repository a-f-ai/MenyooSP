/*
* Menyoo PC - Grand Theft Auto V single-player trainer mod
*
* Playing a camera path, and the state the ImGui window edits.
*
* The window runs on the render thread and must not touch natives, so it edits
* this state under the mutex and raises requests; the script thread drains them
* in Tick() and is the only place the camera is actually driven.
*/
#pragma once

#include "CameraPath.h"

#include <mutex>
#include <string>
#include <vector>

namespace sub::Spooner::CameraPaths
{
	enum class Transport
	{
		Stopped,
		Playing,
		Paused,
	};

	struct PlayerState
	{
		CameraPath path;

		Transport transport = Transport::Stopped;
		float time = 0.0f;
		float speed = 1.0f;
		int selectedKey = -1;

		// Raised by the window or the HTTP bridge, cleared by the script thread.
		bool requestAddKeyAtCamera = false;
		bool requestUpdateKeyFromCamera = false;
		int requestDeleteKey = -1;
		bool requestPlay = false;
		bool requestPause = false;
		bool requestStop = false;
		bool requestSeek = false;          // with `time` already set
		bool requestPreviewKey = false;    // jump the camera to `selectedKey`
		std::string requestLoadName;
		std::string requestSaveName;
		bool requestNew = false;

		// Written by the script thread for the window to read.
		Vector3 liveCameraPosition;
		Vector3 liveCameraRotation;
		float liveCameraFov = 50.0f;
		bool spoonerCameraActive = false;
		bool pathCameraOwnsView = false;
		std::string status;
		std::vector<std::string> savedPathNames;
		bool savedPathNamesStale = true;
	};

	// Both are only valid while the mutex is held.
	std::mutex& StateMutex();
	PlayerState& State();

	// Script thread, once per frame.
	void Tick();

	// Releases the camera and hands the view back to the game.
	void Release();

	bool IsWindowVisible();
	void SetWindowVisible(bool visible);
	void ToggleWindow();

	// While cursor mode is on the mouse belongs to the window and the game
	// stops reading it, which is the same bargain the spooner's gizmo makes
	// with its "lock camera" key. Off by default, so opening the window never
	// costs you control of the camera.
	bool IsCursorMode();
	void ToggleCursorMode();
}
