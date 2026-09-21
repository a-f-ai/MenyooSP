#include "BooleanHotkeyRegistry.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
	void Check(bool condition, const std::string& message)
	{
		if (condition) return;
		std::cerr << "FAIL: " << message << '\n';
		std::exit(1);
	}
}

int main()
{
	BooleanHotkeyRegistry registry;
	bool hudHidden = false;
	bool lodEnabled = true;

	registry.Register({ "hide-hud", { 0x48, true, true, false }, [&] { return hudHidden; }, [&](bool value) {
		hudHidden = value;
		return true;
	} });
	registry.Register({ "ped-lod", { 0x4C, true, true, false }, [&] { return lodEnabled; }, [&](bool value) {
		lodEnabled = value;
		return true;
	} });

	registry.Event(0x11, false, false);
	registry.Event(0x10, false, false);
	registry.Event(0x48, false, false);
	Check(registry.Event(0x48, true, false), "matched chord release is marked consumed");
	auto results = registry.DispatchPending();
	Check(results.size() == 1 && results[0].actionId == "hide-hud" && results[0].enabled,
		"Ctrl+Shift+H toggles only hide-hud on");
	Check(hudHidden && lodEnabled, "unrelated boolean is unchanged");
	Check(registry.DispatchPending().empty(), "release is consumed exactly once");

	registry.Event(0x12, false, false);
	registry.Event(0x4C, false, false);
	Check(!registry.Event(0x4C, true, false), "disqualified chord release is not consumed");
	Check(registry.DispatchPending().empty(), "extra Alt rejects an exact Ctrl+Shift chord");
	registry.Event(0x12, true, false);

	registry.Event(0x4C, false, false);
	registry.Event(0x4C, false, true);
	registry.Event(0x4C, true, true);
	results = registry.DispatchPending();
	Check(results.size() == 1 && results[0].actionId == "ped-lod" && !results[0].enabled,
		"repeat events still dispatch one toggle on release");

	bool duplicateActionRejected = false;
	try
	{
		registry.Register({ "hide-hud", { 0x4A, true, true, false }, [] { return false; }, [](bool) { return true; } });
	}
	catch (const std::logic_error&)
	{
		duplicateActionRejected = true;
	}
	Check(duplicateActionRejected, "duplicate action ids are rejected");

	bool duplicateBindingRejected = false;
	try
	{
		registry.Register({ "another-action", { 0x48, true, true, false }, [] { return false; }, [](bool) { return true; } });
	}
	catch (const std::logic_error&)
	{
		duplicateBindingRejected = true;
	}
	Check(duplicateBindingRejected, "duplicate bindings are rejected");

	bool rejectedValue = false;
	registry.Register({ "reject-toggle", { 0x52, true, true, false }, [&] { return rejectedValue; }, [](bool) { return false; } });
	registry.Event(0x52, false, false);
	registry.Event(0x52, true, false);
	results = registry.DispatchPending();
	Check(results.size() == 1 && !results[0].success && !results[0].enabled,
		"setter rejection is explicit and leaves the previous value visible");

	std::cout << "BooleanHotkeyRegistryTests: OK\n";
}
