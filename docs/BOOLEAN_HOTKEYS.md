# Boolean hotkeys

Menyoo registers boolean actions once, then the shared keyboard dispatcher
matches exact configured chords and toggles them on the game thread.

The shipped bindings are:

- `Ctrl+Shift+H`: `hide-hud`
- `Ctrl+Shift+L`: `ped-lod`
- `Ctrl+Shift+F`: `display-fps`

Change the `Key`, `Control`, `Shift`, and `Alt` values in the `[general]`
section of `menyooStuff/menyooConfig.ini`. Keys use Windows virtual-key codes.
Restart Menyoo after changing a binding.

The preferred Spooner map action is separate from the boolean registry. Select
an XML map in `Manage Saved Files`, choose `Set as Preferred Map`, then press
`Ctrl+Shift+Q` to load it through the normal placements loader. Its relative
path is stored as `PreferredMap` in `[object-spooner]`; map XML is not changed.
The chord uses `PreferredMapLoadKey`, `PreferredMapLoadControl`,
`PreferredMapLoadShift`, and `PreferredMapLoadAlt` in `[general]`. A missing or
unset preferred map reports an error and does not load another map.

To add a third boolean action, register it beside the existing actions in
`MenyooMain()`:

```cpp
RegisterBooleanHotkey({ "third-action",
    { static_cast<unsigned>(BindThirdAction), true, true, false },
    [] { return ThirdAction::Enabled(); },
    [](bool enabled) { return ThirdAction::SetEnabled(enabled); } });
```

Add the matching key and modifier fields to `MenuConfig::ConfigRead()` and
`MenuConfig::SaveConfig()`. Action IDs and complete chords must be unique;
duplicates stop initialization with an explicit error instead of selecting an
action implicitly.
