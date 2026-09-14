# ApocryphaUnbindControls - changelog

Rule 61: this mod's own history, kept beside the code it describes.

<!-- VERSIONING-RULES -->
> **Versioning rules (CLAUDE.md rules 6 and 48):** `X.Y.Z`; a change increments the THIRD
> number; at `.9` the MINOR rolls. The next number is LAST WORKING + 1; failed/scratch/
> untested numbers are reused. Numbers come from version-gate.ps1.

## 1.0.4 - 2026-09-14 - working

### Changed
- The shipped list is back to the Tween Menu set only (the owner: "update the mod to only unbind the tween menu overhaul coverd buttons and keys and leave the rest alone"): Journal, Quick Inventory, Quick Magic, Quick Map, Quick Stats and Wait on the keyboard, and Wait (Back) on the controller; Start stays on Journal. Quickload, Quicksave, Auto-Move, Toggle Always Run, Toggle POV and controller Toggle POV / Sneak keep their keys. Compiled defaults and the shipped INI match.

### Fixed
- A menu action the game links to an unbound gameplay control keeps a key (the owner chose "Keep Charge Item working"). controlmap.txt defines some menu actions as links (the inventory's ChargeItem is "!0,Wait"); once the game resolves those links against an unbound control, the action has no key too. After every apply the mod walks ControlMap::linkedMappings and gives each action linked to a control in the list that control's default key, read from Interface\Controls\PC\controlmap.txt through the game's resource system (so a controlmap replacer's key is used). The live key cannot be used: once the game has saved the control as empty, it is already 0xFF at startup.

## 1.0.3 - 2026-09-14 - working

### Fixed
- Hotfix: Favorites is no longer in the shipped unbound list, so the Favorites key (Q) stays bound on the keyboard. A user report showed that a player without the Tween Menu had no way into the Favorites menu with it unbound (the owner: "for now just add the favorites key back in as a bound key as a hotfix"). The Favorites row stays in the Controls menu as a normal bound key (the owner: "the favorites row shouldnt be removed, it should go back to being a bound key"). A player whose game already saved Favorites with no key (ControlMap_Custom.txt, written by the game after any Controls-menu change under 1.0.2) deletes that file and launches the game; no repair code (the owner: "we dont need a repair function, they can just delete the control custom txt and launch the game"). Compiled defaults and the shipped INI both drop the line; an INI saved by an earlier version keeps its own list until the player deletes that line. The wider fix (the game saving unbound keys into ControlMap_Custom.txt, and the Controls-menu crash report) is still open.

## 1.0.2 - 2026-09-14 - untested

### Fixed
- The game's Controls list (Journal > System > Controls) now lists every control this mod unbinds, with an empty key column, on the keyboard and on the controller (the owner: "for both the controller and the keyboard I want the rows to be shown but blank in the key area"). The game itself leaves a keyless control out of the list, and a vanilla-style journal draws an 'unknown key' badge for a row with no key. After each frame of the journal (the Journal Menu's AdvanceMovie is wrapped) the missing rows are put back in controlmap.txt order, and the key art of any row whose control has no key on the device being shown is hidden before the frame is drawn; no journal code is called, so any journal built on the vanilla or SkyUI list works. The device is read from the button names the game sent. In the Controls menu, pressing the key a control already has unbinds it on that device and adds it to the INI list (the owner: "pressing the same key as a function is already tied to should unbind it"); giving a key to a control the INI list unbinds removes its line, so closing the journal no longer takes the new key away. The remap is watched, not changed: the System page's bRemapMode names the row, an input-event sink records the key pressed, and the live map is compared when the remap ends. DevBench uvc.control gains op=rows and state.controlsList. state.controlsList.lastRemap says what the last remap did.

### Changed
- The shipped list is the author's own, picked in the Controls menu: Journal, Quick Inventory, Quick Magic, Quick Map, Quick Stats, Wait, Quickload, Quicksave, Favorites, Auto-Move, Toggle Always Run and Toggle POV on the keyboard; Wait, Toggle POV and Sneak on the controller. Compiled defaults and the shipped INI match. (Two earlier builds of 1.0.2 were never kept: one forwarded the list's row function through a call path and crashed the game when the journal opened; one hid Start and Back on the controller list although they were still bound.)

## 1.0.0 - 2026-09-13 - untested

### Added
- First build. The vanilla controls listed in the INI's `[Unbound]` section (one `Context|Control|Device` line each) have no key while the game runs: the engine's own 0xFF "unmapped" value is written into the live control map, the device list is re-sorted, and the list is re-applied at data load, game load, new game and whenever the journal closes. Nothing in the game's control files changes; deleting a line gives that control its key back at the next start.
- Ships with the keyboard shortcuts for screens the Tween Menu already opens unbound: Journal, Quick Inventory, Quick Magic, Quick Map, Quick Stats and Wait (Tween Menu Overhaul with its Wait add-on offers every one of them). The Tween Menu key itself stays.
- No settings menu (the owner: the functionality is too simple to need one). DevBench tool uvc.control: state, dump, unbind, rebind, apply, reload.
