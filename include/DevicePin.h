#pragma once

// Unbind Vanilla Controls 1.6.0 - the Controls page's section stays put (the owner, 2026-11-10: "you have to manually change
// which control map you're altering in the control area, so that you can actually press a keyboard key while on the gamepad
// section without it automatically switching to the keyboard section").
//
// The game's Controls page lists the controller's bindings or the keyboard's according to its "is the controller in use"
// check (BSInputDeviceManager's using-gamepad question, SE 67320 / AE 68622 / 443396 from 1.6.1130). Auto Input Switch
// answers that check with the device touched last and refreshes every menu when it changes, so a key pressed on the
// controller section flipped the page to the keyboard section. While UVC's Controls page is open, this answers the check
// with the section the player chose (it starts on what the game said when the page opened); Square on the controller (X on
// Xbox) or the switch at the top right of the page changes it. The moment the page closes the check is the game's (or Auto
// Input Switch's) again and every menu is refreshed. Outside the page nothing here changes anything.
//
// The hook: the call inside the check (vanilla "48 8B 01 FF 50 38", the gamepad device's IsEnabled; Auto Input Switch writes
// a six-byte indirect call there) is replaced with a call to this mod, which passes the question on to whatever was there.

#include <string>

namespace devicepin
{
	// kInputLoaded (after Auto Input Switch's own hook, which it writes at plugin load).
	void Install();
	// Every journal frame: whether UVC's Controls page is the one shown. Opening pins the section shown; closing lets go.
	void OnPage(bool a_open);
	// Square / the page's switch: the other section. False when the hook is not there (the page then follows the game).
	bool Toggle(const char* a_why);
	bool Hooked();
	bool Pinned();
	std::string StateJson();
}
