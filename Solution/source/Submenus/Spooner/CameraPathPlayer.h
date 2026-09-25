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
		// Selection is by key id, not index, because sorting by time renumbers
		// the vector under any edit.
		std::vector<unsigned> selectedIds;
		int selectedKey = -1;   // index of the last one clicked, for the editor panel
		// The trajectory and its key markers are authoring aids. They are drawn
		// into the world, so they would sit in the middle of any shot.
		bool showPath = true;

		// Raised by the window or the HTTP bridge, cleared by the script thread.
		bool requestAddKeyAtCamera = false;
		bool requestUpdateKeyFromCamera = false;
		int requestDeleteKey = -1;
		std::vector<unsigned> requestDeleteIds;
		bool requestPlay = false;
		bool requestPause = false;
		bool requestStop = false;
		bool requestSeek = false;          // with `time` already set
		bool requestPreviewKey = false;    // jump the camera to `selectedKey`
		// Keys cut from the path, times relative to the first of them.
		std::vector<CameraKey> clipboard;
		float pauseSeconds = 1.0f;

		std::string requestLoadName;
		std::string requestSaveName;
		bool requestNew = false;

		// A single held pose with no path behind it: "stand here, look there".
		// Any transport request ends it, so play/seek/stop behave as before.
		bool requestLook = false;
		bool holdingLook = false;
		CameraPose lookPose;

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

	// Easing a new key gets - from the window, the ] key or POST /camera/keys.
	// An index into EasingFromIndex, kept in menyooConfig.ini as CameraPathDefaultEasing.
	int DefaultEasingIndex();
	void SetDefaultEasingIndex(int index);

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
