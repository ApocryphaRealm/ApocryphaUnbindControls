#pragma once

// Unbind Vanilla Controls - core. The game's ControlMap holds, per input context and per device,
// an array of {user event, key} mappings kept SORTED by key (the engine's button -> event lookup
// is a binary search on inputKey). "Unbound" is the engine's own value for an unmapped control:
// inputKey 0xFF (controlmap.txt: "A value of 0xff means the event is unmapped for this device").
//
// This module sets that value on the live map for every entry in its list, remembers the key it
// replaced so the control can be given back, re-sorts the device array after every edit, and
// re-applies whenever the engine could have rewritten the map (a game load, the journal closing -
// the Controls menu lives there and its Reset to defaults reloads the map). Nothing on disk changes.
//
// Every function that touches the ControlMap runs on the game's main thread (queue through
// SKSE::GetTaskInterface()). The page and the DevBench tool read the snapshot, never the map.

#include <cstdint>
#include <memory>
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
		std::string event;              // the user event id, e.g. "Tween Menu"
		int device = 0;                 // 0 keyboard, 1 mouse, 2 gamepad
		std::vector<KeyPair> original;  // what the map held before the unbind, in array order
	};

	// Contexts and devices, by the names the INI and the tool use.
	int ContextCount();                              // 17, or 18 on AE 1.6.1130+ (Marketplace)
	const char* ContextName(int a_context);          // "" when out of range
	int ContextIndex(std::string_view a_name);       // -1 when unknown (case-insensitive)
	const char* DeviceName(int a_device);
	int DeviceIndex(std::string_view a_name);        // -1 when unknown

	// The list (thread-safe copies).
	std::vector<Entry> GetEntries();
	void SetEntries(std::vector<Entry> a_entries);   // from the INI; does not apply
	bool IsUnbound(int a_context, std::string_view a_event, int a_device);

	// Main thread only.
	bool Unbind(int a_context, std::string_view a_event, int a_device, std::string& a_why);
	bool Rebind(int a_context, std::string_view a_event, int a_device, std::string& a_why);
	void ApplyAll(const char* a_reason);   // every entry -> 0xFF (no-op per entry when already so); honours settings::general::enabled
	void RestoreAll(const char* a_reason); // every entry -> its originals; the list is kept
	void ClearAll();                       // RestoreAll, then forget the list
	void RebuildRows();                    // refresh the page snapshot from the live map
	void Install();                        // the journal-close sink; call at kDataLoaded

	// The page snapshot.
	struct Cell
	{
		bool exists = false;     // the device has a mapping for this event with a real key (or we unbound it)
		bool unbound = false;    // in our list
		std::uint16_t key = 0xFF;
		std::string keyName;     // the game's button name for the live key, or the remembered original when unbound
	};
	struct Row
	{
		int context = 0;
		std::string event;
		std::string label;       // the game's translated name ($event), or event when it has none
		bool warn = false;       // Pause / Journal / Console: unbinding can leave no way to open the system menu
		Cell cell[3];
	};
	std::shared_ptr<const std::vector<Row>> GetRows();

	// For the tool.
	std::string DumpJson(int a_context);   // -1 = every context
	std::string RowsJson(int a_context);
	std::string StateJson();
}
