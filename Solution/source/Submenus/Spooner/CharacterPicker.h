/*
* Menyoo PC - Grand Theft Auto V single-player trainer mod
*
* The character picker: the author's addon peds by name and colour, placed
* where the camera or the mouse points, one or a row at a time.
*
* Same bargain as the camera path window. Draw() runs on the render thread
* and only edits State() under the mutex; Tick() runs on the script thread
* and is the only place the game is touched. The list comes from
* menyooStuff/Characters.json, written from palette/characters.json by
* tools/palette_to_pedlist.py, so a model that is broken or kills the script
* is simply not in it.
*/
#pragma once

#include <mutex>
#include <string>
#include <vector>

namespace sub::Spooner::CharacterPicker
{
	struct Variant
	{
		std::string model;
		unsigned long hash;
		std::string color;   // empty when the variant is not a colour
		std::string note;
	};

	struct Character
	{
		std::string key;
		std::string label;
		std::vector<std::string> aliases;
		std::vector<Variant> variants;
	};

	enum class Facing { TowardCamera, AwayFromCamera, SameAsCamera, AcrossLine };
	enum class Mode { Peds, Props, Vehicles };
	// How a line A-B (or a row) is filled: exactly count, as many as fit at spacing,
	// or packed by the spawned thing's own box plus a gap.
	enum class Fill { Count, BySpacing, Pack };

	struct PropEntry
	{
		std::string model;
		unsigned long hash;
		int placements;             // how often the author's maps use it
		std::vector<int> tintsUsed; // TextureVariation values the author set on it; the real
		                            // count is baked into the model and cannot be queried
	};

	struct VehicleEntry
	{
		std::string label;   // the author's name for it in the maps
		unsigned long hash;
		int placements;
	};

	struct Point3 { float x = 0.0f, y = 0.0f, z = 0.0f; };

	struct State
	{
		std::vector<Character> characters;   // sorted by label
		std::vector<PropEntry> props;        // the author's shortlist, most used first
		std::vector<VehicleEntry> vehicles;  // likewise
		bool loaded = false;
		std::string loadError;               // non-empty: why a list is empty
		std::string status;

		Mode mode = Mode::Peds;
		int selectedCharacter = -1;
		int selectedVariant = 0;
		int selectedProp = -1;
		std::string customProp;              // a typed model name wins over the list
		int selectedVehicle = -1;
		std::string customVehicle;
		bool rainbowCars = true;             // body colour walks a palette along the row

		int count = 1;
		float spacing = 0.9f;
		Facing facing = Facing::TowardCamera;
		float jitter = 15.0f;
		bool rainbow = true;      // next colour for each ped; only colour variants take part
		bool armsWaving = true;   // the author's peds wave their arms
		int texture = 0;          // ped component texture, for models that carry recolours
		bool cycleTextures = false;
		int tint = 0;             // prop TextureVariation: the colour of stunt blocks and tubes
		bool cycleTints = false;  // next tint for each prop along a row, and the next click carries on
		int tintCount = 16;       // how many tints the model has; bblock/tube families use 0..16

		// The cursor the person sees is ImGui's; the world ray is built from it,
		// not from the game's own cursor, which drifts from it under CrossOver.
		bool mouseValid = false;
		float mouseU = 0.5f, mouseV = 0.5f;

		// Where a placement would land right now, redrawn every frame.
		bool aimValid = false;
		Point3 aimPoint;
		std::string aimProblem;

		// Two points and a line between them.
		bool hasA = false, hasB = false;
		Point3 pointA, pointB;
		Fill fill = Fill::Count;
		float gap = 0.3f;                    // Pack: air between neighbours' boxes

		// Raised by the window or the keyboard, cleared by the script thread.
		bool requestReload = false;
		bool requestPlace = false;       // at the aim
		bool requestSetA = false, requestSetB = false, requestClearLine = false;
		bool requestPlaceLine = false;
		bool requestUndo = false;

		std::vector<int> lastPlaced;
		int placedTotal = 0;
	};

	std::mutex& StateMutex();
	State& GetState();

	void Tick();   // script thread, once per frame
	void Draw();   // render thread, inside the ImGui frame

	bool IsWindowVisible();
	void SetWindowVisible(bool visible);
	void ToggleWindow();
	bool IsCursorMode();
	void ToggleCursorMode();
}
