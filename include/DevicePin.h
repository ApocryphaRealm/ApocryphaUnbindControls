#pragma once

// Unbind Vanilla Controls 1.6.0 - the Controls page's section stays put (the owner, 2026-11-10: "you have to manually change
// which control map you're altering in the control area, so that you can actually press a keyboard key while on the gamepad
// section without it automatically switching to the keyboard section").
//
// The game on its own never switches between controller and keyboard on a key press; Auto Input Switch (Parapets, MIT) does:
// its input sink on BSInputDeviceManager flips its "using the controller" answer on the device touched last and refreshes
// every menu, so a key pressed on the controller section flipped the page to the keyboard section (the owner, 2026-11-11: "the
// UVC issue is with auto input switch, not with the game").
//
// So while UVC's Controls page is open, Auto Input Switch's sink is taken off the game's input source (found by its vtable
// lying in AutoInputSwitch.dll) and put back the moment the page closes. The game and every other mod still get every input
// event. Square on the controller (X on Xbox) or the switch at the top right of the page hands Auto Input Switch ONE event of
// the other device - to it alone, never to the game - so it switches section and refreshes the menus as it always does.
// No engine code is patched. Without Auto Input Switch the page follows the game's device and there is no section switch.
//
// 2026-11-11: the first form of this (commit d9afd58) chained a call hook after Auto Input Switch's own six-byte call at
// 67320+0xD and crashed SE 1.5.97 at the first loading screen: CommonLibSSE-NG's write_call<6> returns the ADDRESS of the
// indirect call's 8-byte pointer slot (nextOp + disp32), not the function stored in it, and that slot was called as code.

#include <string>

namespace devicepin
{
	// kInputLoaded: notes the using-controller check's address (called, never patched) and makes the two switch events.
	void Install();
	// Every journal frame: whether UVC's Controls page is the one shown. Opening stands Auto Input Switch down; closing restores it.
	void OnPage(bool a_open);
	// Square / the page's switch: the other section. False when Auto Input Switch is not standing down for the page.
	bool Toggle(const char* a_why);
	// Auto Input Switch's sink was found (the page then offers the section switch).
	bool Available();
	bool Pinned();
	std::string StateJson();
}
