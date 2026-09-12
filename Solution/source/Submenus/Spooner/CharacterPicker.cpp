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
#include "../../Scripting/Model.h"
#include "../../Scripting/Raycast.h"
#include "../../Scripting/World.h"
#include "../../Util/ExePath.h"
#include "../../Util/FileLogger.h"
#include "../../Util/GTAmath.h"
#include "../../Util/keyboard.h"
#include "../../Util/StringManip.h"
#include "../../Http/EntityApi.h"
#include "../../Http/ModelApi.h"

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
		char g_propName[64] = "";

		// A standing ped's origin sits this far above the surface: measured on
		// 686 of the author's placed peds (tools/calibrate_stand_offsets.py).
		constexpr float kPedStandHeight = 1.0f;
		constexpr float kAimDistance = 300.0f;
		constexpr int kPedComponents = 12;

		std::string Lower(std::string s)
		{
			std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return s;
		}

		Vector3 ToVec(const Point3& p) { return Vector3(p.x, p.y, p.z); }
		Point3 ToPoint(const Vector3& v) { Point3 p; p.x = v.x; p.y = v.y; p.z = v.z; return p; }

		// ---- the lists ----

		bool ReadFile(const std::string& path, std::string& out)
		{
			std::ifstream file(path, std::ios::binary);
			if (!file) return false;
			std::stringstream buffer;
			buffer << file.rdbuf();
			out = buffer.str();
			return true;
		}

		void Load(State& state)
		{
			state.characters.clear();
			state.props.clear();
			state.loaded = true;
			state.loadError.clear();
			state.selectedCharacter = -1;
			state.selectedProp = -1;

			const std::string dir = GetPathffA(Pathff::Main, true);
			std::string text;
			if (!ReadFile(dir + "Characters.json", text))
				state.loadError = "cannot open " + dir + "Characters.json - run tools/palette_to_pedlist.py";
			else
			{
				// No exceptions: a broken file must leave a message, not kill the script.
				const json doc = json::parse(text, nullptr, false);
				if (doc.is_discarded() || !doc.is_object() || !doc.contains("characters") || !doc["characters"].is_array())
					state.loadError = "Characters.json is not the expected JSON (an object with a \"characters\" array)";
				else
				{
					for (const json& c : doc["characters"])
					{
						Character character;
						character.key = c.value("key", "");
						character.label = c.value("label", character.key);
						if (c.contains("aliases") && c["aliases"].is_array())
							for (const json& a : c["aliases"]) if (a.is_string()) character.aliases.push_back(a.get<std::string>());
						if (!c.contains("variants") || !c["variants"].is_array()) continue;
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
						if (!character.variants.empty()) state.characters.push_back(std::move(character));
					}
					std::sort(state.characters.begin(), state.characters.end(),
						[](const Character& a, const Character& b) { return Lower(a.label) < Lower(b.label); });
					if (!state.characters.empty()) state.selectedCharacter = 0;
				}
			}

			if (ReadFile(dir + "PropShortlist.json", text))
			{
				const json doc = json::parse(text, nullptr, false);
				if (!doc.is_discarded() && doc.is_object() && doc.contains("props") && doc["props"].is_array())
				{
					for (const json& p : doc["props"])
					{
						const std::string hex = p.value("hash", "");
						if (hex.size() < 3) continue;
						state.props.push_back(PropEntry{ p.value("model", hex), std::strtoul(hex.c_str(), nullptr, 16), p.value("placements", 0) });
					}
					if (!state.props.empty()) state.selectedProp = 0;
				}
				else if (state.loadError.empty())
					state.loadError = "PropShortlist.json is not the expected JSON (an object with a \"props\" array)";
			}
			else if (state.loadError.empty())
				state.loadError = "no PropShortlist.json yet - run tools/palette_to_pedlist.py; typed prop names still work";

			addlog(ige::LogType::LOG_INFO, "CharacterPicker: " + std::to_string(state.characters.size()) + " characters, " +
				std::to_string(state.props.size()) + " shortlisted props");
		}

		// ---- aiming (script thread) ----

		struct Aim
		{
			bool ok = false;
			std::string problem;
			Vector3 point;
			Vector3 cameraPosition;
			float cameraYaw = 0.0f;
		};

		void CameraPose(Vector3& position, Vector3& direction, float& yaw, bool& spooner)
		{
			auto& spoonerCam = SpoonerMode::spoonerModeCamera;
			spooner = spoonerCam.IsActive();
			if (spooner)
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

		// One rule for every placement: under the cursor while the window owns the
		// mouse, under the screen centre otherwise.
		Aim AimNow(bool mouseValid, float mouseU, float mouseV)
		{
			Aim aim;
			Vector3 direction;
			bool spooner = false;
			CameraPose(aim.cameraPosition, direction, aim.cameraYaw, spooner);

			Vector3 target = aim.cameraPosition + direction * kAimDistance;
			if (IsCursorMode() && mouseValid)
			{
				// Camera::WorldToScreenRel maps 0..1 screen space to -1..1, y down.
				const Vector2 rel(mouseU * 2.0f - 1.0f, mouseV * 2.0f - 1.0f);
				const Vector3 onRay = spooner ? SpoonerMode::spoonerModeCamera.ScreenToWorld(rel) : GameplayCamera::ScreenToWorld(rel);
				Vector3 toward = onRay - aim.cameraPosition;
				if (toward.Length() < 0.01f)
				{
					aim.problem = "cannot build a ray through the cursor";
					return aim;
				}
				toward.Normalize();
				target = aim.cameraPosition + toward * kAimDistance;
			}

			const RaycastResult hit = RaycastResult::Raycast(aim.cameraPosition, target, IntersectOptions::Everything);
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

		// ---- placing (script thread) ----

		struct PlaceJob
		{
			Mode mode;
			Character character;
			int variantIndex;
			std::string propModel;
			unsigned long propHash;
			int count;
			float spacing;
			bool fillBySpacing;
			Facing facing;
			float jitter;
			bool rainbow;
			bool armsWaving;
			int texture;
			bool cycleTextures;
			// where: either a row across the aim, or the line A->B
			bool alongLine;
			Vector3 lineA, lineB;
		};

		float YawToward(const Vector3& from, const Vector3& to)
		{
			// Game heading: 0 looks along +y, 90 along -x.
			return static_cast<float>(std::atan2(-(to.x - from.x), to.y - from.y) * 180.0 / MATH_PI);
		}

		std::vector<const Variant*> ColourVariants(const Character& c)
		{
			std::vector<const Variant*> out;
			for (const Variant& v : c.variants) if (!v.color.empty()) out.push_back(&v);
			return out;
		}

		// Recolour through the ped's own component textures. Only components that
		// actually carry more than one texture change; the others are left alone.
		int ApplyTexture(int ped, int wanted)
		{
			int slots = 1;
			for (int comp = 0; comp < kPedComponents; ++comp)
			{
				const int drawable = GET_PED_DRAWABLE_VARIATION(ped, comp);
				const int textures = GET_NUMBER_OF_PED_TEXTURE_VARIATIONS(ped, comp, drawable);
				if (textures > slots) slots = textures;
				if (textures > 1)
					SET_PED_COMPONENT_VARIATION(ped, comp, drawable, wanted % textures, 0);
			}
			return slots;
		}

		bool SpawnPedAt(const PlaceJob& job, const Variant& variant, Vector3 spot, float yaw, int textureIndex,
			std::vector<int>& placed, std::vector<std::string>& problems, int& textureSlots)
		{
			spot.z += kPedStandHeight;
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
			if (!Http::EntityApi::CreateDirect(request, id, failure))
			{
				problems.push_back(variant.model + ": " + failure);
				return false;
			}
			if (textureIndex > 0 || job.cycleTextures)
				textureSlots = ApplyTexture(id, textureIndex);
			placed.push_back(id);
			return true;
		}

		bool SpawnPropAt(const PlaceJob& job, Vector3 spot, float yaw, std::vector<int>& placed, std::vector<std::string>& problems)
		{
			GTAmodel::Model model(static_cast<Hash>(job.propHash));
			if (!model.IsInCdImage())
			{
				problems.push_back(job.propModel + ": not in the game files");
				return false;
			}
			// Loaded first so the box is known: the prop's own bottom, not its origin,
			// goes on the surface.
			model.Load(3000);
			const Http::ModelApi::Box box = Http::ModelApi::GetBox(job.propHash);
			if (!box.valid)
			{
				problems.push_back(job.propModel + ": the game reports no box for it");
				return false;
			}
			spot.z -= box.min.z;

			Http::EntityApi::CreateRequest request{};
			request.type = 3;
			request.model = job.propHash;
			request.modelLabel = job.propModel;
			request.name = job.propModel;
			request.position = { spot.x, spot.y, spot.z };
			request.rotation = { 0.0f, 0.0f, yaw };
			request.dynamic = false;
			request.snapToGround = false;
			request.still = false;
			request.tolerance = 0.25f;
			int id = 0;
			std::string failure;
			if (!Http::EntityApi::CreateDirect(request, id, failure))
			{
				problems.push_back(job.propModel + ": " + failure);
				return false;
			}
			placed.push_back(id);
			return true;
		}

		std::vector<Vector3> SpotsAlong(const Vector3& a, const Vector3& b, int count, bool fill, float spacing)
		{
			std::vector<Vector3> spots;
			const Vector3 delta = b - a;
			const float length = delta.Length();
			if (length < 0.001f) { spots.push_back(a); return spots; }
			const Vector3 unit = delta * (1.0f / length);
			int n = count;
			float start = 0.0f, step = 0.0f;
			if (fill)
			{
				n = static_cast<int>(length / spacing) + 1;
				step = spacing;
				start = (length - (n - 1) * spacing) / 2.0f;   // centred between the two points
			}
			else if (n <= 1)
			{
				n = 1; start = length / 2.0f;
			}
			else
			{
				step = length / (n - 1);
			}
			for (int i = 0; i < n; ++i)
				spots.push_back(a + unit * (start + i * step));
			return spots;
		}

		void RunPlace(const PlaceJob& job, const Aim& aim)
		{
			std::vector<Vector3> spots;
			if (job.alongLine)
				spots = SpotsAlong(job.lineA, job.lineB, job.count, job.fillBySpacing, job.spacing);
			else
			{
				// A row across the view, centred on the aim.
				const float yawRad = static_cast<float>(aim.cameraYaw * MATH_PI / 180.0);
				const Vector3 right(std::cos(yawRad), std::sin(yawRad), 0.0f);
				const float half = static_cast<float>(job.count - 1) / 2.0f * job.spacing;
				spots = SpotsAlong(aim.point - right * half, aim.point + right * half, job.count, false, job.spacing);
			}

			std::mt19937 rng(static_cast<unsigned>(GetTickCount()));
			std::uniform_real_distribution<float> jitter(-job.jitter, job.jitter);
			const float referenceZ = job.alongLine ? (job.lineA.z + job.lineB.z) / 2.0f : aim.point.z;

			Vector3 lineNormal(0.0f, 1.0f, 0.0f);
			if (job.alongLine)
			{
				const Vector3 d = job.lineB - job.lineA;
				lineNormal = Vector3(-d.y, d.x, 0.0f);
				if (lineNormal.Length() > 0.001f) lineNormal.Normalize();
				const Vector3 mid = (job.lineA + job.lineB) * 0.5f;
				const Vector3 toCam = aim.cameraPosition - mid;
				if (lineNormal.x * toCam.x + lineNormal.y * toCam.y < 0.0f) lineNormal = lineNormal * -1.0f;
			}

			std::vector<const Variant*> colours;
			if (job.mode == Mode::Peds) colours = ColourVariants(job.character);
			const bool rainbowOn = job.mode == Mode::Peds && job.rainbow && colours.size() > 1;

			std::vector<int> placed;
			std::vector<std::string> problems;
			int textureSlots = 0;
			for (int i = 0; i < static_cast<int>(spots.size()); ++i)
			{
				Vector3 spot = spots[i];
				// Each spot finds its own surface: a row can run off a plank, a line can
				// cross a gap.
				Vector3 probe = spot;
				probe.z = referenceZ + 2.5f;
				const float ground = World::GetGroundHeight(probe);
				if (ground <= -1000.0f || ground >= 10000.0f || std::fabs(ground - referenceZ) > 3.0f)
				{
					problems.push_back("#" + std::to_string(i + 1) + ": no surface at that spot");
					continue;
				}
				spot.z = ground;

				float yaw = aim.cameraYaw;
				switch (job.facing)
				{
				case Facing::TowardCamera:   yaw = YawToward(spot, aim.cameraPosition); break;
				case Facing::AwayFromCamera: yaw = YawToward(aim.cameraPosition, spot); break;
				case Facing::AcrossLine:     yaw = static_cast<float>(std::atan2(-lineNormal.x, lineNormal.y) * 180.0 / MATH_PI); break;
				case Facing::SameAsCamera:   break;
				}
				yaw += jitter(rng);

				if (job.mode == Mode::Props)
				{
					SpawnPropAt(job, spot, yaw, placed, problems);
					continue;
				}
				const Variant& variant = rainbowOn
					? *colours[(job.variantIndex + i) % colours.size()]
					: job.character.variants[job.variantIndex];
				const int textureIndex = job.cycleTextures ? job.texture + i : job.texture;
				SpawnPedAt(job, variant, spot, yaw, textureIndex, placed, problems, textureSlots);
			}

			std::lock_guard<std::mutex> lock(g_mutex);
			if (!placed.empty())
			{
				g_state.lastPlaced = placed;
				g_state.placedTotal += static_cast<int>(placed.size());
			}
			const std::string what = job.mode == Mode::Props ? job.propModel : job.character.label;
			g_state.status = "placed " + std::to_string(placed.size()) + " x " + what;
			if (textureSlots > 0)
				g_state.status += " (textures on this model: " + std::to_string(textureSlots) + ")";
			if (!problems.empty())
			{
				g_state.status += "; " + std::to_string(problems.size()) + " not placed: " + problems.front();
				for (const auto& p : problems) addlog(ige::LogType::LOG_WARNING, "CharacterPicker: " + p);
			}
			// Round robin across clicks: the next placement starts where this one's
			// colours left off.
			if (rainbowOn && g_state.selectedCharacter >= 0 && g_state.selectedCharacter < static_cast<int>(g_state.characters.size()))
			{
				const Character& current = g_state.characters[g_state.selectedCharacter];
				const std::vector<const Variant*> now = ColourVariants(current);
				if (now.size() == colours.size() && !now.empty())
				{
					const Variant* next = now[(job.variantIndex + spots.size()) % now.size()];
					for (int v = 0; v < static_cast<int>(current.variants.size()); ++v)
						if (&current.variants[v] == next) g_state.selectedVariant = v;
				}
			}
			if (job.cycleTextures) g_state.texture = (job.texture + static_cast<int>(spots.size())) % 16;
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

		// Cursor mode only, and never while typing into the window.
		void Keyboard(State& state)
		{
			if (!IsCursorMode() || ImGuiSpooner::WantsTextInput())
				return;
			if (state.mode == Mode::Peds && !state.characters.empty())
			{
				const int n = static_cast<int>(state.characters.size());
				if (IsKeyJustUp(VirtualKey::Down)) { state.selectedCharacter = (state.selectedCharacter + 1) % n; state.selectedVariant = 0; }
				if (IsKeyJustUp(VirtualKey::Up))   { state.selectedCharacter = (state.selectedCharacter + n - 1) % n; state.selectedVariant = 0; }
				if (state.selectedCharacter >= 0)
				{
					const int v = static_cast<int>(state.characters[state.selectedCharacter].variants.size());
					if (IsKeyJustUp(VirtualKey::Right)) state.selectedVariant = (state.selectedVariant + 1) % v;
					if (IsKeyJustUp(VirtualKey::Left))  state.selectedVariant = (state.selectedVariant + v - 1) % v;
				}
			}
			if (state.mode == Mode::Props && !state.props.empty())
			{
				const int n = static_cast<int>(state.props.size());
				if (IsKeyJustUp(VirtualKey::Down)) { state.selectedProp = (state.selectedProp + 1) % n; state.customProp.clear(); }
				if (IsKeyJustUp(VirtualKey::Up))   { state.selectedProp = (state.selectedProp + n - 1) % n; state.customProp.clear(); }
			}
			if (IsKeyJustUp(VirtualKey::Return)) state.requestPlace = true;
			if (IsKeyJustUp(VirtualKey::N1)) state.requestSetA = true;
			if (IsKeyJustUp(VirtualKey::N2)) state.requestSetB = true;
			if (IsKeyJustUp(VirtualKey::N3)) state.requestPlaceLine = true;
		}

		bool BuildJob(const State& state, PlaceJob& job, std::string& problem)
		{
			job.mode = state.mode;
			job.count = std::clamp(state.count, 1, 60);
			job.spacing = state.spacing;
			job.fillBySpacing = state.fillBySpacing;
			job.facing = state.facing;
			job.jitter = state.jitter;
			job.rainbow = state.rainbow;
			job.armsWaving = state.armsWaving;
			job.texture = std::clamp(state.texture, 0, 15);
			job.cycleTextures = state.cycleTextures;
			if (state.mode == Mode::Peds)
			{
				if (state.selectedCharacter < 0 || state.selectedCharacter >= static_cast<int>(state.characters.size()))
				{
					problem = "pick a character first";
					return false;
				}
				job.character = state.characters[state.selectedCharacter];
				job.variantIndex = std::clamp(state.selectedVariant, 0, static_cast<int>(job.character.variants.size()) - 1);
				return true;
			}
			if (!state.customProp.empty())
			{
				job.propModel = state.customProp;
				job.propHash = GET_HASH_KEY(state.customProp.c_str());   // Menyoo's own joaat
				return true;
			}
			if (state.selectedProp < 0 || state.selectedProp >= static_cast<int>(state.props.size()))
			{
				problem = "pick a prop from the list or type a model name";
				return false;
			}
			job.propModel = state.props[state.selectedProp].model;
			job.propHash = state.props[state.selectedProp].hash;
			return true;
		}

		// ---- window helpers (render thread) ----

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

		void DrawVariants(State& state, const Character& c)
		{
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
				// Not std::max: Windows.h is in this unit without NOMINMAX, so max is a macro.
				const float textWidth = ImGui::CalcTextSize(caption.c_str()).x + 18.0f;
				const float width = textWidth > 72.0f ? textWidth : 72.0f;
				if (used > 0.0f && used + width > avail) used = 0.0f;
				else if (used > 0.0f) ImGui::SameLine();
				char id[160];
				std::snprintf(id, sizeof(id), "%s##v%d", caption.c_str(), v);
				if (ImGui::Button(id, ImVec2(width, 30.0f))) state.selectedVariant = v;
				used += width + 8.0f;
				ImGui::PopStyleVar();
				ImGui::PopStyleColor(3);
				if (ImGui::IsItemHovered() && !variant.note.empty())
					ImGui::SetTooltip("%s\n%s", variant.model.c_str(), variant.note.c_str());
			}
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
		Aim aim;
		std::vector<int> undo;
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			State& state = g_state;
			if (!state.loaded || state.requestReload)
			{
				state.requestReload = false;
				Load(state);
			}
			Keyboard(state);

			// The aim is refreshed every frame so the marker shows where the next
			// placement lands before anyone clicks.
			aim = AimNow(state.mouseValid, state.mouseU, state.mouseV);
			state.aimValid = aim.ok;
			state.aimProblem = aim.ok ? "" : aim.problem;
			if (aim.ok) state.aimPoint = ToPoint(aim.point);

			if (state.requestClearLine) { state.requestClearLine = false; state.hasA = state.hasB = false; }
			if (state.requestSetA) { state.requestSetA = false; if (aim.ok) { state.pointA = ToPoint(aim.point); state.hasA = true; } else state.status = "A: " + aim.problem; }
			if (state.requestSetB) { state.requestSetB = false; if (aim.ok) { state.pointB = ToPoint(aim.point); state.hasB = true; } else state.status = "B: " + aim.problem; }

			if (state.requestUndo) { state.requestUndo = false; undo = state.lastPlaced; }

			if (state.requestPlace || state.requestPlaceLine)
			{
				const bool line = state.requestPlaceLine;
				state.requestPlace = state.requestPlaceLine = false;
				std::string problem;
				if (line && !(state.hasA && state.hasB))
					state.status = "set both A (1) and B (2) first";
				else if (!line && !aim.ok)
					state.status = aim.problem;
				else if (!BuildJob(state, job, problem))
					state.status = problem;
				else
				{
					job.alongLine = line;
					job.lineA = ToVec(state.pointA);
					job.lineB = ToVec(state.pointB);
					doPlace = true;
				}
			}

			// Authoring aids, drawn into the world: the aim spot and the two points.
			if (state.aimValid)
				World::DrawMarker(MarkerType::DebugSphere, ToVec(state.aimPoint), Vector3(), Vector3(), Vector3(0.25f, 0.25f, 0.25f), RGBA(255, 255, 255, 170));
			if (state.hasA)
				World::DrawMarker(MarkerType::DebugSphere, ToVec(state.pointA), Vector3(), Vector3(), Vector3(0.35f, 0.35f, 0.35f), RGBA(90, 220, 90, 200));
			if (state.hasB)
				World::DrawMarker(MarkerType::DebugSphere, ToVec(state.pointB), Vector3(), Vector3(), Vector3(0.35f, 0.35f, 0.35f), RGBA(255, 120, 60, 200));
			if (state.hasA && state.hasB)
				World::DrawLine(ToVec(state.pointA), ToVec(state.pointB), RGBA(255, 220, 90, 200));
		}
		// Spawning streams models and yields frames; the lock is not held for it,
		// so the window keeps drawing.
		if (!undo.empty()) RunUndo(undo);
		if (doPlace) RunPlace(job, aim);
	}

	void Draw()
	{
		if (!g_windowVisible)
			return;
		bool open = true;
		ImGui::SetNextWindowSize(ImVec2(660.0f, 600.0f), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin("Characters", &open))
		{
			ImGui::End();
			if (!open) SetWindowVisible(false);
			return;
		}

		std::lock_guard<std::mutex> lock(g_mutex);
		State& state = g_state;
		const ImGuiIO& io = ImGui::GetIO();

		// The cursor the person sees, in 0..1 of the screen; the script thread turns
		// it into a world ray.
		state.mouseValid = io.DisplaySize.x > 1.0f && io.DisplaySize.y > 1.0f && io.MousePos.x > -100000.0f;
		if (state.mouseValid)
		{
			state.mouseU = std::clamp(io.MousePos.x / io.DisplaySize.x, 0.0f, 1.0f);
			state.mouseV = std::clamp(io.MousePos.y / io.DisplaySize.y, 0.0f, 1.0f);
		}

		if (!state.loadError.empty())
			ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", state.loadError.c_str());
		ImGui::TextDisabled("F7 mouse mode. Click in the world or Enter: place at the white dot (under the cursor in mouse mode, screen centre otherwise).");
		ImGui::TextDisabled("Up/Down pick, Left/Right colour. 1 = set A, 2 = set B, 3 = place along A-B.");

		int mode = static_cast<int>(state.mode);
		ImGui::RadioButton("Peds", &mode, 0); ImGui::SameLine();
		ImGui::RadioButton("Props", &mode, 1); ImGui::SameLine();
		state.mode = static_cast<Mode>(mode);
		ImGui::SetNextItemWidth(200.0f);
		ImGui::InputTextWithHint("##filter", "filter...", g_filter, sizeof(g_filter));
		ImGui::SameLine();
		if (ImGui::Button("Reload lists")) state.requestReload = true;
		const std::string needle = Lower(g_filter);

		ImGui::BeginChild("##list", ImVec2(240.0f, -120.0f), true);
		if (state.mode == Mode::Peds)
		{
			for (int i = 0; i < static_cast<int>(state.characters.size()); ++i)
			{
				const Character& c = state.characters[i];
				if (!Matches(c, needle)) continue;
				char row[128];
				std::snprintf(row, sizeof(row), "%s  (%d)##c%d", c.label.c_str(), static_cast<int>(c.variants.size()), i);
				if (ImGui::Selectable(row, state.selectedCharacter == i)) { state.selectedCharacter = i; state.selectedVariant = 0; }
			}
		}
		else
		{
			for (int i = 0; i < static_cast<int>(state.props.size()); ++i)
			{
				const PropEntry& p = state.props[i];
				if (!needle.empty() && Lower(p.model).find(needle) == std::string::npos) continue;
				char row[160];
				std::snprintf(row, sizeof(row), "%s  (%d)##p%d", p.model.c_str(), p.placements, i);
				if (ImGui::Selectable(row, state.selectedProp == i && state.customProp.empty())) { state.selectedProp = i; state.customProp.clear(); g_propName[0] = 0; }
			}
		}
		ImGui::EndChild();

		ImGui::SameLine();
		ImGui::BeginChild("##right", ImVec2(0.0f, -120.0f), true);
		if (state.mode == Mode::Peds)
		{
			if (state.selectedCharacter >= 0 && state.selectedCharacter < static_cast<int>(state.characters.size()))
			{
				const Character& c = state.characters[state.selectedCharacter];
				ImGui::Text("%s", c.label.c_str());
				ImGui::Separator();
				DrawVariants(state, c);
				ImGui::Spacing();
				ImGui::Checkbox("rainbow: next colour for each ped, and the next click carries on", &state.rainbow);
				ImGui::Checkbox("arms waving (the author's look)", &state.armsWaving);
				ImGui::SetNextItemWidth(120.0f); ImGui::SliderInt("texture", &state.texture, 0, 15);
				ImGui::SameLine(); ImGui::Checkbox("cycle textures", &state.cycleTextures);
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("Recolour through the model's own component textures.\nWorks only for models that ship several; the status says how many this one has.");
			}
			else ImGui::TextDisabled("pick a character on the left");
		}
		else
		{
			ImGui::Text("Prop");
			ImGui::SetNextItemWidth(-1.0f);
			if (ImGui::InputTextWithHint("##propname", "or type a model name: prop_beach_ring_01", g_propName, sizeof(g_propName)))
				state.customProp = g_propName;
			if (!state.customProp.empty()) ImGui::TextDisabled("typed: %s", state.customProp.c_str());
			else if (state.selectedProp >= 0 && state.selectedProp < static_cast<int>(state.props.size()))
				ImGui::TextDisabled("from the list: %s", state.props[state.selectedProp].model.c_str());
			ImGui::TextWrapped("The list is the author's own vocabulary: the 150 props the saved maps use most. Props are frozen and put down on their bottom face.");
		}

		ImGui::Separator();
		ImGui::SetNextItemWidth(150.0f); ImGui::SliderInt("count", &state.count, 1, 60);
		ImGui::SameLine(); ImGui::Checkbox("fill A-B by spacing", &state.fillBySpacing);
		ImGui::SetNextItemWidth(150.0f); ImGui::SliderFloat("spacing m", &state.spacing, 0.3f, 6.0f, "%.2f");
		ImGui::SetNextItemWidth(150.0f); ImGui::SliderFloat("yaw jitter", &state.jitter, 0.0f, 45.0f, "%.0f deg");
		int facing = static_cast<int>(state.facing);
		ImGui::RadioButton("face camera", &facing, 0); ImGui::SameLine();
		ImGui::RadioButton("face away", &facing, 1); ImGui::SameLine();
		ImGui::RadioButton("as camera", &facing, 2); ImGui::SameLine();
		ImGui::RadioButton("across A-B", &facing, 3);
		state.facing = static_cast<Facing>(facing);
		ImGui::EndChild();

		// Line controls
		ImGui::Text("A: %s   B: %s", state.hasA ? "set" : "-", state.hasB ? "set" : "-");
		ImGui::SameLine(); if (ImGui::Button("Set A at aim (1)")) state.requestSetA = true;
		ImGui::SameLine(); if (ImGui::Button("Set B at aim (2)")) state.requestSetB = true;
		ImGui::SameLine(); if (ImGui::Button("Clear")) state.requestClearLine = true;
		ImGui::SameLine(); if (ImGui::Button("Place along A-B (3)")) state.requestPlaceLine = true;

		if (ImGui::Button("Place at aim (Enter / click)", ImVec2(240.0f, 30.0f))) state.requestPlace = true;
		ImGui::SameLine();
		if (ImGui::Button("Undo last group", ImVec2(150.0f, 30.0f))) state.requestUndo = true;
		ImGui::SameLine();
		ImGui::TextDisabled("placed %d", state.placedTotal);
		if (!state.aimValid && !state.aimProblem.empty())
			ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.4f, 1.0f), "aim: %s", state.aimProblem.c_str());
		if (!state.status.empty())
			ImGui::TextWrapped("%s", state.status.c_str());

		// A click in the world, not on the window, while the window owns the mouse.
		if (IsCursorMode() && !io.WantCaptureMouse && io.MouseClicked[0])
			state.requestPlace = true;

		ImGui::End();
		if (!open) SetWindowVisible(false);
	}
}
