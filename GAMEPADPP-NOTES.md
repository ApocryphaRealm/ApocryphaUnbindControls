# Gamepad++ - what UVC reads, and where it was learned (1.5.0, 2026-11-06)

The owner, 2026-11-06 19:42: *"I'd rather add compatibility for Gamepad++ so that it can interact properly and know what
it's doing, than try to replace it."* Code: `include/GamepadPP.h`, `source/GamepadPP.cpp`.

## Source of these facts

Gamepad++ 1.2.1 by Dunc001, Nexus SSE 27007, main file `Gamepad Plus Plus-27007-1-2-1-1616967745.7z`, extracted to
`D:\Claude output\4. plans\amf-gamepadpp-presets\extract\`. It is **not installed** in Njordlinger or Apostasy (checked
2026-11-06: no mod folder, no `Gamepad++.esp`, no `Gamepad++\Presets` in either Overwrite), so nothing here was read from
a running game yet. No licence file ships in the archive and its Nexus permissions were not read; no Gamepad++ code is
copied into UVC. What UVC relies on is its plugin's records, its data files, and the names of its script properties.

| Fact | Where it was read |
|---|---|
| Plugin `Gamepad++.esp` (ESP and ESL-flagged variants, same name, FOMOD picks one) | `00 ESP`, `00 ESL`, `fomod\ModuleConfig.xml` |
| Records: QUST `gpp_controlquest` 0x800, QUST `gpp_mcmquest` 0xD62, GLOB `GPP_COMBO1..4` 0x801-0x804 | parsed from the plugin |
| Key handler: script `gpp_keyhandler` on a ReferenceAlias of the control quest; int properties `GPP_KEYCODE_A1..A4` (action keys), `GPP_KEYCODE_C1..C4` (combo keys), bool `bFourthComboEnabled` | property declarations in the shipped `gpp_keyhandler.psc` |
| Defaults: A1 268 D-pad left, A2 269 D-pad right, A3 266 D-pad up, A4 267 D-pad down; C1 277 B, C2 274 LB, C3 275 RB, C4 -1 (off) | the same declarations, and the shipped preset below |
| It registers A1-A4 and C1-C3 with SKSE `RegisterForKey`, C4 only while `bFourthComboEnabled`; it sets the GPP_COMBO globals to C1-C4 and sends mod event `GPP_ComboKeysUpdated` | `RegisterKeys` in `gpp_keyhandler.psc` |
| A combo acts only while the combo key is HELD and an action key is pressed (single, double, triple press, hold) | `interface\translations\Gamepad++_ENGLISH.txt` (`$gpp_mcm_com_txt_combosingle` ...) |
| Numbers are SKSE Input Script codes: 266-281 the pad buttons, -1 none | the values above |

## Preset files (data, JSON through JContainers)

- `Data\Gamepad++\Presets\<name>.gpp` (written by its MCM Save Preset, so usually in MO2's Overwrite); only `.gpp`
  is listed by Load. The shipped `gpp_default_doNotDelete.gppd` is used by Reset only.
- Shape: `{"Actions":[...], "Combo1".."Combo4":[...], "General":[...], "version":2}`. `Actions` and `ComboN` are 16
  pairs of `int key, string label` (-1 and "" when empty) - the keyboard key each press kind mimics, with its label.
- `General` is 12 ints: A1, A2, A3, A4, C1, C2, C3, C4, bFourthComboEnabled, bExtControlsEnabled (unused since 1.1),
  bMultiTapEnabled, bLongPressEnabled. The shipped default: `268, 269, 266, 267, 277, 274, 275, -1, 0, 0, 0, 0`.
- UVC does NOT read presets: a preset is only what was saved, the running game holds what is in force. It reads the
  key handler's properties through the Papyrus VM instead (no Papyrus interface or API is exposed by Gamepad++).

## Its control map

Gamepad++ ships `interface\controls\pc\controlmap.txt`, a full replacement: in Gameplay it takes the D-pad away from
the game (Favorites on B + LS, Hotkeys keyboard only, Quick Inventory RB + LS, ...), which leaves the D-pad free for its
four action keys. Whichever control map wins in the load order, UVC reads the live map as always.

## What UVC does

- `gpp::AtDataLoaded`: one info line, found or not. Without the plugin nothing else runs.
- `gpp::Refresh`: after a load / new game and at each journal open, read the key handler's properties (debug line per
  value); before the script is bound, the GPP_COMBO globals for the combo keys only.
- Controls page: eight listed `[Functions]` rows made at runtime (never saved to the INI), shown on the controller
  page while their slot has a registered button. Selecting one: "Gamepad++ uses this button. Change it in Gamepad++'s
  own menu."
- Refusal (the plainer of warn / refuse, as the page already refuses a reserved button): a capture on the sheet onto an
  ACTION key is refused with "Gamepad++ uses that button. Change it in Gamepad++ first.", unless the row already holds
  that button (pressing it unbinds, as on every row). Combo keys are listed, not refused.
- Controller graphic: each registered slot as a mod line on its button in Gameplay, Swimming and On Horseback.
- DevBench: `uvc.control op=gamepadpp [refresh=1]`.

Not covered: requests from other mods (decided at data load, before a save says which buttons Gamepad++ uses), and a
remap through the game's own list when UVC's sheet is not drawn.
