/*
* Menyoo PC - Grand Theft Auto V single-player trainer mod
*/
#include "CameraPathUI.h"

#include "CameraPath.h"
#include "CameraPathPlayer.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

namespace sub::Spooner::CameraPathUI
{
	using namespace sub::Spooner::CameraPaths;

	namespace
	{
		char g_saveName[64] = "shot_01";
		constexpr float kTimelineHeight = 58.0f;
		constexpr float kKeyRadius = 6.0f;

		float TimeToX(float time, float duration, float left, float width)
		{
			if (duration <= 0.0001f)
				return left;
			return left + (time / duration) * width;
		}

		float XToTime(float x, float duration, float left, float width)
		{
			if (width <= 1.0f)
				return 0.0f;
			const float fraction = (x - left) / width;
			return std::max(0.0f, std::min(1.0f, fraction)) * duration;
		}

		// The timeline strip: keys as draggable handles, a playhead, and click
		// anywhere to scrub.
		void DrawTimeline(PlayerState& state)
		{
			// A path shorter than its own last key leaves no room to drag, so
			// the strip always shows a little past the end.
			const float duration = std::max(state.path.Duration(), 1.0f) * 1.05f;

			ImDrawList* draw = ImGui::GetWindowDrawList();
			const ImVec2 origin = ImGui::GetCursorScreenPos();
			const float width = ImGui::GetContentRegionAvail().x - 8.0f;
			const ImVec2 topLeft(origin.x, origin.y);
			const ImVec2 bottomRight(origin.x + width, origin.y + kTimelineHeight);

			ImGui::InvisibleButton("##timeline", ImVec2(width, kTimelineHeight));
			const bool hovered = ImGui::IsItemHovered();
			const bool held = ImGui::IsItemActive();

			draw->AddRectFilled(topLeft, bottomRight, IM_COL32(28, 30, 36, 255), 4.0f);
			draw->AddRect(topLeft, bottomRight, IM_COL32(70, 74, 84, 255), 4.0f);

			// second gridlines
			for (int second = 0; second <= static_cast<int>(duration); ++second)
			{
				const float x = TimeToX(static_cast<float>(second), duration, topLeft.x, width);
				draw->AddLine(ImVec2(x, bottomRight.y - 10.0f), ImVec2(x, bottomRight.y),
					IM_COL32(90, 95, 105, 255));
			}

			const float centreY = topLeft.y + kTimelineHeight * 0.42f;
			draw->AddLine(ImVec2(topLeft.x, centreY), ImVec2(bottomRight.x, centreY),
				IM_COL32(70, 74, 84, 255), 2.0f);

			// keys
			static int draggingKey = -1;
			for (int i = 0; i < static_cast<int>(state.path.keys.size()); ++i)
			{
				const CameraKey& key = state.path.keys[i];
				const float x = TimeToX(key.time, duration, topLeft.x, width);
				const bool selected = state.selectedKey == i;

				draw->AddCircleFilled(ImVec2(x, centreY), kKeyRadius,
					selected ? IM_COL32(255, 190, 60, 255) : IM_COL32(150, 200, 255, 255));
				draw->AddCircle(ImVec2(x, centreY), kKeyRadius, IM_COL32(20, 22, 26, 255), 0, 2.0f);

				if (!held && hovered && draggingKey < 0)
				{
					const ImVec2 mouse = ImGui::GetMousePos();
					if (std::fabs(mouse.x - x) <= kKeyRadius + 2.0f &&
						std::fabs(mouse.y - centreY) <= kKeyRadius + 4.0f &&
						ImGui::IsMouseClicked(ImGuiMouseButton_Left))
					{
						state.selectedKey = i;
						draggingKey = i;
					}
				}
			}

			if (draggingKey >= 0)
			{
				if (ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
					draggingKey < static_cast<int>(state.path.keys.size()))
				{
					state.path.keys[draggingKey].time =
						XToTime(ImGui::GetMousePos().x, duration, topLeft.x, width);
					state.path.Rebuild();
					// Sorting may have moved it, so follow the key by identity
					// rather than by index.
					state.selectedKey = draggingKey;
				}
				else
				{
					draggingKey = -1;
					state.path.Rebuild();
				}
			}
			else if (held)
			{
				state.time = XToTime(ImGui::GetMousePos().x, duration, topLeft.x, width);
				state.requestSeek = true;
			}

			// playhead
			const float playheadX = TimeToX(state.time, duration, topLeft.x, width);
			draw->AddLine(ImVec2(playheadX, topLeft.y + 2.0f), ImVec2(playheadX, bottomRight.y - 2.0f),
				IM_COL32(255, 90, 90, 255), 2.0f);

			ImGui::Dummy(ImVec2(0.0f, 2.0f));
		}

		void DrawSelectedKey(PlayerState& state)
		{
			if (state.selectedKey < 0 || state.selectedKey >= static_cast<int>(state.path.keys.size()))
			{
				ImGui::TextDisabled("No key selected. Click one on the timeline.");
				return;
			}

			CameraKey& key = state.path.keys[state.selectedKey];
			bool dirty = false;

			ImGui::Text("Key %d", state.selectedKey);
			ImGui::SameLine();
			if (ImGui::SmallButton("Set from camera"))
				state.requestUpdateKeyFromCamera = true;
			ImGui::SameLine();
			if (ImGui::SmallButton("Go to"))
				state.requestPreviewKey = true;
			ImGui::SameLine();
			if (ImGui::SmallButton("Delete"))
				state.requestDeleteKey = state.selectedKey;

			if (ImGui::DragFloat("Time (s)", &key.time, 0.02f, 0.0f, 3600.0f, "%.2f"))
				dirty = true;
			if (ImGui::DragFloat3("Position", &key.position.x, 0.05f))
				dirty = true;
			if (ImGui::DragFloat3("Rotation", &key.rotation.x, 0.25f))
				dirty = true;
			if (ImGui::DragFloat("FOV", &key.fov, 0.2f, 1.0f, 130.0f, "%.1f"))
				dirty = true;

			int easingIndex = static_cast<int>(key.easing);
			std::vector<const char*> names;
			names.reserve(EasingCount());
			for (int i = 0; i < EasingCount(); ++i)
				names.push_back(EasingName(EasingFromIndex(i)));
			if (ImGui::Combo("Easing", &easingIndex, names.data(), static_cast<int>(names.size())))
			{
				key.easing = EasingFromIndex(easingIndex);
				dirty = true;
			}
			ImGui::TextDisabled("Easing shapes the move that starts at this key.");

			if (dirty)
				state.path.Rebuild();
		}

		void DrawFiles(PlayerState& state)
		{
			ImGui::InputText("Name", g_saveName, sizeof(g_saveName));
			ImGui::SameLine();
			if (ImGui::Button("Save"))
				state.requestSaveName = g_saveName;

			if (state.savedPathNames.empty())
			{
				ImGui::TextDisabled("Nothing saved yet.");
				return;
			}

			if (ImGui::BeginListBox("##saved", ImVec2(-1.0f, 90.0f)))
			{
				for (const std::string& name : state.savedPathNames)
				{
					if (ImGui::Selectable(name.c_str()))
					{
						state.requestLoadName = name;
						std::snprintf(g_saveName, sizeof(g_saveName), "%s", name.c_str());
					}
				}
				ImGui::EndListBox();
			}
		}
	}

	void Draw()
	{
		if (!CameraPaths::IsWindowVisible())
			return;

		std::lock_guard<std::mutex> lock(CameraPaths::StateMutex());
		PlayerState& state = CameraPaths::State();

		ImGui::SetNextWindowSize(ImVec2(520.0f, 520.0f), ImGuiCond_FirstUseEver);
		bool open = true;
		if (!ImGui::Begin("Camera Path", &open))
		{
			ImGui::End();
			return;
		}
		if (!open)
			CameraPaths::SetWindowVisible(false);

		// transport
		if (ImGui::Button(state.transport == Transport::Playing ? "Pause" : "Play"))
		{
			if (state.transport == Transport::Playing) state.requestPause = true;
			else state.requestPlay = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("Stop")) state.requestStop = true;
		ImGui::SameLine();
		if (ImGui::Button("Add key at camera")) state.requestAddKeyAtCamera = true;
		ImGui::SameLine();
		if (ImGui::Button("New")) state.requestNew = true;

		ImGui::Checkbox("Loop", &state.path.loop);
		ImGui::SameLine();
		if (ImGui::Checkbox("Constant speed", &state.path.constantSpeed))
			state.path.Rebuild();
		ImGui::SameLine();
		ImGui::SetNextItemWidth(110.0f);
		ImGui::DragFloat("Speed", &state.speed, 0.01f, 0.05f, 8.0f, "%.2fx");

		ImGui::Separator();
		ImGui::Text("%d keys, %.2f s", static_cast<int>(state.path.keys.size()), state.path.Duration());
		ImGui::SameLine();
		ImGui::TextDisabled("| t = %.2f s", state.time);
		if (!state.spoonerCameraActive)
		{
			ImGui::SameLine();
			ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "| gameplay camera");
		}

		DrawTimeline(state);

		ImGui::Separator();
		if (ImGui::CollapsingHeader("Key", ImGuiTreeNodeFlags_DefaultOpen))
			DrawSelectedKey(state);

		if (ImGui::CollapsingHeader("Files"))
			DrawFiles(state);

		if (!state.status.empty())
		{
			ImGui::Separator();
			ImGui::TextDisabled("%s", state.status.c_str());
		}

		ImGui::End();
	}
}
