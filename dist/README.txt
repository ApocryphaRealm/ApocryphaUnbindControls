Unbind Vanilla Controls
Version 1.1.1

Skyrim's own Controls menu can only move a control to another key. It cannot leave a control with no
key at all. This mod can: every control listed in its INI has no key while you play, so pressing the
key it had does nothing and the key is free for something else.

WHAT IT UNBINDS OUT OF THE BOX
Keyboard: Journal, Quick Inventory, Quick Magic, Quick Map, Quick Stats, Wait, Favorites, Quicksave,
Quickload, Auto-Move and Toggle Always Run.
Controller: Wait (Back) and Journal (Start).
Every other control keeps its key, Toggle POV included. A menu action the game ties to an unbound
control keeps its key.

SYSTEM TAB ON THE CONTROLLER
Skyrim gives the System tab no controller button. This mod puts it on Start: pressing Start opens the
journal on the System tab, the same as Esc on the keyboard. Move it to another button in the game's
Controls menu and the INI remembers the new button. The Tween Menu still opens the journal's quests.

SYSTEM TAB ROWS
The rows of the journal's System tab can be hidden from the INI. Out of the box Quicksave, Installed
Content, Creations and Help are hidden. A hidden row is only skipped by the list: the list stays
centred, the highlight never lands on it, and every other row does exactly what it did before.
Works with the game's own journal and journals built on it, such as SkyUI's. Quest Journal Overhaul -
Entire Journal Redesigned hides System rows from its own MCM settings, so this mod leaves that journal
alone.

THE CONTROL MAP (INSTALLER CHOICE)
The installer offers Controlmap.txt Fixed and Cleaned - Updated by DEEJMASTER333 and others, with
System Tab on Start and Journal off the controller already in it. It unlinks the game's menu controls
from gameplay keys, so changing controls no longer breaks menus.
- Skyrim Special Edition 1.5.97: the pre-1.6.1130 build.
- Anniversary Edition 1.6.1170: the 1.6.1130+ build.
- Another mod provides your control map: choose None. The mod still gives System Tab the Start button.
The wrong build crashes the game at startup. Before the first launch with a new control map, close the
game and delete ControlMap_Custom.txt from the Skyrim folder (beside SkyrimSE.exe; with Mod Organizer 2
and Root Builder, also check overwrite\Root). After that the mod keeps the file away itself (see below).

CHANGING THE LISTS
There is no menu. Open SKSE\Plugins\UnbindControls.ini.
[Unbound] - one Context|Control|Device line per control, for example
    Gameplay|Sneak|keyboard
Delete a line, or put ; in front of it, and that control has its key again the next time you start
the game.
[Bound] - one Context|Control|Device|Button line gives a control a key or button, for example
    Gameplay|Pause|gamepad|Start
Controller buttons can be written by name (A, B, X, Y, LB, RB, LT, RT, Start, Back, LeftStick,
RightStick, DPadUp, DPadDown, DPadLeft, DPadRight). A button another control already has is not taken.
[SystemMenu] - one Row=1 or Row=0 line per System tab row: 1 shows it, 0 hides it. The rows are
Quicksave, Save, Load, Installed Content, Creations, Settings, Mod Configuration, Controls, Help and
Quit.
bEnabled=0 turns the [Unbound] and [Bound] lists off. The INI explains the names.

In the game's Controls menu (Journal > System > Controls) an unbound control's row shows no key.
Pressing a control's own key again when the menu asks for a key unbinds it on that device; giving an
unbound control a key there keeps that key.

MENU BUTTONS HAVE THEIR OWN KEYS
The game's control map gives many menu buttons no key of their own: they borrow a gameplay control's key
(Charge Item uses Wait's, every menu's Cancel uses Tween Menu's and Pause's, Favorite in the inventory
uses Toggle POV's and Jump's). Moving or unbinding that control used to move or empty the menu button
with it. This mod gives each of those menu buttons a key of its own, in its own menu - the key the
control map gives it out of the box - so changing a gameplay control never changes a menu button.

THE CONTROLS PAGE
Journal > System > Controls is laid out as a sheet that fits on one screen: GAMEPLAY across two columns,
then EXTRA CONTROLS (the rows this mod adds), then one column per menu - MENUS, ITEMS and IN MAP. Move
with the D-pad, the arrow keys or the mouse; the sheet slides sideways to keep the selection in view.
Select a row and press Accept (or click it), then press the new key. A key another gameplay control uses
is swapped with it, the way the game does it; a key the game reserves is refused. Hold the Modifier (Left
Trigger on a controller) and press a button to bind a combination, shown as two key pictures.
Each menu button has its own row: Charge Item under ITEMS, Make Legendary, Cancel and Run in Menus under
MENUS, the map's zoom under IN MAP. Favorite has two rows: Favorites Menu (GAMEPLAY - opens the menu) and
Favorite Item (ITEMS - favourites the highlighted item), which starts on F / Y (triangle) and can be moved
like any other. The Favorites menu, lockpicking, follower and journal buttons keep their own keys but get
no row: they only repeat the MENUS ones.

YOUR CONTROLS STAY IN THE MOD'S OWN FILE
Every change you make in the Controls menu is written into this mod's INI: a control left with no key
goes into [Unbound], a control given another key goes into [Bound], and a control put back on the
game's own key loses its line. The game's ControlMap_Custom.txt is removed when you close the journal,
and one left over from before is moved into the INI the next time the game starts. With Mod Organizer 2
the INI is saved inside this mod's folder, so nothing lands in overwrite. bKeepRemapsInIni=0 in [General]
lets the game keep its own ControlMap_Custom.txt instead.

WHAT IT CHANGES
The mod writes the engine's own "unmapped" value into the control map while the game runs, and
re-applies its lists when a game loads, on a new game and whenever the journal closes. Hidden System
rows are marked for the list's own filter while the journal is open. The only file it places is the
control map you pick in the installer (Interface\Controls\PC\controlmap.txt). With bKeepRemapsInIni=1 it
writes your Controls-menu changes into its own INI and removes the game's ControlMap_Custom.txt.

REMOVING THE MOD
With bKeepRemapsInIni=1 (the default) the mod removes ControlMap_Custom.txt, so removing the mod gives
every key back; your changes stay in the INI for a reinstall. With bKeepRemapsInIni=0, whenever you
change anything in the game's Controls menu, the game saves its whole control map,
including the keys this mod left empty, to ControlMap_Custom.txt. That file stays after the mod is
removed, so those controls keep no key and drop out of the Controls list. To get every key back, close
the game, delete ControlMap_Custom.txt from the Skyrim Special Edition game folder (beside SkyrimSE.exe;
with Mod Organizer 2 and Root Builder, also check overwrite\Root) and launch the game again.

REQUIREMENTS
SKSE64 and Address Library for SKSE Plugins. Skyrim SE 1.5.97 and AE 1.6.1170.

DEBUGGING
Send the log for any bug you find:
Documents\My Games\Skyrim Special Edition\SKSE\UnbindControls.log

WHAT CHANGED

Version 1.1.1
Fixed the Favorite button in the inventory and magic menus showing [???] instead of a key. It borrowed Toggle POV's key on the keyboard and Jump's on the controller, so moving or unbinding either control left it with none. It is now a control of its own - F on the keyboard and Y (triangle) on the controller out of the box - and you can move it on the Controls page (Favorite Item).
Changed every menu button the game's control map ties to a gameplay control into a mapping of its own, in its own menu. Charge Item, every menu's Cancel, the inventory's Equip and Drop, the map's zoom and movement and the rest keep the key the control map gives them out of the box, whatever you do to the gameplay controls - Cancel keeps both Tab and Esc, Charge Item keeps T even with Wait unbound.
Changed the Controls page into a sheet that fits on one screen: GAMEPLAY across two columns, then EXTRA CONTROLS, then MENUS, ITEMS and IN MAP. The D-pad, the arrow keys or the mouse move the selection, the sheet slides sideways to keep it in view, and a key combination is drawn as two key pictures.
Added rebinding for every row on the sheet, with the game's own rules: a key another gameplay control uses is swapped with it, a key the game reserves is refused, and holding the Modifier (Left Trigger on a controller) then pressing a button binds that combination.
Added a row for each menu button - Charge Item under ITEMS, Make Legendary, Cancel and Run in Menus under MENUS, the map's zoom under IN MAP. Favorite has two rows: Favorites Menu (opens the menu) and Favorite Item (favourites the highlighted item). The Favorites menu, lockpicking, follower and journal buttons keep their own keys but get no row - they only repeat the MENUS ones.
Added a Back Pocket row, for Back Pocket For Controller's key.
Fixed controls bound with a modifier dropping off the Controls page, and added rows naming a key in words ("Escape") instead of showing its key picture.
Fixed saving the INI dropping the last field of the Power Attack and Stance rows.
Added an Address Library check at startup: a missing Address Library file gets a message naming it, and the mod stays inactive instead of failing silently.

Version 1.1.0
The shipped controller layout is the author's own, and Toggle POV is on D-pad up. Vanilla gives the Favorites control both D-pad up and down on the controller and will not let anything else take them, which is why the POV rebind was refused in the Controls menu; Favorites is now unbound on the controller (the wheel from Wheeler - Refined has taken that menu's place, on its own D-pad-down hold) and Toggle POV takes D-pad up. Also shipped: Tween Menu on Back, Shout on RT, Right Attack/Block on RB, Left Attack/Block on LB, Sprint on B. Every line is in the INI's [Bound] section and changes in the Controls menu as before.
The DLL, its INI, its log and the translation files lose the Apocrypha prefix: UnbindControls.dll, UnbindControls.ini, UnbindControls.log. Your old ApocryphaUnbindControls.ini is not read - copy your [Unbound] and [Bound] lines across once, or set them again in the Controls menu.
Both build lines are in the installer now (SE 1.5.97 / AE 1.6.1170, and Skyrim 1.7.x), beside the control-map choice.
Version 1.0.9
Fixed the added Controls-page rows appearing even when the mod they set the key for is not installed. A row now names a file whose presence means its target mod is there - the shipped rows look for One Click Power Attack, Stances NG and Wheeler - and a row whose mod is missing is left off the Controls page entirely and writes nothing, so no settings file is created for a mod you do not have.
Version 1.0.8
Added extra rows on the game's own Controls page for functions the game has no control for, set and cleared there like any other control ([Functions] in the INI). Shipped rows: Power Attack, Stance and Wheel Menu.
Added a Modifier row: one designated button per device that any bound control can require, so moving it moves every combination at once.
Added a [Remappable] list that lets the game list and rebind controls it normally refuses to, such as the attack controls on a controller.
Added a bind being able to require a modifier held down, shown on the Controls page as "Left Shift + Q".
Added the mod's own rows keeping whatever key the target mod is already set to, so installing it changes nothing until you bind a row.
Changed bound controls to be applied together, so two controls can trade keys instead of blocking each other.
Fixed added rows having no key picture and showing a dollar sign in front of their name.

Version 1.0.7
Added keeping your Controls menu changes in the mod's own INI ([Unbound] and [Bound]) instead of the game's ControlMap_Custom.txt, which is moved into the INI and removed (bKeepRemapsInIni).

Version 1.0.6
Added hiding System tab rows from the INI ([SystemMenu], Row=1 or Row=0); Quicksave, Installed Content, Creations and Help are hidden out of the box.

Version 1.0.5
Added System Tab on the controller's Start button; it always opens the System tab, the way Esc does on the keyboard, and it can be moved to another button in the Controls menu.
Added an installer choice of control map: Controlmap.txt Fixed and Cleaned - Updated (pre-1.6.1130 or 1.6.1130+ build) with System Tab on Start and Journal off the controller, or keep your own.
Added a [Bound] section to the INI that gives a control a key or button, including one the game gives no button.
Changed the shipped list to the author's picks: Journal, Quick Inventory, Quick Magic, Quick Map, Quick Stats, Wait, Favorites, Quicksave, Quickload, Auto-Move and Toggle Always Run on the keyboard, and Wait and Journal on the controller.
Fixed a bound control's new button in the Controls menu being put back to the old one when the journal closed.

Version 1.0.4
Changed the shipped list to the Tween Menu shortcuts only: Journal, Quick Inventory, Quick Magic, Quick Map, Quick Stats and Wait on the keyboard, and Wait on the controller.
Fixed menu actions tied to an unbound control losing their key; Charge Item in the inventory keeps T.

Version 1.0.3
Fixed favorites being unreachable: the Favorites key is no longer in the shipped unbound list, so it stays bound.

Version 1.0.2
Fixed the Controls menu showing a key for an unbound control; its row is now blank.
Added unbinding a control in the Controls menu by pressing the key it already has.
Changed the shipped list to fifteen controls on the keyboard and controller.

Version 1.0.0
Added the first release: the controls listed in the INI have no key, starting with the six shortcuts the Tween Menu already covers.

LICENCE
GPL-3.0-or-later: LICENSE and NOTICE.md. Notices of included
components, and the control map's permission ("I don't care what you do with this") and credits
(DEEJMASTER333 and others; DavidJCobb, mistaabushido, Hawkbar): THIRD_PARTY_NOTICES.md.
Source: https://github.com/ApocryphaRealm/ApocryphaUnbindControls
