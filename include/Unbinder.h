#pragma once

// Unbind Vanilla Controls - core. The game's ControlMap holds, per input context and per device, an array
// of {user event, key} mappings kept SORTED by key (the engine's button -> event lookup is a binary search
// on inputKey). "Unbound" is the engine's own value for an unmapped control: inputKey 0xFF (controlmap.txt:
// "A value of 0xff means the event is unmapped for this device").
//
// The list comes from the INI. ApplyAll sets that value on the live map for every entry, remembers the key
// it replaced (for this session only - the game rebuilds the map at every start), re-sorts the device array,
// and is re-run whenever the engine could have rewritten the map: a game load, a new game, the journal
// closing (the Controls menu lives there). Nothing on disk in the game changes.
//
// Every function that touches the ControlMap runs on the game's main thread.

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace unbinder
{
	struct KeyPair
	{
		std::uint16_t key = 0xFF;
		std::uint16_t modifier = 0xFF;
	};

	struct Entry
	{
		std::string context;            // the context's name (ContextName), e.g. "Gameplay"
		std::string event;              // the user event id, e.g. "Journal"
		int device = 0;                 // 0 keyboard, 1 mouse, 2 gamepad
		std::vector<KeyPair> original;  // captured from the live map at the first apply this session
	};

	int ContextCount();                              // 17, or 18 on AE 1.6.1130+ (Marketplace)
	const char* ContextName(int a_context);          // "" when out of range
	int ContextIndex(std::string_view a_name);       // -1 when unknown (case-insensitive)
	const char* DeviceName(int a_device);
	int DeviceIndex(std::string_view a_name);        // -1 when unknown

	std::vector<Entry> GetEntries();
	void SetEntries(std::vector<Entry> a_entries);   // from the INI; does not apply
	// The shipped list (the owner's own picks, 2026-09-14): Journal, Quick Inventory, Quick Magic, Quick Map, Quick Stats,
	// Wait, Favorites, Quicksave, Quickload, Auto-Move and Toggle Always Run on the keyboard; Wait and Journal on the gamepad
	// (Start opens the System tab instead, see DefaultBinds). Matches the shipped INI's [Unbound] lines.
	std::vector<Entry> DefaultEntries();

	// [Bound]: a control GIVEN a key or button on a device, including one controlmap.txt gives nothing there and the
	// Controls menu cannot remap there (System Tab, the Pause control, has no gamepad button and is not
	// gamepad-remappable). Applied after the unbinds on every apply; a key another control on that device holds is not
	// taken, and on the keyboard AMF's reserved keys are refused.
	struct Bind
	{
		std::string context;    // e.g. "Gameplay"
		std::string event;      // e.g. "Pause"
		int device = 0;         // 0 keyboard, 1 mouse, 2 gamepad
		std::uint16_t key = 0xFF;
		// The key that must be HELD for this bind to fire. 0 = none - never 0xFF: the engine's
		// button -> user event lookup matches (inputKey, modifier) exactly and a press searches with
		// modifier 0, so 0xFF here makes the mapping unreachable (logic library, 2026-09-14).
		// controlmap.txt writes the same thing as "0x0009+0x0100" (modifier+key).
		std::uint16_t modifier = 0;
	};
	std::vector<Bind> GetBinds();
	void SetBinds(std::vector<Bind> a_binds);        // from the INI; does not apply
	// The shipped binds (the owner, 2026-09-14: "add a system tab button to the controller control layout and unbind
	// the journal button"): Gameplay|Pause|gamepad|Start. Matches the shipped INI's [Bound] lines.
	std::vector<Bind> DefaultBinds();
	// A button name (gamepad: A, B, X, Y, LB, RB, LT, RT, Start, Back, LeftStick, RightStick, DPadUp/Down/Left/Right)
	// or a number (0x0010); 0xFF when neither. ButtonName: the gamepad name for a code, "" when it has none.
	std::uint16_t ParseButton(std::string_view a_text, int a_device);
	const char* ButtonName(std::uint16_t a_key, int a_device);
	// The Controls menu's remap watch (main thread; the caller saves). UpdateBind: a bound control was given another key
	// by the player - its bind now holds that key (true when a bind changed). RemoveBind: the bind is dropped (the player
	// unbound the control by pressing its own key).
	bool UpdateBind(int a_context, std::string_view a_event, int a_device, std::uint16_t a_key);
	bool RemoveBind(int a_context, std::string_view a_event, int a_device);

	// Main thread only.
	bool Unbind(int a_context, std::string_view a_event, int a_device, std::string& a_why);  // adds to the list
	bool Rebind(int a_context, std::string_view a_event, int a_device, std::string& a_why);  // removes, gives the key back
	void ApplyAll(const char* a_reason);   // honours settings::general::enabled
	void RestoreAll(const char* a_reason); // every entry -> its captured key; the list is kept
	// The game's Controls list shows either the keyboard AND mouse keys or the gamepad buttons, for the Gameplay context.
	// KeylessOnFamily: a_event has at least one mapping on that family and every one is 0xFF on the live map; a_order
	// gets its indexInContext (the order controlmap.txt lists it in). OrderInContext: that index on any device, -1 when
	// the event is not in Gameplay. ListedKeylessOnFamily: the INI list's Gameplay controls for that family that are
	// keyless right now (empty when the list is switched off).
	struct KeylessControl
	{
		std::string event;
		int order = -1;
	};
	bool KeylessOnFamily(std::string_view a_event, bool a_gamepad, int* a_order = nullptr);
	int OrderInContext(std::string_view a_event);
	std::vector<KeylessControl> ListedKeylessOnFamily(bool a_gamepad);
	// Gameplay context helpers for the Controls list's remap watch (main thread). LiveKeys: every key a_event has on
	// a_device right now, in array order. IsListed: the INI list holds Gameplay|a_event|a_device. Forget: remove that
	// line from the list WITHOUT touching the live map (a key the player just gave the control stays); the caller saves.
	std::vector<std::uint16_t> LiveKeys(std::string_view a_event, int a_device);
	bool IsListed(std::string_view a_event, int a_device);
	bool Forget(int a_context, std::string_view a_event, int a_device);
	// Remaps owned by the INI (1.0.7; the owner, 2026-09-15: "include the control map file in the mod and instead of it
	// generating a file each time, it writes back to our mods included file and nothing goes to the overwrite"). The game
	// saves a Controls-menu remap in ControlMap_Custom.txt in its working folder, which MO2's Root Builder syncs into
	// overwrite\Root. With bKeepRemapsInIni=1 this mod records the change in its own [Unbound] and [Bound] lists instead -
	// MO2 writes the INI back into this mod's folder - and removes that file. Main thread only.
	//   SnapshotGameplay: every Gameplay control's live keys per device.
	//   RecordChanges: controls whose keys differ from a_before get INI lines (no key -> [Unbound]; another key -> [Bound];
	//     back to the controlmap.txt key -> both lines dropped). Returns the lines changed; the caller saves.
	//   ImportLiveRemaps: the same for every Gameplay control with no INI line whose live keys differ from controlmap.txt -
	//     what a ControlMap_Custom.txt loaded at startup changed. The caller saves.
	//   OwnRemapsAtDataLoad: when bKeepRemapsInIni=1, bEnabled=1 and the file exists - import, save, remove.
	struct GameplaySnapshot
	{
		std::vector<std::string> events;
		std::vector<std::array<std::vector<std::uint16_t>, 3>> keys;  // parallel to events: keyboard, mouse, gamepad
	};
	GameplaySnapshot SnapshotGameplay();
	int RecordChanges(const GameplaySnapshot& a_before, const char* a_reason);
	int ImportLiveRemaps(const char* a_reason);
	std::uint16_t DefaultKey(std::string_view a_event, int a_device);  // 0xFF when controlmap.txt gives none
	std::string CustomMapPath();                                        // "" when the working folder is unknown
	bool RemoveCustomMap(const char* a_reason);                         // true when a file was removed
	void OwnRemapsAtDataLoad();
	void Install();                        // the journal open/close sink; call at kDataLoaded

	// For the DevBench tool.
	std::string DumpJson(int a_context);   // -1 = every context
	std::string StateJson();               // "unbinder":{...} member, no braces around it
}
