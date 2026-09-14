#pragma once

// Unbind Vanilla Controls - the game's Controls list (Journal > System > Controls). A control this mod has unbound
// has no key, and the list would still draw one for it: the vanilla-style journal attaches an "unknown key" badge
// with the key's hex code or "???", a SkyUI-style journal prints whatever button name the game sent. The owner
// (2026-09-14): "unbind vanilla buttons needs to leave the unbound button entry blank on the controls area of the
// pause menu".
//
// How: every time the journal opens, the Controls list's SetEntry (the function that draws one row) is replaced on
// the list INSTANCE by a native function. It forwards the call to the class's own SetEntry through
// `<list>.__proto__.SetEntry.call(list, clip, entry)` - reading the ActionScript function out into C++ does not
// work in this engine (GetVariable returns no functions; AMF's System row found that first) - and then hides the
// row's key art when the row's control is unbound. The art members of both journal families are covered
// (`ButtonArt` vanilla, `buttonArt` SkyUI), so any journal built on either draws the row blank. A probe call proves
// the forward reaches the class function before anything is replaced; if it does not, the list is left exactly as
// the game draws it.
//
// Every function here runs on the game's main (UI) thread.

#include <string>

namespace controlslist
{
	void OnJournalOpen();    // from the MenuOpenCloseEvent sink
	void OnJournalClose();

	// For the DevBench tool (main thread): the live rows of the Controls list and what this mod decided for each.
	std::string RowsJson();
	std::string StateJson();  // "controlsList":{...} member, no braces around it
}
