# Prior art for UVC Controller Layout (searched 2026-11-04, rule 24)

Wanted: on the game's Controls page, a fixed controller graphic with a pointer to each button and a box beside it that follows what
the player sets in Unbind Vanilla Controls.

Searched Nexus (skyrimspecialedition, GraphQL, name and description, op MATCHES): controller diagram, controller layout, gamepad
layout, controller bindings, controls overlay, cheat sheet, hotkey reminder, controller map, button map, gamepad diagram, control
scheme, controls menu. Pages read through the API: 193486, 197710, 175045, 115853, 140010, 25470. A web search on the same words found
nothing further.

Searched GitHub (`gh search repos`): skyrim controller layout overlay SKSE, skyrim gamepad diagram hotkey, skyrim hotkey reminder
controller, SKSE controls cheat sheet gamepad, CommonLibSSE controlmap viewer gamepad, skyrim keybinds overlay, Hotkey Atlas, skyrim
hotkey. Hits that matter: Neutral9/HotkeyAtlas, whose source was read.

Closest:

- **Hotkey Atlas (193486), GPL-3.0, 1.5.97 to 1.7.104, source on GitHub.** The nearest: draws a gamepad in SKSE Menu Framework with
  PlayStation labels as an option, from a scan of the control map, MCM values, INIs and YAML. It is its own binding system: it remaps
  through its own input hook and presets, so it would be a second owner of the bindings beside Unbind Vanilla Controls. Its boxes count
  binds rather than follow UVC's rows, no pointers, and it is not on the Controls page.
- **Hotkey Reminder (115853), on the list.** Has a keyboard and a gamepad picture, but the labels are typed by hand and saved to a file.
  It stays for the keyboard.
- **Hotkey Central (197710), Action Hotkeys (175045).** ImGui lists and launcher grids; no pad picture. Action Hotkeys has a keyboard map only.
- **Dax's guide (140010), LoreRim mapping (111887), Kontrol (25470) and other control maps:** a fixed picture on the mod page, not in game.

Nothing does this. Nothing reads Unbind Vanilla Controls either, since it is our own mod, so an add-on is the plain way in.
Hotkey Atlas's layout table was read for the idea only; none of its code or art is used.
