#pragma once

namespace sub::Spooner::ImGuiSpooner
{
	bool Initialize();
	void Shutdown();

	void Tick();

	void SetVisible(bool visible);
	bool IsVisible();

	// The hook only renders and forwards input while it believes an overlay is
	// up, so anything that shows its own window must say so.
	void NotifyOverlayChanged();

	// True while an ImGui text field has the keyboard, so a hotkey does not
	// fire while the user is typing into the window.
	bool WantsTextInput();
}
