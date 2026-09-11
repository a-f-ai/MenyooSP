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
}
