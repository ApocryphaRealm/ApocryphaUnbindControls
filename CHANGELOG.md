# ApocryphaUnbindControls - changelog

Rule 61: this mod's own history, kept beside the code it describes.

<!-- VERSIONING-RULES -->
> **Versioning rules (CLAUDE.md rules 6 and 48):** `X.Y.Z`; a change increments the THIRD
> number; at `.9` the MINOR rolls. The next number is LAST WORKING + 1; failed/scratch/
> untested numbers are reused. Numbers come from version-gate.ps1.

## 1.0.0 - 2026-09-13 - untested

### Added
- First build. The vanilla controls listed in the INI's `[Unbound]` section (one `Context|Control|Device` line each) have no key while the game runs: the engine's own 0xFF "unmapped" value is written into the live control map, the device list is re-sorted, and the list is re-applied at data load, game load, new game and whenever the journal closes. Nothing in the game's control files changes; deleting a line gives that control its key back at the next start.
- Ships with the keyboard shortcuts for screens the Tween Menu already opens unbound: Journal, Quick Inventory, Quick Magic, Quick Map, Quick Stats and Wait (Tween Menu Overhaul with its Wait add-on offers every one of them). The Tween Menu key itself stays.
- No settings menu (the owner: the functionality is too simple to need one). DevBench tool uvc.control: state, dump, unbind, rebind, apply, reload.
