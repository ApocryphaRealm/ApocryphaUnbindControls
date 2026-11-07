#pragma once

// Unbind Vanilla Controls - Gamepad++ compatibility (1.5.0; the owner, 2026-11-06: "I'd rather add compatibility for Gamepad++ so
// that it can interact properly and know what it's doing, than try to replace it").
//
// Gamepad++ (Nexus 27007, Gamepad++.esp) is a Papyrus mod. It never edits the control map: its key handler script registers raw
// gamepad buttons with SKSE (RegisterForKey) and acts on single, double, triple presses and holds of them. So the control map UVC
// works on cannot show which buttons it took. This module asks Gamepad++ itself, read only:
//   - the quest gpp_controlquest (Gamepad++.esp 0x800) holds a reference alias with the script gpp_keyhandler; its properties
//     GPP_KEYCODE_A1..A4 (action keys) and GPP_KEYCODE_C1..C4 (combo keys) are the buttons it registers, and C4 only while
//     bFourthComboEnabled is on. The values live in the save, so they are read after a game loads and whenever the journal opens
//     (the player may have changed them in Gamepad++'s menu since).
//   - with no bound script yet (before the quest starts), the four globals GPP_COMBO1..4 (Gamepad++.esp 0x801-0x804), which
//     Gamepad++ keeps equal to the combo keys, are read instead; the action keys are then unknown and reserve nothing.
// The numbers are the SKSE Input Script numbering (266-281 the pad buttons, -1 none); here they become the pad codes the control
// map uses. Nothing of Gamepad++'s is written, and no file of it is read.
//
// What UVC does with them: each button Gamepad++ uses is listed on the Controls page (GAMEPLAY) and in the controller graphic as
// Gamepad++'s. A bind made on the Controls page onto one of its ACTION keys is REFUSED with a note naming Gamepad++ (a control
// already on that button can still be unbound by pressing it). Its COMBO keys are listed but not refused: Gamepad++'s own text
// (Gamepad++_ENGLISH.txt) has a combo act only while the combo key is HELD and an action key is pressed, so a combo key is a
// modifier shared with the game's own control on that button (B, LB and RB out of the box), not a button it takes.
// Without Gamepad++.esp loaded nothing here runs beyond one log line.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gpp
{
	inline constexpr int kSlotCount = 8;  // A1-A4, C1-C4

	struct Slot
	{
		const char* id = "";     // "A1" .. "C4", for the log and the tool: never translated
		const char* row = "";    // the row's name on the Controls page, e.g. "Gamepad++ Action 1" (a translation key with '$')
		int skse = -1;           // the value read, SKSE numbering; -1 none
		std::uint16_t pad = 0xFF;  // the pad code (0x0004 D-pad left ...); 0xFF none
		bool registered = false;   // Gamepad++ listens to it (C4 only while its fourth combo key is on)
	};

	void AtDataLoaded();                  // the one info line: found or not. Main thread
	void Refresh(const char* a_reason);   // reads the buttons again; main thread. Does nothing without Gamepad++
	bool Found();                         // Gamepad++.esp is loaded
	std::vector<Slot> Slots();            // a copy, any thread
	std::uint16_t SlotPad(int a_slot);    // 0xFF when that slot reserves nothing
	const char* SlotRow(int a_slot);      // "" out of range
	int SlotOfRow(std::string_view a_row);  // -1 when a_row is not one of Gamepad++'s rows
	// The ACTION slot (0-3) that reserves this pad code, -1 when none. a_code is the control map's pad code. Combo keys never reserve.
	int SlotFor(std::uint32_t a_code);
	std::string StateJson();              // DevBench op=gamepadpp: the members, no braces
}
