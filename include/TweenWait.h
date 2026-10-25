#pragma once

// Unbind Vanilla Controls - Tween Menu Overhaul's Wait option with keyboard Wait unbound (1.2.0).
//
// Tween Menu Overhaul 1.2 (JPSteel) opens the wait menu from its tween-menu "Wait" option by looking up the KEYBOARD
// Wait key (ControlMap::GetMappedKey) and pressing it through the game's own menu-open handler once the tween menu
// closes. With Wait unbound on the keyboard - which this mod ships - it finds no key, logs "Could not resolve Wait
// key" and opens nothing (reported on our Nexus page, 2026-10-25).
//
// This module listens for the same request (the mod event "OpenMenuFromTween" with "Sleep/Wait Menu"), and only when
// keyboard Wait has no key at that moment it sends the game's own Wait to the same handler when the tween menu closes,
// so the game's own "you cannot wait now" checks still apply. With a keyboard key, Tween Menu Overhaul does it itself
// and this module does nothing - one open, never two. Nothing here runs without Tween Menu Overhaul's request.

#include <string>

namespace tweenwait
{
	void Install();             // kDataLoaded: the mod-event sink and the tween-menu close sink
	std::string StateJson();    // "tweenWait":{...} for DevBench op=state
	bool Fire();                // DevBench op=tweenwait fire: sends the same mod event Tween Menu Overhaul's menu sends
}
