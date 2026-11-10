#pragma once

// Unbind Vanilla Controls 1.6.0 - controller buttons that press keyboard keys (the owner, 2026-11-09: "make it so that
// keyboard buttons can be bound to controller buttons").
//
// [PadToKey]: a controller button (optionally with a held modifier) presses a keyboard key, spliced into the event list the
// game dispatches, so the game and every SKSE mod's input sink see the key.
//
// One call hook on the PollInputDevices dispatch (the site InputBench and the Apocrypha framework use). No
// BSInputEventQueue: its singleton is laid out for another runtime in this CommonLibSSE-NG build (past mistakes 2631, 7352).
//
// Nothing here touches which device the game thinks is in use: that is left to the game and to Auto Input Switch (the
// owner, 2026-11-10: UVC "is not supposed to actually prevent auto input switch from working in game"). The game-wide
// [InputLock] of an earlier, unposted 1.6.0 build is gone; a leftover [InputLock] section in a player's INI is not read.

#include <string>

namespace padtokey
{
	// Reads [PadToKey] from the INI. Called at plugin load (before the hook) and by the DevBench reload.
	void Load(const std::string& a_iniPath);
	// kInputLoaded: the dispatch hook (with no line it passes every frame through untouched; a DevBench reload can add lines).
	void Install();
	std::string StateJson();
}
