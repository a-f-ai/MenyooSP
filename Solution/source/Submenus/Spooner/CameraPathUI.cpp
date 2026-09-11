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
		float g_scaleFactor = 1.0f;
		constexpr float kTimelineHeight = 58.0f;
		constexpr float kKeyRadius = 6.0f;
		constexpr float kGrabPixels = 8.0f;

		// Dragged key, by id: an index would be wrong the moment a drag
		// carries a key past its neighbour and the vector is re-sorted.
		unsigned g_draggingId = 0;
		bool g_scrubbing = false;

		bool IsSelected(const PlayerState& state, unsigned id)
		{
			return std::find(state.selectedIds.begin(), state.selectedIds.end(), id) !=
				state.selectedIds.end();
		}

		void Select(PlayerState& state, unsigned id, bool additive)
		{
			if (!additive)
				state.selectedIds.clear();
			else
			{
				auto found = std::find(state.selectedIds.begin(), state.selectedIds.end(), id);
				if (found != state.selectedIds.end())
				{
					state.selectedIds.erase(found);
					state.selectedKey = -1;
					return;
				}
			}
			state.selectedIds.push_back(id);
			state.selectedKey = state.path.IndexOfId(id);
		}

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

		int KeyNearX(const PlayerState& state, float x, float duration, float left, float width)
		{
			int best = -1;
			float bestDistance = kGrabPixels;
			for (int i = 0; i < static_cast<int>(state.path.keys.size()); ++i)
			{
				const float keyX = TimeToX(state.path.keys[i].time, duration, left, width);
				const float distance = std::fabs(x - keyX);
				if (distance <= bestDistance)
				{
					bestDistance = distance;
					best = i;
				}
			}
			return best;
		}

		void DrawTimeline(PlayerState& state)
		{
			const float duration = std::max(state.path.Duration(), 1.0f) * 1.05f;

			ImDrawList* draw = ImGui::GetWindowDrawList();
			const ImVec2 origin = ImGui::GetCursorScreenPos();
			const float width = std::max(ImGui::GetContentRegionAvail().x - 8.0f, 32.0f);
			const ImVec2 topLeft(origin.x, origin.y);
			const ImVec2 bottomRight(origin.x + width, origin.y + kTimelineHeight);

			ImGui::InvisibleButton("##timeline", ImVec2(width, kTimelineHeight));

			draw->AddRectFilled(topLeft, bottomRight, IM_COL32(28, 30, 36, 255), 4.0f);
			draw->AddRect(topLeft, bottomRight, IM_COL32(70, 74, 84, 255), 4.0f);

			for (int second = 0; second <= static_cast<int>(duration); ++second)
			{
				const float x = TimeToX(static_cast<float>(second), duration, topLeft.x, width);
				draw->AddLine(ImVec2(x, bottomRight.y - 10.0f), ImVec2(x, bottomRight.y),
					IM_COL32(90, 95, 105, 255));
			}

			const float centreY = topLeft.y + kTimelineHeight * 0.42f;
			draw->AddLine(ImVec2(topLeft.x, centreY), ImVec2(bottomRight.x, centreY),
				IM_COL32(70, 74, 84, 255), 2.0f);

			// The press has to be classified the moment it happens. Asking
			// IsItemActive afterwards is too late: the button is already active
			// on the click frame, which is why clicking a key used to scrub
			// instead of selecting.
			if (ImGui::IsItemActivated())
			{
				const float mouseX = ImGui::GetMousePos().x;
				const int hit = KeyNearX(state, mouseX, duration, topLeft.x, width);
				if (hit >= 0)
				{
					const ImGuiIO& io = ImGui::GetIO();
					Select(state, state.path.keys[hit].id, io.KeyCtrl || io.KeySuper);
					g_draggingId = state.path.keys[hit].id;
					g_scrubbing = false;
				}
				else
				{
					g_draggingId = 0;
					g_scrubbing = true;
				}
			}

			if (ImGui::IsItemActive())
			{
				const float mouseX = ImGui::GetMousePos().x;
				if (g_draggingId != 0)
				{
					const int index = state.path.IndexOfId(g_draggingId);
					if (index >= 0)
					{
						state.path.keys[static_cast<size_t>(index)].time =
							XToTime(mouseX, duration, topLeft.x, width);
						state.path.Rebuild();
						state.selectedKey = state.path.IndexOfId(g_draggingId);
					}
				}
				else if (g_scrubbing)
				{
					state.time = XToTime(mouseX, duration, topLeft.x, width);
					state.requestSeek = true;
				}
			}

			if (ImGui::IsItemDeactivated())
			{
				g_draggingId = 0;
				g_scrubbing = false;
				state.path.Rebuild();
			}

			// Drawn after the hit test so the marks sit on top of the strip.
			for (int i = 0; i < static_cast<int>(state.path.keys.size()); ++i)
			{
				const CameraKey& key = state.path.keys[i];
				const float x = TimeToX(key.time, duration, topLeft.x, width);
				const bool selected = IsSelected(state, key.id);

				// A pause is two keys with the same pose; joining them makes it
				// visible as a bar rather than two marks that look like a slip.
				if (i + 1 < static_cast<int>(state.path.keys.size()))
				{
					const CameraKey& next = state.path.keys[i + 1];
					const bool samePlace =
						std::fabs(key.position.x - next.position.x) < 1e-3f &&
						std::fabs(key.position.y - next.position.y) < 1e-3f &&
						std::fabs(key.position.z - next.position.z) < 1e-3f;
					if (samePlace)
					{
						const float nextX = TimeToX(next.time, duration, topLeft.x, width);
						draw->AddRectFilled(ImVec2(x, centreY - 3.0f), ImVec2(nextX, centreY + 3.0f),
							IM_COL32(120, 210, 140, 220), 2.0f);
					}
				}

				draw->AddCircleFilled(ImVec2(x, centreY), kKeyRadius,
					selected ? IM_COL32(255, 190, 60, 255) : IM_COL32(150, 200, 255, 255));
				draw->AddCircle(ImVec2(x, centreY), kKeyRadius, IM_COL32(20, 22, 26, 255), 0, 2.0f);
			}

			const float playheadX = TimeToX(state.time, duration, topLeft.x, width);
			draw->AddLine(ImVec2(playheadX, topLeft.y + 2.0f), ImVec2(playheadX, bottomRight.y - 2.0f),
				IM_COL32(255, 90, 90, 255), 2.0f);

			ImGui::Dummy(ImVec2(0.0f, 2.0f));
		}

		void DrawSelection(PlayerState& state)
		{
			const int count = static_cast<int>(state.selectedIds.size());
			ImGui::Text("%d selected", count);
			ImGui::SameLine();
			if (ImGui::SmallButton("All"))
			{
				state.selectedIds.clear();
				for (const CameraKey& key : state.path.keys)
					state.selectedIds.push_back(key.id);
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("None"))
			{
				state.selectedIds.clear();
				state.selectedKey = -1;
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("Copy") && count > 0)
				state.clipboard = state.path.CopyKeys(state.selectedIds);
			ImGui::SameLine();
			if (ImGui::SmallButton("Paste") && !state.clipboard.empty())
			{
				// Lands at the playhead, pushing later keys back so nothing is
				// overwritten.
				state.selectedIds = state.path.InsertKeys(state.clipboard, state.time, true);
				state.selectedKey = state.selectedIds.empty()
					? -1 : state.path.IndexOfId(state.selectedIds.front());
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("Delete") && count > 0)
			{
				state.path.RemoveIds(state.selectedIds);
				state.selectedIds.clear();
				state.selectedKey = -1;
			}

			if (count >= 2)
			{
				ImGui::SetNextItemWidth(110.0f);
				ImGui::DragFloat("##scale", &g_scaleFactor, 0.01f, 0.05f, 10.0f, "%.2fx");
				ImGui::SameLine();
				if (ImGui::Button("Scale selected"))
				{
					// About the earliest selected key, so the selection stretches
					// forward rather than sliding around.
					float anchor = state.path.Duration();
					for (unsigned id : state.selectedIds)
					{
						const int index = state.path.IndexOfId(id);
						if (index >= 0)
							anchor = std::min(anchor, state.path.keys[static_cast<size_t>(index)].time);
					}
					state.path.ScaleTimes(state.selectedIds, g_scaleFactor, anchor);
				}
				ImGui::SameLine();
				ImGui::TextDisabled("stretch the selected keys in time");
			}
		}

		void DrawSelectedKey(PlayerState& state)
		{
			if (state.selectedKey < 0 || state.selectedKey >= static_cast<int>(state.path.keys.size()))
			{
				ImGui::TextDisabled("No key selected. Click one on the timeline.");
				return;
			}

			CameraKey& key = state.path.keys[state.selectedKey];
			const unsigned keyId = key.id;
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

			ImGui::SetNextItemWidth(90.0f);
			ImGui::DragFloat("##pause", &state.pauseSeconds, 0.05f, 0.05f, 60.0f, "%.2f s");
			ImGui::SameLine();
			if (ImGui::Button("Hold here"))
			{
				// A twin of this key further along: identical poses evaluate to
				// a still camera, so the move pauses instead of easing through.
				const unsigned added = state.path.InsertPause(keyId, state.pauseSeconds);
				if (added != 0)
				{
					state.selectedIds = { added };
					state.selectedKey = state.path.IndexOfId(added);
				}
			}
			ImGui::SameLine();
			ImGui::TextDisabled("pause the camera at this key");

			if (ImGui::DragFloat("Time (s)", &key.time, 0.02f, 0.0f, 3600.0f, "%.2f"))
				dirty = true;
			if (ImGui::DragFloat3("Position", &key.position.x, 0.05f))
				dirty = true;
			if (ImGui::DragFloat3("Rotation", &key.rotation.x, 0.25f))
				dirty = true;
			if (ImGui::DragFloat("FOV", &key.fov, 0.2f, 1.0f, 130.0f, "%.1f"))
				dirty = true;

			if (state.path.smoothing == Smoothing::PerKey)
			{
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
				ImGui::TextDisabled("Shapes the move that starts at this key.");
			}
			else
			{
				ImGui::TextDisabled("Per-key easing is off while smoothing is Whole path.");
			}

			if (dirty)
			{
				state.path.Rebuild();
				state.selectedKey = state.path.IndexOfId(keyId);
			}
		}

		void DrawSmoothing(PlayerState& state)
		{
			// Two one-click fixes for the usual complaints, both of which leave
			// every key exactly as controllable as before.
			if (ImGui::Button("Even speed"))
				state.path.RetimeByArcLength();
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Retime the keys by distance, so the camera stops "
					"racing through long legs and crawling through short ones.\n"
					"Start and total length are kept.");
			ImGui::SameLine();
			if (ImGui::Button("Ease ends"))
				state.path.EaseEnds();
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Accelerate out of the first key, settle into the last, "
					"steady in between.\nThis is the smoothing you usually want.");

			ImGui::SameLine();
			bool global = state.path.smoothing == Smoothing::WholePath;
			if (ImGui::Checkbox("Global smoothing", &global))
				state.path.smoothing = global ? Smoothing::WholePath : Smoothing::PerKey;
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("One curve over the whole path instead of per-key easing.\n"
					"Smooth, but it takes local control of pacing away.\n"
					"Off by default; \"Ease ends\" usually does what you want.");

			if (state.path.smoothing == Smoothing::WholePath)
			{
				int easingIndex = static_cast<int>(state.path.pathEasing);
				std::vector<const char*> names;
				names.reserve(EasingCount());
				for (int i = 0; i < EasingCount(); ++i)
					names.push_back(EasingName(EasingFromIndex(i)));
				ImGui::SetNextItemWidth(150.0f);
				if (ImGui::Combo("Curve", &easingIndex, names.data(), static_cast<int>(names.size())))
					state.path.pathEasing = EasingFromIndex(easingIndex);
				ImGui::SameLine();
				ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.35f, 1.0f),
					"one curve over everything, per-key easing ignored");
			}
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

		ImGui::SetNextWindowSize(ImVec2(560.0f, 560.0f), ImGuiCond_FirstUseEver);
		bool open = true;
		if (!ImGui::Begin("Camera Path", &open))
		{
			ImGui::End();
			return;
		}
		if (!open)
			CameraPaths::SetWindowVisible(false);

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

		ImGui::Checkbox("Show path", &state.showPath);
		ImGui::SameLine();
		ImGui::Checkbox("Loop", &state.path.loop);
		ImGui::SameLine();
		if (ImGui::Checkbox("Constant speed", &state.path.constantSpeed))
			state.path.Rebuild();
		ImGui::SameLine();
		ImGui::SetNextItemWidth(100.0f);
		ImGui::DragFloat("Speed", &state.speed, 0.01f, 0.05f, 8.0f, "%.2fx");

		DrawSmoothing(state);

		if (CameraPaths::IsCursorMode())
			ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.5f, 1.0f),
				"Cursor mode: F7 gives the camera back");
		else
			ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.35f, 1.0f),
				"Camera is yours. F7 to use the mouse here");
		ImGui::TextDisabled("]  add key   [ or Space  play/pause   \\  stop   F10 hide");

		ImGui::Separator();
		ImGui::Text("%d keys, %.2f s", static_cast<int>(state.path.keys.size()), state.path.Duration());
		ImGui::SameLine();
		ImGui::TextDisabled("| t = %.2f s", state.time);
		if (!state.spoonerCameraActive)
		{
			ImGui::SameLine();
			ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f),
				"| gameplay camera - press F9 to fly a free camera");
		}

		DrawTimeline(state);

		// Retiming the whole move at once, which is what "it is too fast"
		// usually needs.
		float total = state.path.Duration();
		ImGui::SetNextItemWidth(110.0f);
		if (ImGui::DragFloat("Total length (s)", &total, 0.05f, 0.1f, 3600.0f, "%.2f"))
			state.path.SetTotalDuration(total);

		ImGui::Separator();
		DrawSelection(state);

		ImGui::Separator();
		if (ImGui::CollapsingHeader("Key", ImGuiTreeNodeFlags_DefaultOpen))
			DrawSelectedKey(state);

		if (ImGui::CollapsingHeader("Files"))
			DrawFiles(state);

		if (state.pathCameraOwnsView && state.transport != Transport::Playing)
		{
			ImGui::Separator();
			ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.4f, 1.0f),
				"The path camera has the view. Stop to give it back.");
		}

		if (!state.status.empty())
		{
			ImGui::Separator();
			ImGui::TextDisabled("%s", state.status.c_str());
		}

		ImGui::End();
	}
}
