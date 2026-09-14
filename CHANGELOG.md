# ApocryphaUnbindControls - changelog

Rule 61: this mod's own history, kept beside the code it describes.

<!-- VERSIONING-RULES -->
> **Versioning rules (CLAUDE.md rules 6 and 48):** `X.Y.Z`; a change increments the THIRD
> number; at `.9` the MINOR rolls. The next number is LAST WORKING + 1; failed/scratch/
> untested numbers are reused. Numbers come from version-gate.ps1.

## 1.0.2 - 2026-09-14 - untested

### Fixed
- The game's Controls list (Journal > System > Controls) now draws an unbound control's row with no key. It used to show a leftover badge instead - an 'unknown key' code such as 0xff or '???' in a vanilla-style journal, or the raw button name in a SkyUI-style one. After each frame of the journal, the visible rows of the list are checked and the key art of an unbound control's row is hidden before the frame is drawn (the Journal Menu's AdvanceMovie is wrapped); only visibility is changed and no journal code is called, so any journal built on either style works. The control map itself is handled as before. DevBench uvc.control gains op=rows (every row of the Controls list with what the game sent and whether it is drawn blank) and state.controlsList. (A first build of 1.0.2 forwarded the list's row function through a call path and crashed the game when the journal opened; it was never kept.)

## 1.0.0 - 2026-09-13 - untested

### Added
- First build. The vanilla controls listed in the INI's `[Unbound]` section (one `Context|Control|Device` line each) have no key while the game runs: the engine's own 0xFF "unmapped" value is written into the live control map, the device list is re-sorted, and the list is re-applied at data load, game load, new game and whenever the journal closes. Nothing in the game's control files changes; deleting a line gives that control its key back at the next start.
- Ships with the keyboard shortcuts for screens the Tween Menu already opens unbound: Journal, Quick Inventory, Quick Magic, Quick Map, Quick Stats and Wait (Tween Menu Overhaul with its Wait add-on offers every one of them). The Tween Menu key itself stays.
- No settings menu (the owner: the functionality is too simple to need one). DevBench tool uvc.control: state, dump, unbind, rebind, apply, reload.
