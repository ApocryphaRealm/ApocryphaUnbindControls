#pragma once

// Unbind Vanilla Controls - the request API, version 1. Copy this one header into your plugin; nothing to link. It finds
// UnbindControls.dll's exports at run time, so your plugin loads and runs the same with Unbind Vanilla Controls absent: GetAPI()
// then answers with an API whose IsAvailable() is false, and you keep whatever input you would have used without it.
// This header alone is free to copy into a mod under any licence.
//
// WHAT IT DOES. You ask for a key; UVC decides. The player's own binding always wins, a key already taken is refused or
// swapped for the free alternative you name, and the answer says why. An accepted request is a mapping on the game's live
// control map, through the same in-memory path as every other UVC binding (controlmap.txt on disk is never edited), and a row
// on the game's Controls page labelled "<label> (<plugin>)" that the player can rebind or clear. What the player sets is kept
// in UVC's INI and later requests do not override it.
//
// HOW YOU SEE THE PRESS. Name an action of your own ("MyMod Dash") or a user event the game has with no key on that device
// ("Hotkey3"). The press reaches you as an ordinary button event carrying that user event, in your input sink:
//     button->QUserEvent() == "MyMod Dash"
// So you do not need to know which key the player gave it. Each device is its own request.
//
// CONTEXT. A name from UVC's own list: "Gameplay", "Swimming", "Horseback", or an engine context ("Menu Mode", "Item Menus",
// "Inventory", "Favorites", "Map", "Stats", "Cursor", "Book", "Journal", "TFC", "Lockpicking", "Favor"...). Swimming and
// Horseback hold only while the player is swimming or mounted.
//
// KEYS. The codes controlmap.txt uses: a DirectInput scan code on the keyboard (0x25 = K), a mouse button number, an XInput
// mask on the gamepad (0x1000 = A, 0x0009 = LT, 0x000a = RT). kNoKey asks for a row with no key yet. No modifier combinations
// in version 1.
//
// WHEN TO CALL. From kDataLoaded onward, on the main thread. Earlier than UVC's own kDataLoaded, or from another thread, the
// request is kept and the answer is kQueued: it is decided at the next chance, and Query gives you the result. Ask at every
// game start; a request is not remembered between sessions, only the player's choice for it is.
//
//     const auto uvc = UVCRequestAPI::GetAPI();
//     if (uvc.IsAvailable()) {
//         const auto a = uvc.Request("My Mod", "MyMod Dash", "Dash", "Gameplay", UVCRequestAPI::kGamepad, 0x4000, 0x0002);
//         logger::info("dash: {} ({}): {}", UVCRequestAPI::StatusName(a.status), a.key, a.why);
//     }

#include <cstdint>

#if __has_include("REX/W32/KERNEL32.h")
#	include "REX/W32/KERNEL32.h"
#else
#	include <Windows.h>
#endif

namespace UVCRequestAPI
{
	inline constexpr std::int32_t kVersion = 1;
	inline constexpr std::uint32_t kNoKey = 0xFF;

	enum Device : std::int32_t
	{
		kKeyboard = 0,
		kMouse = 1,
		kGamepad = 2,
	};

	enum Status : std::int32_t
	{
		kGranted = 0,       // you have the key you asked for
		kAlternative = 1,   // the key you asked for is taken; you have the alternative you named (why names the holder)
		kPlayerChoice = 2,  // the player set this row; key is theirs (kNoKey when they cleared it)
		kRefused = 3,       // no key for you; why says what holds it. No mapping and no row
		kQueued = 4,        // too early or off the main thread; Query later
		kReleased = 5,      // released (the player's choice stays in the INI for the next request)
		kNotRequested = 6,  // Query only: nobody asked for this; key is what the control has now
		kBadRequest = 7,    // an empty name, an unknown context or device, a '|' in a name, or UVC is switched off
		kDisplaced = 8,     // you had a key; the player has since given it to something else. The row stays, with no key
	};

	inline const char* StatusName(std::int32_t a_status)
	{
		switch (a_status)
		{
		case kGranted: return "granted";
		case kAlternative: return "alternative";
		case kPlayerChoice: return "playerChoice";
		case kRefused: return "refused";
		case kQueued: return "queued";
		case kReleased: return "released";
		case kNotRequested: return "notRequested";
		case kBadRequest: return "badRequest";
		case kDisplaced: return "displaced";
		default: return "unknown";
		}
	}

	struct Answer
	{
		std::int32_t status = kBadRequest;
		std::uint32_t key = kNoKey;
		char why[256] = {};
	};

	using GetApiVersion_t = std::int32_t (*)();
	using RequestBind_t = void (*)(const char* a_plugin, const char* a_action, const char* a_label, const char* a_context, std::int32_t a_device, std::uint32_t a_key, std::uint32_t a_alternative, Answer* a_out);
	using ReleaseBind_t = void (*)(const char* a_plugin, const char* a_action, const char* a_context, std::int32_t a_device, Answer* a_out);
	using QueryBind_t = void (*)(const char* a_plugin, const char* a_action, const char* a_context, std::int32_t a_device, Answer* a_out);

	// EVERY BINDING, FOR A MOD THAT SHOWS THEM (UVC Controller Layout). One call per row UVC knows on that device, on the calling
	// thread, before ForEachBinding returns. Main thread only; from another thread there are no calls. As set, whatever the player is
	// doing now. Rows with no key are left out. The strings last only for the call.
	// block: an engine context ("Gameplay", "Map"...), or "Swimming" or "Horseback" (each whole: every Gameplay control as it is there),
	// "Modifier" (the designated modifier), or a Controls
	// page block for another mod's function ("Mod" when it has none). event: the user event; for a function, its row's name. label: the
	// row's name when it differs from the event (a requested row, a function), else empty. key and modifier: the codes controlmap.txt
	// uses; modifier 0 or kNoKey means none.
	struct BindingRow
	{
		const char* block = "";
		const char* event = "";
		const char* label = "";
		std::uint32_t key = kNoKey;
		std::uint32_t modifier = 0;
	};
	using BindingRowFn_t = void (*)(const BindingRow* a_row, void* a_user);
	using ForEachBinding_t = void (*)(std::int32_t a_device, BindingRowFn_t a_fn, void* a_user);

	struct API
	{
		GetApiVersion_t getApiVersion = nullptr;
		RequestBind_t requestBind = nullptr;
		ReleaseBind_t releaseBind = nullptr;
		QueryBind_t queryBind = nullptr;
		ForEachBinding_t forEachBinding = nullptr;  // not part of IsAvailable: an older UVC has none

		bool IsAvailable() const { return getApiVersion && requestBind && releaseBind && queryBind; }

		// a_plugin: your mod's name as the player knows it (it labels the row and names you in conflicts). a_action: the user
		// event or your own action's id. a_label: the row's name (empty: a_action). a_alternative: the key to take if a_key is
		// taken, or kNoKey for none. Asking again replaces your earlier request.
		Answer Request(const char* a_plugin, const char* a_action, const char* a_label, const char* a_context, std::int32_t a_device, std::uint32_t a_key, std::uint32_t a_alternative = kNoKey) const
		{
			Answer out;
			if (requestBind) { requestBind(a_plugin, a_action, a_label, a_context, a_device, a_key, a_alternative, &out); }
			return out;
		}
		// Takes your key back and removes the row.
		Answer Release(const char* a_plugin, const char* a_action, const char* a_context, std::int32_t a_device) const
		{
			Answer out;
			if (releaseBind) { releaseBind(a_plugin, a_action, a_context, a_device, &out); }
			return out;
		}
		// What the action has now, requested or not (any control of the game can be asked about).
		Answer Query(const char* a_plugin, const char* a_action, const char* a_context, std::int32_t a_device) const
		{
			Answer out;
			if (queryBind) { queryBind(a_plugin, a_action, a_context, a_device, &out); }
			return out;
		}
	};

	inline API GetAPI()
	{
		API api;
#if __has_include("REX/W32/KERNEL32.h")
		const auto module = REX::W32::GetModuleHandleA("UnbindControls.dll");
		const auto find = [&](const char* a_name) { return REX::W32::GetProcAddress(module, a_name); };
#else
		const auto module = GetModuleHandleA("UnbindControls.dll");
		const auto find = [&](const char* a_name) { return GetProcAddress(module, a_name); };
#endif
		if (!module) { return api; }
		api.getApiVersion = reinterpret_cast<GetApiVersion_t>(find("UVC_GetApiVersion"));
		api.requestBind = reinterpret_cast<RequestBind_t>(find("UVC_RequestBind"));
		api.releaseBind = reinterpret_cast<ReleaseBind_t>(find("UVC_ReleaseBind"));
		api.queryBind = reinterpret_cast<QueryBind_t>(find("UVC_QueryBind"));
		api.forEachBinding = reinterpret_cast<ForEachBinding_t>(find("UVC_ForEachBinding"));
		return api;
	}
}
