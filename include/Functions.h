#pragma once

// Unbind Vanilla Controls - EXTRA ROWS on the game's own Controls page, for functions the game has no control for.
//
// The owner, 2026-09-16: "i want to integrate the functionality of these other mods into uvc by adding more control
// rows that can be bound and unbound and each function like shout would have 2 button fields, one for its button and
// an optional one for a modifier key ... as for seperate power attacks, oppa provides what we need so we would just
// provide the rows for a power attack and a normal attack. i dont want an amf settings page, i want this all done on
// the games control page."
//
// So this module does NOT reimplement another mod's behaviour. The other mod keeps doing its job; this mod supplies
// the BINDING: a row on the Controls page, with a button and an optional modifier, whose value is delivered to that
// mod's own settings file. One Click Power Attack is the first case - it already fires the power attack, it already
// reads a key and a modifier from its settings, and the only thing it lacks is a place to set them that a player
// would find. A vanilla "normal attack" needs no row of its own: Right Attack/Block is already on that page.
//
// A function row is not a user event, so nothing here touches the ControlMap. The row is injected into the list's
// entries like the keyless rows ControlsList already puts back, the remap is watched the same way (the page's
// bRemapMode names the row, the input sink records the key pressed), and the result is written to this mod's INI
// and to the target file. Pressing the key the row already has unbinds it, exactly as a vanilla row does.
//
// Delivery: the key is written in the SKSE "Input Script" numbering every SKSE mod's settings use - 0-255 keyboard
// DX scan codes, 256-263 mouse buttons, 266-281 gamepad buttons (SKSE::InputMap) - because that is the vocabulary
// the target file is already written in. -1 (configurable per function) means "no key".
//
// Every function here runs on the game's main thread, except StateJson.

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace functions
{
	inline constexpr std::uint16_t kUnbound = 0xFF;

	// Where a function's binding is delivered. The file is relative to Data, and is written in place: the value
	// replaces the key's line inside its section, so everything else in the target file is left exactly as it was.
	struct Target
	{
		std::string file;         // e.g. "MCM\\Settings\\OCPA.ini"
		std::string section;      // e.g. "General"
		std::string key;          // e.g. "iKeycode"
		std::string modifierKey;  // e.g. "iModifierKey"; empty when the target has no modifier setting
		int none = -1;            // what the target writes for "no key" (OCPA uses -1)
	};

	struct Binding
	{
		std::uint16_t key = kUnbound;
		std::uint16_t modifier = 0;  // 0 = none, like unbinder::Bind (see its note on 0 vs 0xFF)
	};

	struct Function
	{
		std::string name;                 // the row's text on the Controls page, e.g. "Power Attack"
		Target target;
		std::array<Binding, 3> bind{};    // keyboard, mouse, gamepad
	};

	std::vector<Function> GetFunctions();
	void SetFunctions(std::vector<Function> a_functions);  // from the INI; does not apply
	// The shipped list: one row, "Power Attack", delivering to One Click Power Attack's own settings file. It ships
	// UNBOUND on every device - a row that arrives already holding a button would take that button away from whatever
	// the player has there, and the point of the row is that the player picks.
	std::vector<Function> DefaultFunctions();

	// True when a_name is one of the function rows (the Controls list asks this once per visible row per frame, so it
	// must not copy the vector). a_index gets its position in the list.
	bool IsFunctionRow(std::string_view a_name, std::size_t* a_index = nullptr);
	// The binding a function row shows for the family the Controls page is showing. Keyboard and mouse are one family:
	// the keyboard binding is preferred, the mouse one used when the keyboard has none.
	Binding ShownBinding(std::size_t a_index, bool a_gamepad);
	// The row's key text for that family - "" when unbound, "Left Shift + Q" when it has a modifier.
	std::string ShownText(std::size_t a_index, bool a_gamepad);

	// The Controls menu's remap watch (main thread; the caller saves the INI).
	//   Bind: the player pressed a_key on a_device for this row. Returns false with a_why set when the key is refused
	//     (AMF's reserved keys on the keyboard, or a key another function row already holds).
	//   Unbind: the player pressed the key the row already had. Always succeeds.
	bool Bind(std::size_t a_index, int a_device, std::uint16_t a_key, std::string& a_why);
	bool Unbind(std::size_t a_index, int a_device);
	// True when a_key on a_device is what this row is already bound to - the "press it again to unbind" test.
	bool HoldsKey(std::size_t a_index, int a_device, std::uint16_t a_key);

	// Writes every function's binding into its target file. Called after a bind changes and at data load, so a target
	// file that was edited by hand (or by the other mod's own menu) is put back to what the Controls page shows.
	// Returns the number of target files written. Main thread only.
	int Deliver(const char* a_reason);

	// The SKSE Input Script code for a key on a device, and back. kUnbound / the target's "none" value round-trip.
	int ToInputCode(std::uint16_t a_key, int a_device);
	std::uint16_t FromInputCode(int a_code, int& a_device);

	void Install();  // resolves the Data path; call at kDataLoaded, before Deliver

	// For the DevBench tool.
	std::string StateJson();  // "functions":{...} member, no braces around it
}
