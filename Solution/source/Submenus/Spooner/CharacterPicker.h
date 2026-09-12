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

	enum class Facing { TowardCamera, AwayFromCamera, SameAsCamera };

	struct State
	{
		std::vector<Character> characters;   // sorted by label
		bool loaded = false;
		std::string loadError;               // non-empty: why the list is empty
		std::string status;

		int selectedCharacter = -1;
		int selectedVariant = 0;

		int count = 1;
		float spacing = 0.9f;
		Facing facing = Facing::TowardCamera;
		float jitter = 15.0f;
		bool rainbow = true;      // walk through the character's colours along the row
		bool armsWaving = true;   // the author's peds wave their arms

		// Raised by the window or the keyboard, cleared by the script thread.
		bool requestReload = false;
		bool requestPlaceAtCentre = false;
		bool requestPlaceAtMouse = false;
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
