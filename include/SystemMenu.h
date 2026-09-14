#pragma once

// Unbind Vanilla Controls - rows hidden from the journal's System tab. The owner (2026-09-14): "lets make it so that the
// different menu rows can be removed from the system tab from the ini, I dont want the help or creations rows", "i also
// want quick save hidden by default", then "we dont have to remove them, just hide them with the ini at visible=0". The
// [SystemMenu] INI section has one Row=1/0 line per row (Quicksave, Save, Load, Installed Content, Creations, Settings, Mod
// Configuration, Controls, Help, Quit); shipped hidden: Quicksave, Installed Content,
// Creations and Help (the owner: "I don't want to see the installed content row either").
//
// Hidden through the list's own filter. The vanilla-style System list (the game's quest_journal.swf, SkyUI's, AUO's) is a
// Shared.CenteredScrollingList: drawing, centring on the highlighted row, scrolling and up/down movement all step through
// its Shared.ListFilterer, whose EntryMatchesFilter is `filterFlag == undefined || (filterFlag & itemFilter) != 0`. A listed
// row's entry gets `filterFlag = 0`, so the list skips it everywhere and stays centred, while `entryList` and every index
// the page and the game read (presses, SetSaveDisabled, SetShowMod) stay exactly as they were. Rows the game adds later
// are new entry objects, so every journal frame (the AdvanceMovie wrap) looks; after a change the list's InvalidateData
// (a method call on the list object) redraws it. First build (moving row clips) left the list off centre and the
// highlight on nothing; the owner's test, 2026-09-14.
//
// Quest Journal Overhaul - Entire Journal Redesigned (a page with `__qjuiSystemCanonical`) hides System rows from its own
// MCM settings; that page is left alone (the owner: "we can just use the MCM ini for Quest Journal overhaul redesign to
// deactivate the system rows on that").
//
// Main thread, except GetHidden/SetHidden/TokenFor/StateJson.

#include <string>
#include <string_view>
#include <vector>

namespace systemmenu
{
	std::vector<std::string> DefaultHidden();  // Quicksave, Installed Content, Creations, Help - the shipped INI's =0 lines
	std::vector<std::string> GetHidden();
	void SetHidden(std::vector<std::string> a_rows);
	// The row's token for a row name ("Help" -> "$HELP"); "" when the name is not a System row. Creations covers both of its
	// tokens ($CREATIONS, $MOD MANAGER).
	std::string TokenFor(std::string_view a_name);

	void OnJournalOpen();
	void OnFrame(RE::GFxMovieView* a_movie);  // from the journal's AdvanceMovie wrap

	// For the DevBench tool.
	std::string RowsJson();   // main thread: every row of the System list, its filterFlag and whether a clip draws it
	std::string StateJson();  // "systemMenu":{...} member, no braces around it
}
