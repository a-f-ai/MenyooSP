/*
* Menyoo PC - Grand Theft Auto V single-player trainer mod
*/
#include "CharacterPicker.h"

#include "ImGuiSpooner.h"
#include "SpoonerMode.h"
#include "../../macros.h"
#include "../../Natives/natives2.h"
#include "../../Scripting/Camera.h"
#include "../../Scripting/GameplayCamera.h"
#include "../../Scripting/GTAentity.h"
#include "../../Scripting/Raycast.h"
#include "../../Scripting/World.h"
#include "../../Util/ExePath.h"
#include "../../Util/FileLogger.h"
#include "../../Util/GTAmath.h"
#include "../../Util/keyboard.h"
#include "../../Util/StringManip.h"
#include "../../Http/EntityApi.h"

#include "imgui.h"
#include <json/single_include/nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace sub::Spooner::CharacterPicker
{
	namespace
	{
		std::mutex g_mutex;
		State g_state;
		std::atomic<bool> g_windowVisible{ false };
		std::atomic<bool> g_cursorMode{ false };
		char g_filter[64] = "";

		// A standing ped's origin sits this far above the surface: measured on
		// 686 of the author's placed peds (tools/calibrate_stand_offsets.py).
		constexpr float kPedStandHeight = 1.0f;
		constexpr float kAimDistance = 250.0f;

		std::string Lower(std::string s)
		{
			std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return s;
		}

		// ---- the list ----

		void Load(State& state)
		{
			state.characters.clear();
			state.loaded = true;
			state.loadError.clear();
			state.selectedCharacter = -1;

			const std::string path = GetPathffA(Pathff::Main, true) + "Characters.json";
			std::ifstream file(path, std::ios::binary);
			if (!file)
			{
				state.loadError = "cannot open " + path + " - run tools/palette_to_pedlist.py";
				return;
			}
			std::stringstream buffer;
			buffer << file.rdbuf();

			// No exceptions: a broken file must leave a message, not kill the script.
			const json doc = json::parse(buffer.str(), nullptr, false);
			if (doc.is_discarded() || !doc.is_object() || !doc.contains("characters") || !doc["characters"].is_array())
			{
				state.loadError = "Characters.json is not the expected JSON (an object with a \"characters\" array)";
				return;
			}

			for (const json& c : doc["characters"])
			{
				Character character;
				character.key = c.value("key", "");
				character.label = c.value("label", character.key);
				if (c.contains("aliases") && c["aliases"].is_array())
					for (const json& a : c["aliases"]) if (a.is_string()) character.aliases.push_back(a.get<std::string>());
				if (!c.contains("variants") || !c["variants"].is_array())
					continue;
				for (const json& v : c["variants"])
				{
					const std::string hex = v.value("hash", "");
					if (hex.size() < 3) continue;
					Variant variant;
					variant.model = v.value("model", "");
					variant.hash = std::strtoul(hex.c_str(), nullptr, 16);
					variant.color = v.value("color", "");
					variant.note = v.value("note", "");
					character.variants.push_back(std::move(variant));
				}
				if (!character.variants.empty())
					state.characters.push_back(std::move(character));
			}
			std::sort(state.characters.begin(), state.characters.end(),
				[](const Character& a, const Character& b) { return Lower(a.label) < Lower(b.label); });
			if (!state.characters.empty())
				state.selectedCharacter = 0;
			addlog(ige::LogType::LOG_INFO, "CharacterPicker: " + std::to_string(state.characters.size()) + " characters from " + path);
		}

		// ---- placing (script thread) ----

		struct Aim
		{
			bool ok = false;
			std::string problem;
			Vector3 point;
			Vector3 cameraPosition;
			float cameraYaw = 0.0f;
		};

		void CameraPose(Vector3& position, Vector3& direction, float& yaw)
		{
			auto& spoonerCam = SpoonerMode::spoonerModeCamera;
			if (spoonerCam.IsActive())
			{
				position = spoonerCam.GetPosition();
				direction = spoonerCam.GetDirection();
				yaw = spoonerCam.GetRotation().z;
				return;
			}
			position = GameplayCamera::GetPosition();
			direction = GameplayCamera::GetDirection();
			yaw = GameplayCamera::GetRotation().z;
		}

		Aim AimAt(bool useMouse)
		{
			Aim aim;
			Vector3 direction;
			CameraPose(aim.cameraPosition, direction, aim.cameraYaw);

			Vector3 from = aim.cameraPosition;
			Vector3 to = aim.cameraPosition + direction * kAimDistance;
			if (useMouse)
			{
				// The game hands out the ray under its cursor; only the endpoints are
				// wanted, the probe itself is run synchronously below.
				Vector3_t a{}, b{};
				START_SHAPE_TEST_MOUSE_CURSOR_LOS_PROBE(&a, &b, -1, 0, 7);
				const Vector3 start(a), end(b);
				if ((end - start).Length() < 0.01f)
				{
					aim.problem = "the game gave no cursor ray this frame; use Enter to place at the screen centre";
					return aim;
				}
				from = start;
				to = end;
			}

			const RaycastResult hit = RaycastResult::Raycast(from, to, IntersectOptions::Everything);
			if (hit.Result() != 2)
			{
				aim.problem = "the probe did not resolve (status " + std::to_string(hit.Result()) + ")";
				return aim;
			}
			if (!hit.DidHitAnything())
			{
				aim.problem = "nothing under the aim within " + std::to_string(static_cast<int>(kAimDistance)) + " m";
				return aim;
			}
			aim.ok = true;
			aim.point = hit.HitCoords();
			return aim;
		}

		struct PlaceJob
		{
			Character character;
			int variantIndex;
			int count;
			float spacing;
			Facing facing;
			float jitter;
			bool rainbow;
			bool armsWaving;
			bool useMouse;
		};

		float YawToward(const Vector3& from, const Vector3& to)
		{
			// Game heading: 0 looks along +y, 90 along -x.
			return static_cast<float>(std::atan2(-(to.x - from.x), to.y - from.y) * 180.0 / MATH_PI);
		}

		void RunPlace(const PlaceJob& job)
		{
			const Aim aim = AimAt(job.useMouse);
			if (!aim.ok)
			{
				std::lock_guard<std::mutex> lock(g_mutex);
				g_state.status = aim.problem;
				return;
			}

			const float yawRad = static_cast<float>(aim.cameraYaw * MATH_PI / 180.0);
			const Vector3 right(std::cos(yawRad), std::sin(yawRad), 0.0f);
			std::mt19937 rng(static_cast<unsigned>(GetTickCount()));
			std::uniform_real_distribution<float> jitter(-job.jitter, job.jitter);

			std::vector<int> placed;
			std::vector<std::string> problems;
			for (int i = 0; i < job.count; ++i)
			{
				const Variant& variant = job.rainbow
					? job.character.variants[(job.variantIndex + i) % job.character.variants.size()]
					: job.character.variants[job.variantIndex];

				const float t = (static_cast<float>(i) - static_cast<float>(job.count - 1) / 2.0f) * job.spacing;
				Vector3 spot = aim.point + right * t;

				// Each spot finds its own surface: a row can run off a plank.
				Vector3 probe = spot;
				probe.z += 1.5f;
				const float ground = World::GetGroundHeight(probe);
				if (ground <= -1000.0f || ground >= 10000.0f || std::fabs(ground - aim.point.z) > 2.0f)
				{
					problems.push_back("#" + std::to_string(i + 1) + ": no surface at that spot");
					continue;
				}
				spot.z = ground + kPedStandHeight;

				float yaw = aim.cameraYaw;
				if (job.facing == Facing::TowardCamera) yaw = YawToward(spot, aim.cameraPosition);
				if (job.facing == Facing::AwayFromCamera) yaw = YawToward(aim.cameraPosition, spot);
				yaw += jitter(rng);

				Http::EntityApi::CreateRequest request{};
				request.type = 1;
				request.model = variant.hash;
				request.modelLabel = variant.model;
				request.name = job.character.label + (variant.color.empty() ? "" : " - " + variant.color);
				request.position = { spot.x, spot.y, spot.z };
				request.rotation = { 0.0f, 0.0f, yaw };
				request.dynamic = true;
				request.snapToGround = false;
				request.still = true;
				request.tolerance = 0.25f;
				if (job.armsWaving)
				{
					request.animDict = "random@car_thief@victimpoints_ig_3";
					request.animName = "arms_waving";
				}

				int id = 0;
				std::string failure;
				if (Http::EntityApi::CreateDirect(request, id, failure))
					placed.push_back(id);
				else
					problems.push_back("#" + std::to_string(i + 1) + " " + variant.model + ": " + failure);
			}

			std::lock_guard<std::mutex> lock(g_mutex);
			if (!placed.empty())
			{
				g_state.lastPlaced = placed;
				g_state.placedTotal += static_cast<int>(placed.size());
			}
			g_state.status = "placed " + std::to_string(placed.size()) + " x " + job.character.label;
			if (!problems.empty())
			{
				g_state.status += "; " + std::to_string(problems.size()) + " not placed: " + problems.front();
				for (const auto& p : problems) addlog(ige::LogType::LOG_WARNING, "CharacterPicker: " + p);
			}
		}

		void RunUndo(std::vector<int> ids)
		{
			int removed = 0;
			for (int id : ids)
			{
				GTAentity entity(id);
				if (!entity.Exists()) continue;
				Http::EntityApi::DeleteEntity(id);
				++removed;
			}
			std::lock_guard<std::mutex> lock(g_mutex);
			g_state.lastPlaced.clear();
			g_state.status = "removed " + std::to_string(removed);
		}

		void Keyboard(State& state)
		{
			if (!IsCursorMode() || ImGuiSpooner::WantsTextInput() || state.characters.empty())
				return;
			const int n = static_cast<int>(state.characters.size());
			if (IsKeyJustUp(VirtualKey::Down)) { state.selectedCharacter = (state.selectedCharacter + 1) % n; state.selectedVariant = 0; }
			if (IsKeyJustUp(VirtualKey::Up))   { state.selectedCharacter = (state.selectedCharacter + n - 1) % n; state.selectedVariant = 0; }
			if (state.selectedCharacter >= 0)
			{
				const int v = static_cast<int>(state.characters[state.selectedCharacter].variants.size());
				if (IsKeyJustUp(VirtualKey::Right)) state.selectedVariant = (state.selectedVariant + 1) % v;
				if (IsKeyJustUp(VirtualKey::Left))  state.selectedVariant = (state.selectedVariant + v - 1) % v;
			}
			if (IsKeyJustUp(VirtualKey::Return)) state.requestPlaceAtCentre = true;
		}

		// ---- window (render thread) ----

		ImVec4 ColourFor(const std::string& name)
		{
			const std::string n = Lower(name);
			struct Swatch { const char* key; ImVec4 rgb; };
			static const Swatch swatches[] = {
				{ "light blue", ImVec4(0.45f, 0.75f, 1.0f, 1.0f) }, { "dark blue", ImVec4(0.10f, 0.15f, 0.50f, 1.0f) },
				{ "blue-black", ImVec4(0.10f, 0.12f, 0.30f, 1.0f) }, { "blue", ImVec4(0.20f, 0.40f, 0.90f, 1.0f) },
				{ "lime", ImVec4(0.60f, 0.90f, 0.20f, 1.0f) }, { "olive", ImVec4(0.50f, 0.55f, 0.20f, 1.0f) },
				{ "green", ImVec4(0.20f, 0.70f, 0.30f, 1.0f) }, { "teal", ImVec4(0.20f, 0.70f, 0.70f, 1.0f) },
				{ "orange-pink", ImVec4(0.95f, 0.50f, 0.45f, 1.0f) }, { "orange-tan", ImVec4(0.85f, 0.60f, 0.30f, 1.0f) },
				{ "brown-yellow", ImVec4(0.65f, 0.50f, 0.20f, 1.0f) }, { "orange", ImVec4(0.95f, 0.55f, 0.15f, 1.0f) },
				{ "yellow", ImVec4(0.90f, 0.85f, 0.20f, 1.0f) }, { "red", ImVec4(0.85f, 0.20f, 0.20f, 1.0f) },
				{ "pink", ImVec4(0.95f, 0.45f, 0.70f, 1.0f) }, { "magenta", ImVec4(0.85f, 0.20f, 0.75f, 1.0f) },
				{ "purple", ImVec4(0.60f, 0.30f, 0.80f, 1.0f) }, { "white", ImVec4(0.90f, 0.90f, 0.90f, 1.0f) },
				{ "black", ImVec4(0.15f, 0.15f, 0.15f, 1.0f) }, { "gradient", ImVec4(0.55f, 0.45f, 0.75f, 1.0f) },
			};
			for (const auto& s : swatches)
				if (n.find(s.key) != std::string::npos) return s.rgb;
			return ImVec4(0.35f, 0.35f, 0.38f, 1.0f);
		}

		bool Matches(const Character& c, const std::string& needle)
		{
			if (needle.empty()) return true;
			if (Lower(c.label).find(needle) != std::string::npos || Lower(c.key).find(needle) != std::string::npos) return true;
			for (const auto& a : c.aliases) if (Lower(a).find(needle) != std::string::npos) return true;
			for (const auto& v : c.variants) if (Lower(v.model).find(needle) != std::string::npos) return true;
			return false;
		}
	}

	std::mutex& StateMutex() { return g_mutex; }
	State& GetState() { return g_state; }

	bool IsWindowVisible() { return g_windowVisible; }
	void SetWindowVisible(bool visible)
	{
		g_windowVisible = visible;
		if (!visible) g_cursorMode = false;
		ImGuiSpooner::NotifyOverlayChanged();
	}
	void ToggleWindow() { SetWindowVisible(!g_windowVisible); }
	bool IsCursorMode() { return g_windowVisible && g_cursorMode; }
	void ToggleCursorMode() { if (g_windowVisible) g_cursorMode = !g_cursorMode; }

	void Tick()
	{
		if (!g_windowVisible && g_state.loaded)
			return;

		PlaceJob job{};
		bool doPlace = false;
		std::vector<int> undo;
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			if (!g_state.loaded || g_state.requestReload)
			{
				g_state.requestReload = false;
				Load(g_state);
			}
			Keyboard(g_state);

			if (g_state.requestUndo)
			{
				g_state.requestUndo = false;
				undo = g_state.lastPlaced;
			}
			const bool wantPlace = g_state.requestPlaceAtCentre || g_state.requestPlaceAtMouse;
			if (wantPlace && g_state.selectedCharacter >= 0 && g_state.selectedCharacter < static_cast<int>(g_state.characters.size()))
			{
				const Character& c = g_state.characters[g_state.selectedCharacter];
				job = PlaceJob{ c, std::clamp(g_state.selectedVariant, 0, static_cast<int>(c.variants.size()) - 1),
					std::clamp(g_state.count, 1, 40), g_state.spacing, g_state.facing, g_state.jitter,
					g_state.rainbow, g_state.armsWaving, g_state.requestPlaceAtMouse };
				doPlace = true;
			}
			g_state.requestPlaceAtCentre = false;
			g_state.requestPlaceAtMouse = false;
		}
		// Spawning streams models and yields frames; the lock is not held for it,
		// so the window keeps drawing.
		if (!undo.empty()) RunUndo(undo);
		if (doPlace) RunPlace(job);
	}

	void Draw()
	{
		if (!g_windowVisible)
			return;
		bool open = true;
		ImGui::SetNextWindowSize(ImVec2(620.0f, 520.0f), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin("Characters", &open))
		{
			ImGui::End();
			if (!open) SetWindowVisible(false);
			return;
		}

		std::lock_guard<std::mutex> lock(g_mutex);
		State& state = g_state;

		if (!state.loadError.empty())
			ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", state.loadError.c_str());
		ImGui::TextDisabled("F7 mouse mode  |  click in the world = place under the cursor  |  Enter = place at screen centre  |  Up/Down character, Left/Right colour");

		ImGui::SetNextItemWidth(220.0f);
		ImGui::InputTextWithHint("##filter", "filter: pomni, spider, hulk...", g_filter, sizeof(g_filter));
		ImGui::SameLine();
		if (ImGui::Button("Reload list")) state.requestReload = true;
		ImGui::SameLine();
		ImGui::TextDisabled("%d characters", static_cast<int>(state.characters.size()));

		const std::string needle = Lower(g_filter);

		ImGui::BeginChild("##list", ImVec2(240.0f, -92.0f), true);
		for (int i = 0; i < static_cast<int>(state.characters.size()); ++i)
		{
			const Character& c = state.characters[i];
			if (!Matches(c, needle)) continue;
			char row[128];
			std::snprintf(row, sizeof(row), "%s  (%d)##%d", c.label.c_str(), static_cast<int>(c.variants.size()), i);
			if (ImGui::Selectable(row, state.selectedCharacter == i))
			{
				state.selectedCharacter = i;
				state.selectedVariant = 0;
			}
		}
		ImGui::EndChild();

		ImGui::SameLine();
		ImGui::BeginChild("##right", ImVec2(0.0f, -92.0f), true);
		if (state.selectedCharacter >= 0 && state.selectedCharacter < static_cast<int>(state.characters.size()))
		{
			const Character& c = state.characters[state.selectedCharacter];
			ImGui::Text("%s", c.label.c_str());
			ImGui::Separator();
			const float avail = ImGui::GetContentRegionAvail().x;
			float used = 0.0f;
			for (int v = 0; v < static_cast<int>(c.variants.size()); ++v)
			{
				const Variant& variant = c.variants[v];
				const std::string caption = variant.color.empty() ? variant.model : variant.color;
				const ImVec4 rgb = ColourFor(variant.color.empty() ? variant.note : variant.color);
				const bool selected = state.selectedVariant == v;
				const float luminance = 0.3f * rgb.x + 0.59f * rgb.y + 0.11f * rgb.z;
				ImGui::PushStyleColor(ImGuiCol_Button, rgb);
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(rgb.x * 1.15f, rgb.y * 1.15f, rgb.z * 1.15f, 1.0f));
				ImGui::PushStyleColor(ImGuiCol_Text, luminance > 0.6f ? ImVec4(0, 0, 0, 1) : ImVec4(1, 1, 1, 1));
				ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, selected ? 3.0f : 0.0f);
				const float width = std::max(72.0f, ImGui::CalcTextSize(caption.c_str()).x + 18.0f);
				if (used > 0.0f && used + width > avail) { used = 0.0f; }
				else if (used > 0.0f) ImGui::SameLine();
				char id[160];
				std::snprintf(id, sizeof(id), "%s##v%d", caption.c_str(), v);
				if (ImGui::Button(id, ImVec2(width, 30.0f)))
					state.selectedVariant = v;
				used += width + 8.0f;
				ImGui::PopStyleVar();
				ImGui::PopStyleColor(3);
				if (ImGui::IsItemHovered() && !variant.note.empty())
					ImGui::SetTooltip("%s\n%s", variant.model.c_str(), variant.note.c_str());
			}
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::SetNextItemWidth(160.0f); ImGui::SliderInt("count", &state.count, 1, 40);
			ImGui::SetNextItemWidth(160.0f); ImGui::SliderFloat("spacing m", &state.spacing, 0.5f, 3.0f, "%.2f");
			ImGui::SetNextItemWidth(160.0f); ImGui::SliderFloat("yaw jitter", &state.jitter, 0.0f, 45.0f, "%.0f deg");
			int facing = static_cast<int>(state.facing);
			ImGui::RadioButton("face camera", &facing, 0); ImGui::SameLine();
			ImGui::RadioButton("face away", &facing, 1); ImGui::SameLine();
			ImGui::RadioButton("as camera", &facing, 2);
			state.facing = static_cast<Facing>(facing);
			ImGui::Checkbox("rainbow: cycle colours along the row", &state.rainbow);
			ImGui::Checkbox("arms waving (the author's look)", &state.armsWaving);
		}
		else
		{
			ImGui::TextDisabled("pick a character on the left");
		}
		ImGui::EndChild();

		if (ImGui::Button("Place at screen centre (Enter)", ImVec2(260.0f, 32.0f))) state.requestPlaceAtCentre = true;
		ImGui::SameLine();
		if (ImGui::Button("Undo last group", ImVec2(160.0f, 32.0f))) state.requestUndo = true;
		ImGui::SameLine();
		ImGui::TextDisabled("placed %d", state.placedTotal);
		if (!state.status.empty())
			ImGui::TextWrapped("%s", state.status.c_str());

		// A click in the world, not on the window, while the window owns the mouse.
		if (IsCursorMode() && !ImGui::GetIO().WantCaptureMouse && ImGui::GetIO().MouseClicked[0])
			state.requestPlaceAtMouse = true;

		ImGui::End();
		if (!open) SetWindowVisible(false);
	}
}
