# ApocryphaUnbindControls - changelog

Rule 61: this mod's own history, kept beside the code it describes.

<!-- VERSIONING-RULES -->
> **Versioning rules (CLAUDE.md rules 6 and 48):** `X.Y.Z`; a change increments the THIRD
> number; at `.9` the MINOR rolls. The next number is LAST WORKING + 1; failed/scratch/
> untested numbers are reused. Numbers come from version-gate.ps1.

## 1.0.0 - 2026-09-13 - untested

### Added
- First build. Every vanilla control in every input context can be left with no key on the keyboard, the mouse or the gamepad independently, from the mod's page in the Apocrypha Menu Framework (tabs General, Gameplay, Menus, Console and Debug). The unbinding is done in the running game against the control map (the engine's own 0xFF "unmapped" value), re-applied when a game loads and whenever the journal closes, and never touches controlmap.txt on disk.
- The original key of every unbound control is remembered in the mod's INI, so a control can be given its key back from the page at any time, even after a restart.
- DevBench tool uvc.control: state, dump, rows, unbind, rebind, apply, reload, restore, strings.
- Eleven languages for the page's own text; control names come from the game's own translation.
- Ships with the keyboard shortcuts for screens the Tween Menu already opens left unbound: Journal, Quick Inventory, Quick Magic, Quick Map, Quick Stats and Wait (Tween Menu Overhaul with its Wait add-on offers every one of them). The Tween Menu key itself stays. Restore defaults returns to this list; the key each had is captured from the running game, so a player's own remap is what comes back.
