/*
* Menyoo PC - Grand Theft Auto V single-player trainer mod
*/
#include "CameraPathPlayer.h"

#include <atomic>

#include "CameraPathFile.h"
#include "ImGuiSpooner.h"
#include "SpoonerMode.h"

#include "../../macros.h"
#include "../../Natives/natives2.h"
#include "../../Natives/types.h"
#include "../../Scripting/Camera.h"
#include "../../Scripting/enums.h"
#include "../../Scripting/GameplayCamera.h"
#include "../../Scripting/World.h"
#include "../../Util/FileLogger.h"

#include <algorithm>

namespace sub::Spooner::CameraPaths
{
	namespace
	{
		std::mutex g_mutex;
		PlayerState g_state;
		bool g_windowVisible = false;
		bool g_cursorMode = false;

		// The one camera the path drives. Created when playback or a preview
		// first needs it, destroyed when the view goes back to the game.
		Camera g_camera;
		bool g_cameraOwnsView = false;

		void EnsureCamera()
		{
			if (g_camera.GetHandle() != 0 && g_camera.IsActive())
				return;
			g_camera = World::CreateCamera();
			g_cameraOwnsView = false;
		}

		void TakeView()
		{
			EnsureCamera();
			if (g_cameraOwnsView)
				return;
			World::SetRenderingCamera(g_camera, false);
			g_cameraOwnsView = true;
		}

		void GiveBackView()
		{
			if (g_cameraOwnsView)
			{
				World::SetRenderingCamera(Camera(0), false);
				g_cameraOwnsView = false;
			}
			if (g_camera.GetHandle() != 0)
			{
				g_camera.Destroy();
				g_camera = Camera(0);
			}
		}

		void ApplyPose(const CameraPose& pose)
		{
			TakeView();
			g_camera.SetPosition(pose.position);
			g_camera.SetRotation(pose.rotation);
			g_camera.SetFieldOfView(pose.fov);
		}

		// Whichever camera the user is composing with: the spooner's when it is
		// up, otherwise the gameplay camera.
		void ReadAuthoringCamera(Vector3& position, Vector3& rotation, float& fov, bool& spoonerActive)
		{
			Camera& spoonerCam = SpoonerMode::spoonerModeCamera;
			spoonerActive = spoonerCam.IsActive();
			if (spoonerActive)
			{
				position = spoonerCam.GetPosition();
				rotation = spoonerCam.GetRotation();
				fov = spoonerCam.GetFieldOfView();
				return;
			}
			position = GameplayCamera::GetPosition();
			rotation = GameplayCamera::GetRotation();
			fov = GET_GAMEPLAY_CAM_FOV();
		}

		void DrawTrajectory(const CameraPath& path)
		{
			const std::vector<Vector3> points = path.Polyline(12);
			for (size_t i = 0; i + 1 < points.size(); ++i)
			{
				World::DrawLine(points[i], points[i + 1], RGBA(90, 190, 255, 170));
			}
			for (const CameraKey& key : path.keys)
			{
				World::DrawMarker(MarkerType::DebugSphere, key.position, Vector3(), Vector3(),
					Vector3(0.35f, 0.35f, 0.35f), RGBA(255, 190, 60, 200));
			}
		}
	}

	std::mutex& StateMutex() { return g_mutex; }
	PlayerState& State() { return g_state; }

	namespace { std::atomic<int> g_defaultEasing{ 0 }; }   // 0 = Linear

	int DefaultEasingIndex() { return g_defaultEasing; }
	void SetDefaultEasingIndex(int index)
	{
		if (index < 0) index = 0;
		if (index >= EasingCount()) index = EasingCount() - 1;
		g_defaultEasing = index;
	}

	bool IsWindowVisible() { return g_windowVisible; }

	void SetWindowVisible(bool visible)
	{
		g_windowVisible = visible;
		if (!visible)
			g_cursorMode = false;
		ImGuiSpooner::NotifyOverlayChanged();
	}

	void ToggleWindow() { SetWindowVisible(!g_windowVisible); }

	bool IsCursorMode() { return g_windowVisible && g_cursorMode; }
	void ToggleCursorMode() { if (g_windowVisible) g_cursorMode = !g_cursorMode; }

	void Release()
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		g_state.transport = Transport::Stopped;
		GiveBackView();
		g_state.pathCameraOwnsView = false;
	}

	void Tick()
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		PlayerState& state = g_state;

		Vector3 authorPosition, authorRotation;
		float authorFov = 50.0f;
		bool spoonerActive = false;
		ReadAuthoringCamera(authorPosition, authorRotation, authorFov, spoonerActive);
		state.spoonerCameraActive = spoonerActive;

		// ---- requests raised by the window or the HTTP bridge ----

		if (state.requestNew)
		{
			state.requestNew = false;
			state.path = CameraPath();
			state.time = 0.0f;
			state.selectedIds.clear();
			state.selectedKey = -1;
			state.transport = Transport::Stopped;
			GiveBackView();
			state.status = "new path";
		}

		if (state.requestAddKeyAtCamera)
		{
			state.requestAddKeyAtCamera = false;
			CameraKey key;
			// A new key lands one second after the last, which is a sane
			// default the user can then drag.
			key.time = state.path.Empty() ? 0.0f : state.path.Duration() + 1.0f;
			key.position = authorPosition;
			key.rotation = authorRotation;
			key.fov = authorFov;
			key.easing = EasingFromIndex(DefaultEasingIndex());
			state.path.AddKey(key);
			state.selectedKey = state.path.IndexOfId(state.path.keys.back().id);
			state.selectedIds = { state.path.keys[state.selectedKey].id };
			state.status = "key added at " + std::to_string(key.time) + "s";
		}

		if (state.requestUpdateKeyFromCamera)
		{
			state.requestUpdateKeyFromCamera = false;
			if (state.selectedKey >= 0 && state.selectedKey < static_cast<int>(state.path.keys.size()))
			{
				CameraKey& key = state.path.keys[state.selectedKey];
				key.position = authorPosition;
				key.rotation = authorRotation;
				key.fov = authorFov;
				state.path.Rebuild();
				state.status = "key " + std::to_string(state.selectedKey) + " moved to the camera";
			}
			else
			{
				state.status = "no key selected";
			}
		}

		if (!state.requestDeleteIds.empty())
		{
			const size_t count = state.requestDeleteIds.size();
			state.path.RemoveIds(state.requestDeleteIds);
			state.requestDeleteIds.clear();
			state.selectedIds.clear();
			state.selectedKey = -1;
			state.status = std::to_string(count) + " key(s) deleted";
		}

		if (state.requestDeleteKey >= 0)
		{
			const int index = state.requestDeleteKey;
			state.requestDeleteKey = -1;
			state.path.RemoveKey(static_cast<size_t>(index));
			state.selectedIds.clear();
			state.selectedKey = -1;
			state.status = "key " + std::to_string(index) + " deleted";
		}

		if (!state.requestLoadName.empty())
		{
			const std::string name = state.requestLoadName;
			state.requestLoadName.clear();
			std::string failure;
			if (Files::Load(name, state.path, failure))
			{
				state.time = 0.0f;
				state.selectedIds.clear();
				state.selectedKey = -1;
				state.transport = Transport::Stopped;
				state.status = "loaded " + name;
			}
			else
			{
				state.status = "load failed: " + failure;
			}
		}

		if (!state.requestSaveName.empty())
		{
			const std::string name = state.requestSaveName;
			state.requestSaveName.clear();
			std::string failure;
			state.path.name = name;
			state.status = Files::Save(name, state.path, failure)
				? "saved " + name
				: "save failed: " + failure;
			state.savedPathNamesStale = true;
		}

		if (state.savedPathNamesStale)
		{
			state.savedPathNamesStale = false;
			state.savedPathNames = Files::List();
		}

		if (state.requestStop)
		{
			state.requestStop = false;
			state.transport = Transport::Stopped;
			state.holdingLook = false;
			state.time = 0.0f;
			GiveBackView();
			state.status = "stopped";
		}

		if (state.requestLook)
		{
			state.requestLook = false;
			state.holdingLook = true;
			state.transport = Transport::Stopped;
			state.status = "looking";
			ApplyPose(state.lookPose);
		}

		if (state.requestPause)
		{
			state.requestPause = false;
			if (state.transport == Transport::Playing)
			{
				state.transport = Transport::Paused;
				state.status = "paused";
			}
		}

		if (state.requestPlay)
		{
			state.requestPlay = false;
			if (state.path.keys.size() < 2)
			{
				state.status = "need at least two keys to play";
			}
			else
			{
				if (state.transport == Transport::Stopped)
					state.time = 0.0f;
				state.holdingLook = false;
				state.transport = Transport::Playing;
				state.status = "playing";
			}
		}

		if (state.requestPreviewKey)
		{
			state.requestPreviewKey = false;
			if (state.selectedKey >= 0 && state.selectedKey < static_cast<int>(state.path.keys.size()))
			{
				const CameraKey& key = state.path.keys[state.selectedKey];
				state.time = key.time;
				state.holdingLook = false;
				state.transport = Transport::Paused;
				ApplyPose(CameraPose{ key.position, key.rotation, key.fov });
			}
		}

		if (state.requestSeek)
		{
			state.requestSeek = false;
			if (!state.path.Empty())
			{
				state.holdingLook = false;
				if (state.transport == Transport::Stopped)
					state.transport = Transport::Paused;
				ApplyPose(state.path.Evaluate(state.time));
			}
		}

		// ---- advance ----

		if (state.transport == Transport::Playing)
		{
			// Frame time rather than a wall clock, so the motion follows the
			// game's own sense of time through pauses and slow-motion.
			state.time += GET_FRAME_TIME() * state.speed;

			const float duration = state.path.Duration();
			if (!state.path.loop && state.time >= duration)
			{
				state.time = duration;
				state.transport = Transport::Paused;
				state.status = "finished";
			}
			ApplyPose(state.path.Evaluate(state.time));
		}
		else if (state.transport == Transport::Paused && g_cameraOwnsView)
		{
			// Hold the paused pose, otherwise the game drifts the camera back.
			ApplyPose(state.path.Evaluate(state.time));
		}
		else if (state.holdingLook)
		{
			ApplyPose(state.lookPose);
		}

		state.pathCameraOwnsView = g_cameraOwnsView;
		state.liveCameraPosition = authorPosition;
		state.liveCameraRotation = authorRotation;
		state.liveCameraFov = authorFov;

		// Never during playback: the line and the markers are in the world, so
		// they would be in the footage.
		const bool drawAids = g_windowVisible && state.showPath &&
			state.transport != Transport::Playing && !state.path.Empty();
		if (drawAids)
			DrawTrajectory(state.path);
	}
}
