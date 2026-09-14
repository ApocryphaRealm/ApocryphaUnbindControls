# ApocryphaUnbindControls - changelog

Rule 61: this mod's own history, kept beside the code it describes.

<!-- VERSIONING-RULES -->
> **Versioning rules (CLAUDE.md rules 6 and 48):** `X.Y.Z`; a change increments the THIRD
> number; at `.9` the MINOR rolls. The next number is LAST WORKING + 1; failed/scratch/
> untested numbers are reused. Numbers come from version-gate.ps1.

## 1.0.2 - 2026-09-14 - untested

### Fixed
- The game's Controls list (Journal > System > Controls) now lists every control this mod unbinds, with an empty key column, on the keyboard and on the controller (the owner: "for both the controller and the keyboard I want the rows to be shown but blank in the key area"). The game itself leaves a keyless control out of the list, and a vanilla-style journal draws an 'unknown key' badge for a row with no key. After each frame of the journal (the Journal Menu's AdvanceMovie is wrapped) the missing rows are put back in controlmap.txt order, and the key art of any row whose control has no key on the device being shown is hidden before the frame is drawn; no journal code is called, so any journal built on the vanilla or SkyUI list works. The device is read from the button names the game sent. DevBench uvc.control gains op=rows and state.controlsList. (Two earlier builds of 1.0.2 were never kept: one forwarded the list's row function through a call path and crashed the game when the journal opened; one hid Start and Back on the controller list although they were still bound.)

## 1.0.0 - 2026-09-13 - untested

### Added
- First build. The vanilla controls listed in the INI's `[Unbound]` section (one `Context|Control|Device` line each) have no key while the game runs: the engine's own 0xFF "unmapped" value is written into the live control map, the device list is re-sorted, and the list is re-applied at data load, game load, new game and whenever the journal closes. Nothing in the game's control files changes; deleting a line gives that control its key back at the next start.
- Ships with the keyboard shortcuts for screens the Tween Menu already opens unbound: Journal, Quick Inventory, Quick Magic, Quick Map, Quick Stats and Wait (Tween Menu Overhaul with its Wait add-on offers every one of them). The Tween Menu key itself stays.
- No settings menu (the owner: the functionality is too simple to need one). DevBench tool uvc.control: state, dump, unbind, rebind, apply, reload.
