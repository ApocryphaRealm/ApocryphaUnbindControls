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
	// The shipped list (the owner, 2026-09-13): the keyboard shortcuts for screens the Tween Menu already
	// opens - Tween Menu Overhaul with its Wait add-on offers Inventory, Magic, Map, Quests, Skills and Wait.
	std::vector<Entry> DefaultEntries();

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
	void Install();                        // the journal open/close sink; call at kDataLoaded

	// For the DevBench tool.
	std::string DumpJson(int a_context);   // -1 = every context
	std::string StateJson();               // "unbinder":{...} member, no braces around it
}
