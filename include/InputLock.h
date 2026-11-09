#pragma once

// Unbind Vanilla Controls 1.7.0 - the input-mode lock and controller-to-key lines (the owner, 2026-11-09: "make it so that you
// have to press a button to toggle between controller and keyboard, and make it so that keyboard buttons can be bound to
// controller buttons").
//
// [InputLock]: the game's "is the controller in use" question (BSInputDeviceManager's using-gamepad check, which the button
// art, the cursor and every menu's platform ask; Auto Input Switch answers it by the device last touched) is answered by this
// mod from the mode the player chose, and only a toggle changes it. The other device either still works (Pass) or is dropped
// (Ignore).
// [PadToKey]: a controller button (optionally with a held modifier) presses a keyboard key, spliced into the event list the
// game dispatches, so the game and every SKSE mod's input sink see the key.
//
// Both live in one call hook on the PollInputDevices dispatch (the site InputBench and the Apocrypha framework use). No
// BSInputEventQueue: its singleton is laid out for another runtime in this CommonLibSSE-NG build (past mistakes 2631, 7352).

#include <string>

namespace inputlock
{
	// Reads [InputLock] and [PadToKey] from the INI. Called at plugin load (before the hooks) and by the DevBench reload.
	void Load(const std::string& a_iniPath);
	// kInputLoaded: the dispatch hook, and the using-gamepad answers when the lock is on.
	void Install();
	// Flips the mode as the toggle does (DevBench op=inputlock toggle=1).
	void Toggle(const char* a_why);
	std::string StateJson();
}
