Unbind Vanilla Controls
Version 1.6.0

Skyrim's own Controls menu can only move a control to another key. It cannot leave a control with no
key at all. This mod can: every control listed in its INI has no key while you play, so pressing the
key it had does nothing and the key is free for something else.

WHAT IT UNBINDS OUT OF THE BOX
Keyboard: Journal, Quick Inventory, Quick Magic, Quick Map, Quick Stats, Wait, Quicksave, Quickload,
Auto-Move and Toggle Always Run.
Controller: Wait (Back) and Journal (Start).
Every other control keeps its key. Favorites stays on Q, and on the controller it is on D-pad down, so
that D-pad up can be Toggle POV. A menu action the game ties to an unbound control keeps its key.
If you use Wheeler on Q or the D-pad instead of the Favorites menu, the INI has the author's two
Favorites lines ready to switch on, with what to change beside them.

FOR MOD AUTHORS: ASKING FOR A KEY
A mod can ask this mod for a key instead of shipping one of its own, so the player sets it on one page
with everything else, and two mods cannot land on the same key without anyone knowing.
Copy include/UVCRequestAPI.h from this mod's source into your plugin. Nothing to link: it finds this
mod's exports at run time, and with this mod absent IsAvailable() is false and your plugin carries on.
  const auto uvc = UVCRequestAPI::GetAPI();
  const auto a = uvc.Request("My Mod", "MyMod Dash", "Dash", "Gameplay", UVCRequestAPI::kGamepad, 0x4000, 0x0002);
That asks for X on the controller, or D-pad down if X is taken, for an action of your own in the
Gameplay context. The press reaches your input sink as a button event whose user event is "MyMod Dash".
Also: Release takes the key back and removes the row, Query reads what an action has now.
- Contexts: Gameplay, Swimming, Horseback, Boat, and the game's others (Menu Mode, Map, Item Menus ...).
- Keys: the codes controlmap.txt uses, per device. No modifier combinations in this version.
- A user event of the game's own works too, on a device where it has no key (Hotkey3, say).
- The answer: granted, alternative, playerChoice, refused, queued, released, notRequested, badRequest
  or displaced, the key, and a sentence saying why, naming what holds the key.
- The player always wins. A key a control, one of this mod's rows or another mod's request holds is not
  given. An accepted request is a row on the Controls page, "Dash (My Mod)", under MOD REQUESTS or in
  the SWIMMING, HORSEBACK or BOAT block. What the player sets there stays in the INI's [ModRequests], above
  whatever you ask next, and if they later give your key to something else your answer says displaced.
- Ask at every game start, from kDataLoaded onward on the main thread. Earlier or from another thread the
  answer is queued: it is decided at the next chance and Query gives the result.
- Everything happens in the game's control map in memory. controlmap.txt on disk is never edited.
There is no Papyrus route in this version.

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

In the game's Controls menu (Journal > System > Controls) an unbound control's row shows no key, and
every unbound control keeps its row, so it can always be bound again. Pressing a control's own key
again when the menu asks for a key unbinds it on that device; giving an unbound control a key there
keeps that key, and only that key. A control the game gives two buttons (Favorites: D-pad up and down)
keeps just the one you press.

MENU BUTTONS HAVE THEIR OWN KEYS
The game's control map gives many menu buttons no key of their own: they borrow a gameplay control's key
(Charge Item uses Wait's, every menu's Cancel uses Tween Menu's and Pause's, Favorite in the inventory
uses Toggle POV's and Jump's). Moving or unbinding that control used to move or empty the menu button
with it. This mod gives each of those menu buttons a key of its own, in its own menu - the key the
control map gives it out of the box - so changing a gameplay control never changes a menu button.

THE CONTROLS PAGE
Journal > System > Controls shows one block at a time: GAMEPLAY (with the rows this mod adds), SWIMMING, HORSEBACK, BOAT, then
one block per menu - MENUS, ITEMS and IN MAP. LB and RB change the block and go round, as do PgUp and PgDn or
a click on the title at the top (its left end goes back). Up and down (the D-pad, the arrow keys or the mouse wheel) walk the
block's rows; left and right move along a line. Which block of how many is at the top right.
Select a row and press Accept (or click it), then press the new key. A key another gameplay control uses
is swapped with it, the way the game does it; a key the game reserves is refused. Hold the Modifier (Left
Trigger on a controller) and press a button to bind a combination, shown as two key pictures.
Each menu button has its own row: Charge Item under ITEMS, Make Legendary, Cancel and Run in Menus under
MENUS, the map's zoom under IN MAP. Favorite has two rows: Favorites Menu (GAMEPLAY - opens the menu) and
Favorite Item (ITEMS - favourites the highlighted item), which starts on F / Y (triangle) and can be moved
like any other. The Favorites menu, lockpicking, follower and journal buttons keep their own keys but get
no row: they only repeat the MENUS ones.
The sheet measures the journal it is drawn in and fits inside its frame: as many lines as the frame's height holds, as many
columns as its width holds. It works on the game's own journal (no SkyUI needed), on SkyUI's and on Norden UI's.

CONTROLLER BUTTON PICTURES
The pictures are the journal's own - the game's journal and SkyUI's carry Xbox, PlayStation 3 and
PlayStation 5 buttons. GamepadFaces in the INI's [Controls] section picks them, and the controller
graphic's pad too (see THE CONTROLLER GRAPHIC):
  Auto         the Xbox buttons, even when a UI mod's gamepad.txt (such as Norden UI's) names them
               PlayStation, unless the game says a PlayStation 5 pad is in use
  Xbox         always the Xbox buttons
  PlayStation  always the PlayStation buttons (the PS5 set when the game says a PS5 pad is in use)
  Steam        the Steam Controller in the graphic; the page draws Xbox buttons
A button with no picture in the chosen set keeps its usual one; a button with no picture at all is
written as its name. It changes only the pictures: your controls, gamepad.txt and the control map are
never touched. Norden UI's journal draws its own button art and keeps it whatever this says.

THE CONTROLLER GRAPHIC
On the Controls page press X (Square on a PlayStation pad) and a rendered controller is shown, with a
line from each button to a box that says what the button does now, read from this mod's bindings each
time it opens - what you set on the page is what you see.
- One context at a time: Gameplay, Swimming, On Horseback, In a Boat, Map, then the menus. LB and RB (or D-pad
  left and right, PgUp and PgDn, or a click on the name at the top) step through them and go round.
  A combination shows its modifier: "L2 + Shout".
- Three pads, each a picture of its own: PlayStation, Xbox and the 2026 Steam Controller.
  GamepadFaces in [Controls] picks one, and a value picked there always wins. Auto goes by the pad
  Steam says is connected, then the type in this game's Steam Input config, then what the game says.
  Steam's files are only read, and only to draw this.
- Trackpads have boxes of their own on the Steam Controller and the PlayStation touchpad: what Steam
  Input has them doing. The game binds no touchpad, so with nothing to read the box says so.
- B, Circle, X again or Esc closes it. While it is up nothing reaches the page beneath.
- Mods' keys that this mod has a row for are listed. Keys a mod keeps to itself are not.

ROWS FOR OTHER MODS' KEYS
GAMEPLAY also holds rows for keys that other mods keep in their own settings. Binding one there
writes the key into that mod's settings file, so the player sets it once, on the game's own page. Each
row shows only while its mod is installed, is named after it, and at first shows the key that mod
already uses:
  Call Horse / Horse Inventory  One key, listed in GAMEPLAY and in HORSEBACK. Simplest Horses (hold: call; tap
                at your mount or while riding: its inventory; any other tap: follow / wait), Press H to Horse
                (tap at your horse: its inventory; tap on foot aimed at nothing: call)
  Dodge         TK Dodge RE / TK Dodge NG, TK Dodge RE Addon, TK Dodge AIO, Dodge - MCO (DMCO),
                Dodge for All
  Power Attack  One Click Power Attack, BFCO, BFCO NG, Simple Power Attack (Power Attack, Left Power
                Attack and Dual Power Attack rows)
  Target Lock   True Directional Movement (also its Target Lock Extension), Simple Lock-On
  Scavenger Sense  Sense Sweep (Y and A out of the box), Trail Key, Mark, Show Trails, Wipe Trails
  Also: Stance (Stances NG), Wheel Menu (Wheeler)
Back Pocket (Back Pocket for Controller) is in ITEMS, Map Selector in IN MAP.
On a controller, a Target Lock (True Directional Movement) button also stops the lock following the
Toggle POV button, and clearing the row gives it back to Toggle POV. Binding Dodge (DMCO) turns its
dodge-on-Sprint off; clearing the row turns it back on. BFCO NG and Dodge for All keep their keys in
Input Manager, so those rows set them through Input Manager (they show only while it is installed): BFCO
NG's row sets Input Manager's "Power Attack" action (pick that one as the power attack in BFCO NG's own
menu; the log warns if BFCO NG is set to another action), and Dodge for All's
row moves the step, and the roll and dash with it while they share its key. A mod uses the new key when
it next reads its settings: One Click Power Attack when the journal closes, True Directional Movement and
Dodge - MCO when their own MCM page closes, Input Manager at once, Simple Power Attack at the NEXT GAME
START (it reads its INI only then - and its own "Save to INI" button writes back the keys it started
with), the others at the next game load. Mods that keep their key in the save (the original TK Dodge SE,
The Ultimate Dodge Mod) cannot be set from here.

YOUR CONTROLS STAY IN THE MOD'S OWN FILE
Every change you make in the Controls menu is written into this mod's INI: a control left with no key
goes into [Unbound], a control given another key goes into [Bound], and a control put back on the
game's own key loses its line. The game's ControlMap_Custom.txt is removed when you close the journal,
and one left over from before is moved into the INI the next time the game starts. With Mod Organizer 2
the INI is saved inside this mod's folder, so nothing lands in overwrite. bKeepRemapsInIni=0 in [General]
lets the game keep its own ControlMap_Custom.txt instead.

GAMEPAD++
With Gamepad++ installed, this mod works alongside it. Gamepad++ does not use the control map: it listens
to controller buttons of its own choosing. This mod reads which ones from Gamepad++ in the running game,
after a game loads and each time the journal opens, and changes nothing of Gamepad++'s.
- The Controls page (controller page) lists them at the end of GAMEPLAY: Gamepad++ Action 1-4 and
  Gamepad++ Combo 1-4, each with its button. They are set in Gamepad++'s own menu, not here.
- The controller graphic lists them in Gameplay, Swimming and On Horseback.
- Binding a control on the Controls page to one of Gamepad++'s action buttons is refused with the note
  "Gamepad++ uses that button. Change it in Gamepad++ first." A control already on that button can still
  be unbound by pressing it. Combo buttons are not refused: Gamepad++ uses them only held together with
  an action button, so they stay shared with the game's own controls.
Without Gamepad++ nothing here changes.

SPRINT OR SNEAK FOR CONTROLLER
With Sprint or Sneak for Controller installed, the controller's Sprint button also crouches and stands
(hold it while standing still). This mod shows that wherever Sprint is:
- The Controls page (controller page) lists "Sneak (hold while still) (Sprint or Sneak)" at the end of
  GAMEPLAY with Sprint's button. It is a listed row: rebind Sprint to move it.
- The controller graphic puts "Sneak (hold while still)" on Sprint's button, in Gameplay.
Without Sprint or Sneak for Controller nothing here changes.

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

IN A BOAT
The BOAT block on the Controls page holds buttons that apply only while you row, steer or sit in a boat
([Boat] in the INI). Every row starts on its on-foot button, so nothing changes until you set one. Walking
about on a ship's deck is on foot; swimming keeps its own SWIMMING block. Which boats count is set in
[BoatDetection]: Oars and Sails' rowboat (at the oars or the helm) and our Longship out of the box, any
seat or mount from a plugin listed there or carrying a keyword listed there, and no boat mod is required.
A boat mod that reads the game's own controls follows the BOAT block too: in Oars and Sails, Forward, Back
and the strafe keys row and steer, Activate stands you up and Auto-Move rows on.

WHAT CHANGED

Version 1.6.0
Added a BOAT block on the Controls page and "In a Boat" in the controller graphic: buttons of their own while
you row, steer or sit in a boat (see IN A BOAT). With no [Boat] lines nothing changes.
Added Sprint or Sneak for Controller compatibility: its crouch button is shown on Sprint's button.

Version 1.5.0
Added Gamepad++ compatibility (see GAMEPAD++): its buttons are listed on the Controls page and in the
controller graphic, and a bind onto one of its action buttons is refused with a note naming it.

Version 1.4.0
Added the controller graphic on the Controls page (X, or Square): a rendered PlayStation, Xbox or Steam
Controller pad with a box beside each button saying what it does now, one context at a time, LB and RB
to step through them (see THE CONTROLLER GRAPHIC). It was being made as a separate add-on, UVC
Controller Layout; it is part of this mod now, so there is nothing else to install.
Added Steam to GamepadFaces in [Controls]: the graphic draws the Steam Controller, the page Xbox buttons.

Version 1.3.0
Added a SWIMMING block on the Controls page: buttons that apply only while you are in water ([Swimming] in the INI). Everything goes back the moment you are out of the water.
Added a HORSEBACK block on the Controls page: its own control map while you are mounted ([Horseback] in the INI). It lists what a rider can use, menus included, and leaves out Sneak and Shout, whose keys are free while you ride once the block has a line. Everything goes back the moment you dismount.
Added horse keys of other mods to the HORSEBACK block: Call Horse (Simplest Horses, Press H to Horse) moved here and can be set; HorsePower's Pet Horse can be set; Better Third Person Selection's hold to dismount and tap to use, HorsePower's horse attack and Convenient Horses' charge are listed, each only while that mod is there.
Changed the Controls page to scroll top to bottom: every block is reached with up and down, no sideways sliding.
Added an IN MAP block that is always listed: the world map's own buttons, bound for the map alone.
Added a Map Selector row in IN MAP for World Map Selector for Controller.

Version 1.2.0
Fixed the Controls page running below the frame on SkyUI's journal: the last GAMEPLAY rows (Sprint,
Shout, Sneak) now stay inside it, at least three columns show at once, and it works on the game's own
journal without SkyUI.
Added GamepadFaces in the INI's [Controls] section: Xbox or PlayStation button pictures on the Controls
page, whatever the game's button names say (see CONTROLLER BUTTON PICTURES).
Added rows for Scavenger Sense: Sense Sweep, Trail Key, Mark, Show Trails and Wipe Trails.
Added rows on the Controls page for Call Horse, Dodge, Power Attack and Target Lock that set the key of
whichever supported mod you use (see ROWS FOR OTHER MODS' KEYS).
A row for a mod that uses MCM Helper now shows that mod's default key before you have changed it in its
MCM, instead of no key.

Version 1.1.3
Fixed Favorites coming back on D-pad up AND down after a restart when you had set it to D-pad up only.
The game gives Favorites both buttons; your choice is now kept exactly as you set it.
Changed Favorites to stay bound out of the box: Q on the keyboard, D-pad down on the controller (D-pad
up stays Toggle POV). The author's Wheeler setup, which unbinds it, is now an optional pair of lines in
the INI. An INI you already have keeps its own lines: to get Favorites back, delete the two Favorites
lines in its [Unbound] section, or bind Favorites Menu again on the Controls page.
Fixed an unbound control having no row on the Controls page when something other than this mod's list
took its key. Favorites now also has a row on the controller page.
Fixed the Favorite Item row (ITEMS, the button that favourites the highlighted item) missing from the
Controls page with the installer's control map, so the Favourite button could not be moved. It now
has its row on the keyboard and the controller page whichever control map you use.

Version 1.1.2
The log is written at info out of the box (uLogLevel=2), with more detail available by lowering it in the INI.

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
