#pragma once

// Unbind Vanilla Controls - the game's Controls list (Journal > System > Controls). A control this mod has unbound
// has no key, and the list would still draw one for it: the vanilla-style journal attaches an "unknown key" badge
// with the key's hex code or "???", a SkyUI-style journal prints whatever button name the game sent. The owner
// (2026-09-14): "unbind vanilla buttons needs to leave the unbound button entry blank on the controls area of the
// pause menu".
//
// How: the Journal Menu's AdvanceMovie (IMenu vfunc 5) is wrapped. After the movie has advanced - so after every row
// the list redrew this frame, whatever redrew it - the visible row clips of the Controls list (`Entry0`..,
// `iMaxItemsShown` of them, each carrying its `itemIndex` into `EntriesA`) are read, and the key art of a row whose
// control is unbound is hidden before the frame is drawn. A row clip reused for a bound control gets its art back.
// The game leaves a keyless control out of the list entirely, so the controls the INI list unbinds on the device
// family being shown (keyboard and mouse, or gamepad - read from the button names the game sent) are first put back
// into the list's entries as rows with no key, in controlmap.txt order (the owner: "for both the controller and the
// keyboard I want the rows to be shown but blank in the key area"). A row is blank when its control has no key on the
// shown family on the live map.
// Only reads and visibility changes: no ActionScript function is called, because invoking one by a path through a
// function object (`X.__proto__.SetEntry.call`) crashed the game in 1.0.2's first build (logic library).
// Both journal families' art members are covered (`ButtonArt` vanilla, `buttonArt` SkyUI).
//
// Every function here runs on the game's main (UI) thread, except StateJson.

#include <string>

namespace controlslist
{
	void Install();          // the AdvanceMovie wrap; call once at plugin load
	void OnJournalOpen();    // from the MenuOpenCloseEvent sink
	void OnJournalClose();

	// For the DevBench tool (main thread): the live rows of the Controls list and what this mod decided for each.
	std::string RowsJson();
	std::string StateJson();  // "controlsList":{...} member, no braces around it
}
