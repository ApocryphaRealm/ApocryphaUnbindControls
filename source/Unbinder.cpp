#include "PCH.h"

#include "Unbinder.h"

#include "ControlsList.h"
#include "Settings.h"
#include "SystemMenu.h"
#include "utils/Logger.h"

#include "REX/W32/KERNEL32.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <format>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace unbinder
{
	namespace
	{
		constexpr std::uint16_t kUnmapped = 0xFF;  // ControlMap::kInvalid - the engine's own "no key"

		std::mutex g_lock;
		std::vector<Entry> g_entries;
		std::string g_lastApply = "never";
		int g_applyCount = 0;
		int g_lastTouched = 0;
		bool g_sinkInstalled = false;
		std::vector<Bind> g_binds;
		std::vector<Remappable> g_remappable;
		std::vector<ModifierKey> g_modifiers;
		int g_lastBound = 0;
		int g_lastRemappable = 0;  // binds that changed a key at the last apply
		std::string g_lastOwn = "nothing recorded yet";  // g_lock: the latest remap written into the INI lists
		int g_ownLines = 0;                               // g_lock: INI lines changed by recorded remaps this session
		bool g_customMapRemoved = false;                  // g_lock: a ControlMap_Custom.txt was removed this session

		// XInput masks, as controlmap.txt writes gamepad buttons (Journal 0x0010 = Start, Wait 0x0020 = Back ...).
		constexpr std::pair<const char*, std::uint16_t> kPadButtons[] = {
			{ "DPadUp", 0x0001 }, { "DPadDown", 0x0002 }, { "DPadLeft", 0x0004 }, { "DPadRight", 0x0008 },
			{ "Start", 0x0010 }, { "Back", 0x0020 }, { "LeftStick", 0x0040 }, { "RightStick", 0x0080 },
			{ "LB", 0x0100 }, { "RB", 0x0200 }, { "A", 0x1000 }, { "B", 0x2000 }, { "X", 0x4000 }, { "Y", 0x8000 },
			{ "LT", 0x0009 }, { "RT", 0x000a },
		};

		// DirectInput scan codes, for NAMING a keyboard key in the log and in the Controls list. The game's own
		// controlmap.txt writes these as raw hex, so without a table a modifier reads as "0x2a" rather than
		// "Left Shift". Names are matched case-insensitively on the way in, so "lshift" and "Left Shift" both parse.
		constexpr std::pair<const char*, std::uint16_t> kKeyboardKeys[] = {
			{ "Escape", 0x01 }, { "1", 0x02 }, { "2", 0x03 }, { "3", 0x04 }, { "4", 0x05 }, { "5", 0x06 },
			{ "6", 0x07 }, { "7", 0x08 }, { "8", 0x09 }, { "9", 0x0a }, { "0", 0x0b }, { "Minus", 0x0c },
			{ "Equals", 0x0d }, { "Backspace", 0x0e }, { "Tab", 0x0f },
			{ "Q", 0x10 }, { "W", 0x11 }, { "E", 0x12 }, { "R", 0x13 }, { "T", 0x14 }, { "Y", 0x15 },
			{ "U", 0x16 }, { "I", 0x17 }, { "O", 0x18 }, { "P", 0x19 },
			{ "LeftBracket", 0x1a }, { "RightBracket", 0x1b }, { "Enter", 0x1c }, { "Left Ctrl", 0x1d },
			{ "A", 0x1e }, { "S", 0x1f }, { "D", 0x20 }, { "F", 0x21 }, { "G", 0x22 }, { "H", 0x23 },
			{ "J", 0x24 }, { "K", 0x25 }, { "L", 0x26 }, { "Semicolon", 0x27 }, { "Apostrophe", 0x28 },
			{ "Grave", 0x29 }, { "Left Shift", 0x2a }, { "Backslash", 0x2b },
			{ "Z", 0x2c }, { "X", 0x2d }, { "C", 0x2e }, { "V", 0x2f }, { "B", 0x30 }, { "N", 0x31 },
			{ "M", 0x32 }, { "Comma", 0x33 }, { "Period", 0x34 }, { "Slash", 0x35 }, { "Right Shift", 0x36 },
			{ "NumMultiply", 0x37 }, { "Left Alt", 0x38 }, { "Space", 0x39 }, { "CapsLock", 0x3a },
			{ "F1", 0x3b }, { "F2", 0x3c }, { "F3", 0x3d }, { "F4", 0x3e }, { "F5", 0x3f }, { "F6", 0x40 },
			{ "F7", 0x41 }, { "F8", 0x42 }, { "F9", 0x43 }, { "F10", 0x44 }, { "NumLock", 0x45 },
			{ "ScrollLock", 0x46 }, { "Num7", 0x47 }, { "Num8", 0x48 }, { "Num9", 0x49 }, { "NumMinus", 0x4a },
			{ "Num4", 0x4b }, { "Num5", 0x4c }, { "Num6", 0x4d }, { "NumPlus", 0x4e }, { "Num1", 0x4f },
			{ "Num2", 0x50 }, { "Num3", 0x51 }, { "Num0", 0x52 }, { "NumPeriod", 0x53 },
			{ "F11", 0x57 }, { "F12", 0x58 }, { "NumEnter", 0x9c }, { "Right Ctrl", 0x9d },
			{ "NumSlash", 0xb5 }, { "PrintScreen", 0xb7 }, { "Right Alt", 0xb8 }, { "Pause", 0xc5 },
			{ "Home", 0xc7 }, { "Up", 0xc8 }, { "PageUp", 0xc9 }, { "Left", 0xcb }, { "Right", 0xcd },
			{ "End", 0xcf }, { "Down", 0xd0 }, { "PageDown", 0xd1 }, { "Insert", 0xd2 }, { "Delete", 0xd3 },
		};

		// The context names, in the engine's index order. SE and AE before 1.6.1130 have 17; AE 1.6.1130+
		// inserts Marketplace at 16 and Favor becomes 17 (RE/U/UserEvents.h).
		constexpr const char* kNamesSE[17] = {
			"Gameplay", "Menu Mode", "Console", "Item Menus", "Inventory", "Debug Text", "Favorites", "Map",
			"Stats", "Cursor", "Book", "Debug Overlay", "Journal", "TFC", "Debug Map", "Lockpicking", "Favor"
		};
		constexpr const char* kNamesAE[18] = {
			"Gameplay", "Menu Mode", "Console", "Item Menus", "Inventory", "Debug Text", "Favorites", "Map",
			"Stats", "Cursor", "Book", "Debug Overlay", "Journal", "TFC", "Debug Map", "Lockpicking", "Marketplace", "Favor"
		};
		constexpr const char* kDevices[3] = { "keyboard", "mouse", "gamepad" };

		bool HasMarketplace()
		{
			static const bool s_value = [] {
				if (!REL::Module::IsAE()) { return false; }
				return REL::Module::get().version().compare(SKSE::RUNTIME_SSE_1_6_1130) != std::strong_ordering::less;
			}();
			return s_value;
		}

		bool IEquals(std::string_view a_a, std::string_view a_b)
		{
			if (a_a.size() != a_b.size()) { return false; }
			for (std::size_t i = 0; i < a_a.size(); ++i)
			{
				if (std::tolower(static_cast<unsigned char>(a_a[i])) != std::tolower(static_cast<unsigned char>(a_b[i]))) { return false; }
			}
			return true;
		}

		std::string EscapeJson(std::string_view a_in)
		{
			std::string out;
			out.reserve(a_in.size() + 8);
			for (const char c : a_in)
			{
				switch (c)
				{
				case '\\': out += "\\\\"; break;
				case '"': out += "\\\""; break;
				case '\n': out += "\\n"; break;
				case '\r': break;
				default: out += c; break;
				}
			}
			return out;
		}

		// The engine's array of context pointers. AE 1.6.1130+ has one more than the header's fixed 17, so
		// index through the first element rather than the declared array.
		RE::ControlMap::InputContext* Context(RE::ControlMap* a_map, int a_context)
		{
			if (!a_map || a_context < 0 || a_context >= ContextCount()) { return nullptr; }
			RE::ControlMap::InputContext** contexts = &a_map->controlMap[0];
			return contexts[a_context];
		}

		using Mappings = RE::BSTArray<RE::ControlMap::UserEventMapping>;

		Mappings* MappingsFor(RE::ControlMap* a_map, int a_context, int a_device)
		{
			auto* ctx = Context(a_map, a_context);
			if (!ctx || a_device < 0 || a_device > 2) { return nullptr; }
			return &ctx->deviceMappings[a_device];
		}

		std::vector<RE::ControlMap::UserEventMapping*> Find(Mappings& a_mappings, std::string_view a_event)
		{
			std::vector<RE::ControlMap::UserEventMapping*> out;
			for (auto& m : a_mappings)
			{
				if (m.eventID.c_str() && IEquals(m.eventID.c_str(), a_event)) { out.push_back(&m); }
			}
			return out;
		}

		// The engine's button -> event lookup (SE ID 67242) is a binary search with comparator 67264, which orders by
		// inputKey and then by modifier (CommonLibSSE's GetUserEventName only mirrors the inputKey half), so the array is
		// re-sorted the same way after any edit - key first, modifier as the tie-break.
		void SortByKey(Mappings& a_mappings)
		{
			if (a_mappings.size() < 2) { return; }
			std::stable_sort(a_mappings.begin(), a_mappings.end(),
							 [](const RE::ControlMap::UserEventMapping& a_l, const RE::ControlMap::UserEventMapping& a_r) {
								 return a_l.inputKey != a_r.inputKey ? a_l.inputKey < a_r.inputKey : a_l.modifier < a_r.modifier;
							 });
		}

		std::string Hex(std::uint16_t a_v) { return std::format("0x{:02x}", a_v); }

		std::vector<Entry>::iterator FindEntry(int a_context, std::string_view a_event, int a_device)
		{
			const char* name = ContextName(a_context);
			return std::find_if(g_entries.begin(), g_entries.end(), [&](const Entry& e) {
				return e.device == a_device && IEquals(e.context, name) && IEquals(e.event, a_event);
			});
		}

		std::string Lower(std::string_view a_in)
		{
			std::string out(a_in);
			for (auto& c : out) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
			return out;
		}

		// ---- a control's default key, from controlmap.txt ---------------------------------------------------------
		// The key controlmap.txt gives a gameplay control, read once through the game's resource system, so a loose or
		// archived controlmap replacer wins exactly as it does for the game. The live key is no use for this: once the
		// game has saved a control as empty in ControlMap_Custom.txt it is already 0xFF when the game starts.
		struct DefaultKeys
		{
			std::uint16_t key[3]{ kUnmapped, kUnmapped, kUnmapped };  // keyboard, mouse, gamepad
		};

		// "0x14"; "0x02,0x4f" (the first key); "0x2a+0x0f" (key+modifier, the key); "!0,Wait" (a link, no key of its own).
		std::uint16_t ParseKey(std::string_view a_field)
		{
			if (a_field.empty() || a_field.front() == '!') { return kUnmapped; }
			a_field = a_field.substr(0, a_field.find_first_of(",+"));
			try
			{
				return static_cast<std::uint16_t>(std::stoul(std::string(a_field), nullptr, 16));
			}
			catch (...)
			{
				return kUnmapped;
			}
		}

		const std::map<std::string, DefaultKeys>& GameplayDefaults()
		{
			static std::map<std::string, DefaultKeys> s_keys;  // lower-case event name -> keys
			static bool s_loaded = false;
			if (s_loaded) { return s_keys; }
			s_loaded = true;

			RE::BSResourceNiBinaryStream stream("Interface\\Controls\\PC\\controlmap.txt");
			if (!stream.good())
			{
				logger::warn("defaults: Interface\\Controls\\PC\\controlmap.txt could not be opened; menu actions linked to an unbound control keep the key the game gives them");
				return s_keys;
			}
			// NiBinaryStream's chunked read is protected; the public read() reports only whether the whole count arrived,
			// so the file (about 15 KB, read once) is taken a byte at a time.
			std::string text;
			char c = 0;
			while (text.size() < (1u << 20) && stream.read(&c, 1)) { text.push_back(c); }

			// The first input context is Main Gameplay; a blank line starts the next one.
			bool inGameplay = false;
			std::size_t pos = 0;
			while (pos < text.size())
			{
				std::size_t eol = text.find('\n', pos);
				if (eol == std::string::npos) { eol = text.size(); }
				std::string_view line(text.data() + pos, eol - pos);
				pos = eol + 1;
				const std::size_t first = line.find_first_not_of(" \t\r");
				if (first == std::string_view::npos)
				{
					if (inGameplay) { break; }
					continue;
				}
				line = line.substr(first);
				if (line.starts_with("//")) { continue; }
				inGameplay = true;

				std::vector<std::string_view> fields;
				std::size_t f = 0;
				while (f < line.size())
				{
					std::size_t tab = line.find('\t', f);
					if (tab == std::string_view::npos) { tab = line.size(); }
					std::string_view field = line.substr(f, tab - f);
					while (!field.empty() && (field.back() == '\r' || field.back() == ' ')) { field.remove_suffix(1); }
					if (!field.empty()) { fields.push_back(field); }
					f = tab + 1;
				}
				if (fields.size() < 4) { continue; }
				DefaultKeys keys;
				for (int d = 0; d < 3; ++d) { keys.key[d] = ParseKey(fields[1 + d]); }
				s_keys.emplace(Lower(fields[0]), keys);
			}
			logger::info("defaults: {} gameplay control(s) read from controlmap.txt", s_keys.size());
			return s_keys;
		}

		int g_lastConverted = 0;  // links turned into separate mappings at the last apply
		int g_convertedTotal = 0;  // and this session

		// controlmap.txt defines many menu actions by LINKING them to a gameplay control (`!0,Wait` - the inventory's
		// ChargeItem takes whatever key Wait has), so the engine copies the gameplay key into the menu action every time it
		// resolves the links - a saved ControlMap_Custom.txt at startup, a Controls-menu remap. Moving or unbinding a
		// gameplay control then moved or emptied menu buttons with it. The owner (2026-09-28): "convert all of the linked
		// mappings ... into context-aware separate mappings". Every link is removed and the menu action keeps a key of its
		// own in its own context: the key the linked control has BY DEFAULT on that device in the controlmap.txt in force
		// (so a controls replacer is respected), or, for a link to a control that file does not define for Gameplay, the key
		// the action has now. Runs at the start of every apply, before the [Unbound] and [Bound] lists, so a line naming a
		// menu action still wins. Caller holds g_lock.
		// Every menu action converted this session, in controlmap.txt order: the Controls page gives each its own row.
		std::vector<DelinkedAction> g_delinked;

		// The row's name on the Controls page: the menu, then the action in words, short enough that the list does not
		// shrink the text. Engine ids whose job differs from page to page keep their own name, spaced ("Skills: Y Button").
		void DelinkedNames(int a_context, std::string_view a_event, std::string& a_block, std::string& a_action)
		{
			static const std::map<std::string, std::string> contexts{
				{ "menu mode", "Menus" }, { "item menus", "Items" }, { "inventory", "Inventory" }, { "favorites", "Favorites" },
				{ "map", "Map" }, { "stats", "Skills" }, { "journal", "Journal" }, { "lockpicking", "Lockpicking" }, { "favor", "Followers" },
				{ "book", "Book" }, { "console", "Console" }, { "cursor", "Cursor" }, { "tfc", "Free Camera" }
			};
			static const std::map<std::string, std::string> actions{
				{ "item menus|ybutton", "Favorite Item" }, { "item menus|xbutton", "Drop" }, { "item menus|leftequip", "Equip Left" },
				{ "item menus|rightequip", "Equip Right" }, { "item menus|item zoom", "Inspect" }, { "inventory|chargeitem", "Charge Item" },
				{ "journal|tabswitch", "Switch Tab" }, { "lockpicking|rotatelock", "Rotate Lock" },
				// The menu copies of Run and Toggle Always Run sit beside the GAMEPLAY rows of the same name; the Skills YButton
				// is the Legendary prompt (the owner agreed the renames, 2026-09-28)
				{ "menu mode|run", "Run in Menus" }, { "menu mode|toggle always run", "Always Run in Menus" },
				{ "stats|ybutton", "Make Legendary" }
			};
			const std::string ctxName = ContextName(a_context);
			const auto c = contexts.find(Lower(ctxName));
			std::string action;
			const auto a = actions.find(Lower(ctxName) + "|" + Lower(a_event));
			if (a != actions.end()) { action = a->second; }
			else
			{
				// "XButton" -> "X Button", "ZoomIn" stays readable, names with spaces are kept as they are
				for (std::size_t i = 0; i < a_event.size(); ++i)
				{
					const auto at = [&](std::size_t a_i) { return static_cast<unsigned char>(a_event[a_i]); };
					const bool startsWord = i > 0 && std::isupper(at(i)) && a_event[i - 1] != ' ' &&
					                        (std::islower(at(i - 1)) || (i + 1 < a_event.size() && std::islower(at(i + 1)) && std::isupper(at(i - 1))));
					if (startsWord) { action += ' '; }
					action += a_event[i];
				}
			}
			a_block = c != contexts.end() ? c->second : ctxName;
			a_action = action;
		}


		void RecordDelinked(int a_context, std::string_view a_event, int a_device)
		{
			auto it = std::find_if(g_delinked.begin(), g_delinked.end(), [&](const DelinkedAction& d) { return d.context == a_context && IEquals(d.event, a_event); });
			if (it == g_delinked.end())
			{
				DelinkedAction d;
				d.context = a_context;
				d.event = std::string(a_event);
				DelinkedNames(a_context, a_event, d.block, d.action);
				d.row = d.block + ": " + d.action;
				// A block that holds the controls INSIDE a menu the GAMEPLAY rows open says so, so "Favorites Menu" (opens it)
				// and the Favorites block (Up, Down, Accept inside it) do not read as the same thing
				static const std::map<std::string, std::string> titles{
					{ "favorites", "IN FAVORITES MENU" }, { "map", "IN MAP" }, { "lockpicking", "IN LOCKPICKING" }
				};
				const auto t = titles.find(Lower(ContextName(a_context)));
				if (t != titles.end()) { d.title = t->second; }
				else
				{
					d.title = d.block;
					for (auto& c : d.title) { c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); }
				}
				// The sheet's columns (the owner, 2026-09-28): "charge item can just be added to the items column", "make
				// legendary be in menus column", and IN FAVORITES MENU, IN LOCKPICKING, FOLLOWERS and JOURNAL taken away -
				// the Favorites menu has the same controls as MENUS, and Lockpicking and Followers only a Cancel that is Back
				// anyway. A hidden row keeps its separate mapping on its default key; the row's identity (d.row) is unchanged,
				// so a bind already saved for it still matches.
				d.sheetContext = a_context;
				const std::string ctx = Lower(ContextName(a_context));
				const std::string key = ctx + "|" + Lower(a_event);
				if (key == "inventory|chargeitem")
				{
					d.title = "ITEMS";
					d.sheetContext = static_cast<int>(RE::UserEvents::INPUT_CONTEXT_ID::kItemMenu);
				}
				else if (key == "stats|ybutton")
				{
					d.title = "MENUS";
					d.sheetContext = static_cast<int>(RE::UserEvents::INPUT_CONTEXT_ID::kMenuMode);
				}
				else if (ctx == "favorites" || ctx == "lockpicking" || ctx == "favor" || ctx == "journal")
				{
					d.hidden = true;
				}
				d.fixed = false;  // Favorite Item is rebindable (2026-09-28); nothing is locked
				g_delinked.push_back(std::move(d));
				it = std::prev(g_delinked.end());
			}
			if (a_device == 2) { it->gamepad = true; }
			else { it->keyboardFamily = true; }
		}

		int ConvertLinkedMappings(RE::ControlMap* a_map, const char* a_reason)
		{
			auto& links = a_map->GetRuntimeData().linkedMappings;
			if (links.empty()) { return 0; }
			const auto& defaults = GameplayDefaults();

			// One action can be linked several times on a device ("Cancel !0,Tween Menu,!0,Pause" - Tab AND Esc), and then it
			// has one mapping per link. Grouped per (context, device, action) so each link's key lands on its own mapping.
			struct Group
			{
				int context = 0;
				int device = 0;
				std::string event;
				std::vector<std::uint16_t> keys;  // the linked controls' default keys, where controlmap.txt gives one
				std::vector<std::string> from;
			};
			std::vector<Group> groups;
			for (const auto& link : links)
			{
				if (!link.linkedMappingName.c_str()) { continue; }
				const int device = static_cast<int>(link.device);
				const int ctx = static_cast<int>(link.linkedMappingContext);
				if (device < 0 || device > 2) { continue; }
				auto g = std::find_if(groups.begin(), groups.end(), [&](const Group& a_g) {
					return a_g.context == ctx && a_g.device == device && IEquals(a_g.event, link.linkedMappingName.c_str());
				});
				if (g == groups.end())
				{
					groups.push_back({ ctx, device, link.linkedMappingName.c_str(), {}, {} });
					g = std::prev(groups.end());
				}
				const char* from = link.linkFromName.c_str() ? link.linkFromName.c_str() : "";
				g->from.emplace_back(from);
				if (static_cast<int>(link.linkFromContext) == 0)
				{
					const auto it = defaults.find(Lower(from));
					if (it != defaults.end()) { g->keys.push_back(it->second.key[device]); }
				}
			}

			int converted = 0;
			std::vector<Mappings*> dirty;
			for (auto& g : groups)
			{
				auto* target = MappingsFor(a_map, g.context, g.device);
				if (!target) { continue; }
				auto found = Find(*target, g.event);
				if (found.empty()) { continue; }
				// A mapping already on one of the keys keeps it; the keys left over go to the mappings left over. The mappings
				// of one action differ only in their key, so which one takes which key does not matter. A link to a control
				// controlmap.txt gives no Gameplay default leaves its mapping on the key it has now.
				std::vector<bool> placed(found.size(), false);
				std::vector<std::uint16_t> left;
				for (const std::uint16_t k : g.keys)
				{
					bool done = false;
					for (std::size_t i = 0; i < found.size() && !done; ++i)
					{
						if (!placed[i] && found[i]->inputKey == k) { placed[i] = true; done = true; }
					}
					if (!done) { left.push_back(k); }
				}
				std::size_t next = 0;
				for (std::size_t i = 0; i < found.size() && next < left.size(); ++i)
				{
					if (placed[i]) { continue; }
					found[i]->inputKey = left[next++];
					placed[i] = true;
				}
				std::string keysNow;
				for (auto* m : found)
				{
					m->linked = false;
					keysNow += (keysNow.empty() ? "" : ",") + Hex(m->inputKey);
				}
				std::string fromText;
				for (const auto& f : g.from) { fromText += (fromText.empty() ? "" : ",") + f; }
				++converted;
				RecordDelinked(g.context, g.event, g.device);
				if (std::find(dirty.begin(), dirty.end(), target) == dirty.end()) { dirty.push_back(target); }
				logger::debug("apply ({}): {}|{}|{} was linked to {}; now its own mapping on {}", a_reason, ContextName(g.context), g.event, DeviceName(g.device), fromText, keysNow);
			}
			links.clear();  // nothing left for the engine to resolve
			for (auto* m : dirty) { SortByKey(*m); }
			return converted;
		}

		int g_lastFavourite = 0;  // Favourite-button keys set at the last apply
		int g_favouriteLinksCut = 0;  // links to the Favourite button removed this session

		// The Favourite button (Item Menus "YButton", the inventory and magic menus' favourite prompt) has no key of its own in
		// controlmap.txt: it borrows Toggle POV's (keyboard, mouse) and Jump's (gamepad) through a link, so unbinding or moving
		// either control - through our list, the Controls menu or a saved ControlMap_Custom.txt - left it on 0xFF and SkyUI drew
		// [???]. The owner (2026-09-28): "it should be hard coded to triangle or F". The links are removed so no later
		// resolution can reach it, and its keys are set on every apply BEFORE the lists: F on the keyboard, Y / triangle on the
		// gamepad, nothing on the mouse. Real keys mean the prompt is drawn from the game's own button art, with or without a
		// UI overhaul. Later the same day, once every link was a separate mapping, the owner asked for it to be rebindable
		// ("Favorite Item" on the Controls page): a line of the player's own is applied after this and wins, so this is the
		// default, never a lock. Caller holds g_lock.
		int PinFavouriteButton(RE::ControlMap* a_map, const char* a_reason)
		{
			constexpr std::string_view kEvent = "YButton";
			constexpr int kItemMenus = static_cast<int>(RE::UserEvents::INPUT_CONTEXT_ID::kItemMenu);
			constexpr std::uint16_t kKeys[3] = { 0x21, kUnmapped, 0x8000 };  // keyboard F, mouse none, gamepad Y / triangle
			auto& links = a_map->GetRuntimeData().linkedMappings;
			for (auto it = links.begin(); it != links.end();)
			{
				if (static_cast<int>(it->linkedMappingContext) == kItemMenus && it->linkedMappingName.c_str() && IEquals(it->linkedMappingName.c_str(), kEvent))
				{
					logger::info("apply ({}): Item Menus|{}|{} was linked to {}; link removed, the key is fixed", a_reason, kEvent, DeviceName(static_cast<int>(it->device)),
								 it->linkFromName.c_str() ? it->linkFromName.c_str() : "?");
					it = links.erase(it);
					++g_favouriteLinksCut;
				}
				else { ++it; }
			}
			int set = 0;
			for (int d = 0; d < 3; ++d)
			{
				auto* mappings = MappingsFor(a_map, kItemMenus, d);
				if (!mappings) { logger::warn("apply ({}): Item Menus|{}|{} - no such device list; the Favourite key could not be fixed", a_reason, kEvent, DeviceName(d)); continue; }
				auto found = Find(*mappings, kEvent);
				if (found.empty()) { logger::warn("apply ({}): Item Menus|{}|{} - the game has no such control", a_reason, kEvent, DeviceName(d)); continue; }
				bool changed = false;
				for (auto* m : found)
				{
					if (m->inputKey != kKeys[d] || m->modifier != 0 || m->linked)
					{
						m->inputKey = kKeys[d];
						m->modifier = 0;
						m->linked = false;
						changed = true;
						++set;
					}
				}
				if (changed)
				{
					SortByKey(*mappings);
					logger::info("apply ({}): Item Menus|{}|{} set to {}", a_reason, kEvent, DeviceName(d), Hex(kKeys[d]));
				}
			}
			return set;
		}

		// AMF's reserved keys (DEFAULT-KEYS.md, runtime check 1): the framework's live menu key and its navigation keys,
		// all keyboard scan codes. Asked at every apply so the answer follows AMF's current configuration.
		bool IsAmfReserved(std::uint16_t a_key)
		{
			using func_t = std::uint32_t (*)(std::int32_t*, std::uint32_t);
			for (const char* dll : { "ApocryphaMenuFramework.dll", "SKSEMenuFramework.dll" })
			{
				const auto module = REX::W32::GetModuleHandleA(dll);
				if (!module) { continue; }
				const auto func = static_cast<func_t>(REX::W32::GetProcAddress(module, "SMF_GetReservedKeyCodes"));
				if (!func) { continue; }
				std::int32_t codes[32]{};
				const std::uint32_t count = func(codes, static_cast<std::uint32_t>(std::size(codes)));
				for (std::uint32_t i = 0; i < count && i < std::size(codes); ++i)
				{
					if (codes[i] == static_cast<std::int32_t>(a_key)) { return true; }
				}
				return false;
			}
			return false;
		}

		// A bind's modifier, with "Modifier" resolved to the device's designated row. A designated modifier that is
		// not set (0xFF) means no modifier at all rather than an unreachable mapping - 0xFF stored here can never be
		// matched by a press (logic library, 2026-09-14).
		std::uint16_t ResolvedModifier(const Bind& a_bind)
		{
			if (!a_bind.useDesignatedModifier) { return a_bind.modifier; }
			std::uint16_t designated = kUnmapped;
			for (const auto& m : g_modifiers)
			{
				if (m.device == a_bind.device) { designated = m.key; break; }
			}
			return designated == kUnmapped ? 0 : designated;
		}

		// [Bound]: give each listed control its key. Runs after the unbinds, so a key the list just freed (Journal's Start
		// on the gamepad) is free for the bind. One key, one action: a key another control on that device still holds is
		// not taken, and the holder is named. A control with no mapping on that device at all gets one, copied from its
		// mapping on another device (same event, order and flags) with the new key. Caller holds g_lock.
		// [Remappable]: set the flag the game reads when it decides which controls its Controls page will list, and
		// which buttons it will let a remap take. Caller holds g_lock. Returns the mappings changed.
		int ApplyRemappable(RE::ControlMap* a_map, const char* a_reason)
		{
			int changed = 0;
			for (const auto& r : g_remappable)
			{
				const int ctx = ContextIndex(r.context);
				auto* mappings = ctx >= 0 ? MappingsFor(a_map, ctx, r.device) : nullptr;
				if (!mappings)
				{
					logger::warn("remappable ({}): {}|{}|{} - context unknown or not loaded", a_reason, r.context, r.event, DeviceName(r.device));
					continue;
				}
				auto found = Find(*mappings, r.event);
				if (found.empty())
				{
					logger::warn("remappable ({}): {}|{}|{} - the game has no such control on that device", a_reason, r.context, r.event, DeviceName(r.device));
					continue;
				}
				for (auto* m : found)
				{
					if (m->remappable) { continue; }
					m->remappable = true;
					++changed;
					logger::info("remappable ({}): {}|{}|{} is now remappable, so the game lists it on its Controls page", a_reason, r.context, r.event, DeviceName(r.device));
				}
			}
			return changed;
		}

		// [Bound]: give each listed control its key.
		//
		// Applied as a SET, in two phases, because a list of binds is a LAYOUT and a layout contains swaps. Applying
		// one bind at a time and refusing any key another control still holds deadlocks the moment two controls trade
		// places: Shout wants RT while Right Attack/Block still has it, Right Attack/Block wants RB while Shout still
		// has it, and each is refused because the other has not moved yet. Nothing moves, and the log fills with
		// conflicts that are not conflicts (2026-09-16, porting a whole controller layout into the INI).
		//
		// So: work out every bind for a device first, treat a key held by another control THAT IS ALSO MOVING as
		// free, clear all the outgoing keys, and only then write the new ones. A key held by a control that is NOT in
		// the list is still a real conflict and is still refused, with the holder named - one key, one action.
		//
		// Caller holds g_lock.
		int ApplyBinds(RE::ControlMap* a_map, const char* a_reason)
		{
			int bound = 0;

			// Group by context+device: a swap can only happen within one device's mapping array.
			std::vector<std::pair<int, int>> groups;
			for (const auto& b : g_binds)
			{
				const int ctx = ContextIndex(b.context);
				if (ctx < 0 || b.key == kUnmapped) { continue; }
				const std::pair<int, int> g{ ctx, b.device };
				if (std::find(groups.begin(), groups.end(), g) == groups.end()) { groups.push_back(g); }
			}

			for (const auto& [ctx, device] : groups)
			{
				auto* mappings = MappingsFor(a_map, ctx, device);
				if (!mappings) { continue; }

				// The controls this device is about to move. A key one of them still holds is not taken, it is vacated.
				std::vector<const Bind*> moving;
				for (const auto& b : g_binds)
				{
					if (ContextIndex(b.context) == ctx && b.device == device && b.key != kUnmapped) { moving.push_back(&b); }
				}
				auto isMoving = [&](std::string_view a_event) {
					return std::any_of(moving.begin(), moving.end(), [&](const Bind* a_b) { return IEquals(a_b->event, a_event); });
				};

				// Phase 1 - decide. Nothing is written yet, so every decision sees the map as it was.
				std::vector<const Bind*> accepted;
				for (const auto* b : moving)
				{
					const std::uint16_t wantModifier = ResolvedModifier(*b);
					const std::string keyText = ButtonName(b->key, b->device)[0] ? std::format("{} ({})", ButtonName(b->key, b->device), Hex(b->key)) : Hex(b->key);
					if (b->device == 0 && IsAmfReserved(b->key))
					{
						logger::warn("bind ({}): {}|{}|{} -> {} refused: AMF reserves that key", a_reason, b->context, b->event, DeviceName(b->device), keyText);
						continue;
					}
					auto found = Find(*mappings, b->event);
					// Already exactly there: BOTH halves must match, or a bind that only changes its modifier is skipped.
					if (!found.empty() && found.front()->inputKey == b->key && found.front()->modifier == wantModifier) { continue; }

					const RE::ControlMap::UserEventMapping* holder = nullptr;
					for (const auto& m : *mappings)
					{
						// The same key under a DIFFERENT modifier is not a conflict - the engine tells (key, modifier)
						// pairs apart - so both halves are compared.
						if (m.inputKey != b->key || m.modifier != wantModifier) { continue; }
						const char* id = m.eventID.c_str();
						if (id && IEquals(id, b->event)) { continue; }        // itself
						if (id && isMoving(id)) { continue; }                 // a swap: that control is leaving this key
						holder = &m;
						break;
					}
					if (holder)
					{
						logger::warn("bind ({}): {}|{}|{} -> {} not applied: \"{}\" already holds it on that device", a_reason, b->context, b->event, DeviceName(b->device), keyText,
									 holder->eventID.c_str() ? holder->eventID.c_str() : "?");
						continue;
					}
					// One key, one action - including among the binds being applied together. Treating a moving
					// control's key as free is what lets two controls SWAP; it must not also let two of them LAND
					// on the same button. Seen live: clearing the designated modifier dropped Shout from LB+LT to
					// plain LB, where Left Attack/Block already was, and both were "moving" so neither blocked the
					// other. The first line in the list keeps the button; a later one is refused and says so.
					const auto clash = std::find_if(accepted.begin(), accepted.end(), [&](const Bind* a_a) {
						return a_a->key == b->key && ResolvedModifier(*a_a) == wantModifier;
					});
					if (clash != accepted.end())
					{
						logger::warn("bind ({}): {}|{}|{} -> {} not applied: \"{}\" was given it first", a_reason, b->context, b->event, DeviceName(b->device), keyText, (*clash)->event);
						continue;
					}
					accepted.push_back(b);
				}
				if (accepted.empty()) { continue; }

				// Phase 2 - vacate. Every accepted control drops its current key before any new key is written, so the
				// array never holds two controls on one (key, modifier) even for an instant.
				for (const auto* b : accepted)
				{
					for (auto* m : Find(*mappings, b->event)) { m->inputKey = kUnmapped; }
				}

				// Phase 3 - write.
				for (const auto* b : accepted)
				{
					const std::uint16_t wantModifier = ResolvedModifier(*b);
					const std::string keyText = ButtonName(b->key, b->device)[0] ? std::format("{} ({})", ButtonName(b->key, b->device), Hex(b->key)) : Hex(b->key);
					auto found = Find(*mappings, b->event);
					if (found.empty())
					{
						RE::ControlMap::UserEventMapping copy{};
						bool have = false;
						for (int d = 0; d < 3 && !have; ++d)
						{
							if (d == b->device) { continue; }
							auto* other = MappingsFor(a_map, ctx, d);
							if (!other) { continue; }
							auto theirs = Find(*other, b->event);
							if (!theirs.empty()) { copy = *theirs.front(); have = true; }
						}
						if (!have)
						{
							logger::warn("bind ({}): {}|{}|{} -> {} not applied: the game has no such control in that context", a_reason, b->context, b->event, DeviceName(b->device), keyText);
							continue;
						}
						copy.inputKey = b->key;
						// 0 = no modifier. The engine's button -> user event lookup (SE ID 67242) binary-searches this array
						// for (inputKey, modifier) with comparator 67264, which orders and matches on BOTH; a press builds its
						// search key with modifier 0, so a mapping stored with modifier 0xFF is never found and the press gets
						// no user event (1.0.5 listen test). Adversarial contest 2026-09-14.
						copy.modifier = wantModifier;
						copy.linked = false;
						mappings->push_back(copy);
						logger::info("bind ({}): {}|{}|{} had no mapping on that device; created one on {}", a_reason, b->context, b->event, DeviceName(b->device), keyText);
					}
					else
					{
						logger::info("bind ({}): {}|{}|{} -> {}", a_reason, b->context, b->event, DeviceName(b->device), keyText);
						found.front()->inputKey = b->key;
						found.front()->modifier = wantModifier;  // an existing mapping keeps its old modifier otherwise
					}
					++bound;
				}
				SortByKey(*mappings);
			}
			return bound;
		}

		// The journal is where the Controls menu lives; its Reset to defaults reloads the whole map and a
		// rebind rewrites entries, so every close re-applies the list.
		class MenuSink : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
		{
		public:
			static MenuSink* GetSingleton() { static MenuSink s; return &s; }

			RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
			{
				if (!a_event || std::string_view(a_event->menuName.c_str()) != RE::JournalMenu::MENU_NAME) { return RE::BSEventNotifyControl::kContinue; }
				if (a_event->opening)
				{
					// The Controls list draws an unbound control's row with no key (ControlsList.h).
					controlslist::OnJournalOpen();
					systemmenu::OnJournalOpen();  // [SystemMenu] rows are looked for again in this open
					return RE::BSEventNotifyControl::kContinue;
				}
				controlslist::OnJournalClose();
				if (auto* tasks = SKSE::GetTaskInterface())
				{
					tasks->AddTask([]() {
						ApplyAll("journal closed");
						// The game writes ControlMap_Custom.txt when a remap is saved; the remap is already in the INI (ControlsList).
						if (settings::general::keepRemapsInIni && settings::general::enabled) { RemoveCustomMap("journal closed"); }
					});
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};
	}

	int ContextCount() { return HasMarketplace() ? 18 : 17; }

	const char* ContextName(int a_context)
	{
		if (a_context < 0 || a_context >= ContextCount()) { return ""; }
		return HasMarketplace() ? kNamesAE[a_context] : kNamesSE[a_context];
	}

	int ContextIndex(std::string_view a_name)
	{
		for (int i = 0; i < ContextCount(); ++i) { if (IEquals(ContextName(i), a_name)) { return i; } }
		return -1;
	}

	const char* DeviceName(int a_device) { return (a_device >= 0 && a_device < 3) ? kDevices[a_device] : ""; }

	int DeviceIndex(std::string_view a_name)
	{
		for (int i = 0; i < 3; ++i) { if (IEquals(kDevices[i], a_name)) { return i; } }
		return -1;
	}

	std::vector<Entry> GetEntries()
	{
		std::scoped_lock l(g_lock);
		return g_entries;
	}

	void SetEntries(std::vector<Entry> a_entries)
	{
		std::scoped_lock l(g_lock);
		g_entries = std::move(a_entries);
		logger::debug("unbinder: list set, {} entries", g_entries.size());
	}

	std::vector<Entry> DefaultEntries()
	{
		// The owner's own list for 1.0.5 (2026-09-14, picked in the Controls menu with the fixed control map installed: "let's
		// ship it with my current unbound keys as the default"): the Tween Menu's shortcuts plus Favorites, Quicksave,
		// Quickload, Auto-Move and Toggle Always Run on the keyboard; Wait and Journal on the controller, where Start opens the
		// System tab instead (DefaultBinds). Device: 0 keyboard, 2 gamepad.
		constexpr std::pair<const char*, int> kDefaults[] = {
			{ "Journal", 0 },
			{ "Quick Inventory", 0 },
			{ "Quick Magic", 0 },
			{ "Quick Map", 0 },
			{ "Quick Stats", 0 },
			{ "Wait", 0 },
			{ "Wait", 2 },
			{ "Journal", 2 },
			{ "Favorites", 0 },
			{ "Quicksave", 0 },
			{ "Quickload", 0 },
			{ "Auto-Move", 0 },
			{ "Toggle Always Run", 0 },
		};
		std::vector<Entry> out;
		for (const auto& [event, device] : kDefaults)
		{
			Entry e;
			e.context = "Gameplay";
			e.event = event;
			e.device = device;
			out.push_back(std::move(e));
		}
		return out;
	}

	// The name the GAME gives a gamepad button, read from its own Interface\Controls\PC\gamepad.txt.
	//
	// That file - not this mod, and not a table written from memory - decides what a Controls-page row must put in
	// buttonName for its key tile to be drawn. It is also full of surprises worth honouring rather than guessing at:
	// the D-PAD entries carry NO prefix at all ("Up", "Down", "Left", "Right") while every other button does
	// ("PS3_LB", "PS3_LT"), and the prefix in the stock file is PS3_ whatever pad is plugged in, because the art set
	// is chosen further down. Guessing lowercase "lt" and assuming the d-pad had no glyph got both halves wrong
	// (the owner, 2026-09-16: "it definitely does have a art tile for the D-pad").
	//
	// Read once, through the game's resource system, so a replacer's file wins exactly as its control map does.
	const std::string& GamepadButtonName(std::uint16_t a_key)
	{
		static const std::map<std::uint16_t, std::string> s_names = [] {
			std::map<std::uint16_t, std::string> out;
			RE::BSResourceNiBinaryStream stream("Interface\\Controls\\PC\\gamepad.txt");
			if (!stream.good())
			{
				logger::warn("gamepad names: the game's gamepad.txt could not be opened; added rows will have no key tile");
				return out;
			}
			std::string text;
			char c = 0;
			while (text.size() < (1u << 16) && stream.read(&c, 1)) { text.push_back(c); }
			std::size_t pos = 0;
			while (pos < text.size())
			{
				std::size_t eol = text.find('\n', pos);
				if (eol == std::string::npos) { eol = text.size(); }
				std::string_view line(text.data() + pos, eol - pos);
				pos = eol + 1;
				const auto first = line.find_first_not_of(" \t\r");
				if (first == std::string_view::npos) { continue; }
				line = line.substr(first);
				if (line.starts_with("//")) { continue; }
				const auto gap = line.find_first_of(" \t");
				if (gap == std::string_view::npos) { continue; }
				const std::string name(line.substr(0, gap));
				const auto valueAt = line.find_first_not_of(" \t", gap);
				if (valueAt == std::string_view::npos) { continue; }
				std::string value(line.substr(valueAt));
				while (!value.empty() && (value.back() == '\r' || value.back() == ' ' || value.back() == '\t')) { value.pop_back(); }
				try
				{
					const auto code = static_cast<std::uint16_t>(std::stoul(value, nullptr, 0));
					out.emplace(code, name);
				}
				catch (...) {}
			}
			logger::info("gamepad names: {} button name(s) read from the game's gamepad.txt", out.size());
			return out;
		}();
		static const std::string empty;
		const auto it = s_names.find(a_key);
		return it == s_names.end() ? empty : it->second;
	}

	std::vector<ModifierKey> GetModifiers()
	{
		std::scoped_lock l(g_lock);
		return g_modifiers;
	}

	void SetModifiers(std::vector<ModifierKey> a_list)
	{
		std::scoped_lock l(g_lock);
		g_modifiers = std::move(a_list);
	}

	std::vector<ModifierKey> DefaultModifiers()
	{
		return { { 2, 0x0009 } };  // Left Trigger on the gamepad (the owner, 2026-09-16)
	}

	std::uint16_t ModifierFor(int a_device)
	{
		std::scoped_lock l(g_lock);
		for (const auto& m : g_modifiers)
		{
			if (m.device == a_device) { return m.key; }
		}
		return kUnmapped;
	}

	bool SetModifier(int a_device, std::uint16_t a_key)
	{
		if (a_device < 0 || a_device > 2) { return false; }
		std::scoped_lock l(g_lock);
		for (auto& m : g_modifiers)
		{
			if (m.device != a_device) { continue; }
			if (m.key == a_key) { return false; }
			m.key = a_key;
			return true;
		}
		g_modifiers.push_back({ a_device, a_key });
		return true;
	}

	std::vector<Remappable> GetRemappable()
	{
		std::scoped_lock l(g_lock);
		return g_remappable;
	}

	void SetRemappable(std::vector<Remappable> a_list)
	{
		std::scoped_lock l(g_lock);
		g_remappable = std::move(a_list);
	}

	// The gamepad attack controls. Apostasy's control map, like vanilla's, clears their gamepad remappable flag, so
	// without this the controller's Controls page has no normal-attack row for either hand and refuses their buttons
	// to any other row as "reserved".
	std::vector<Remappable> DefaultRemappable()
	{
		return {
			{ "Gameplay", "Right Attack/Block", 2 },
			{ "Gameplay", "Left Attack/Block", 2 },
		};
	}

	std::vector<Bind> GetBinds()
	{
		std::scoped_lock l(g_lock);
		return g_binds;
	}

	void SetBinds(std::vector<Bind> a_binds)
	{
		std::scoped_lock l(g_lock);
		g_binds = std::move(a_binds);
		logger::debug("unbinder: binds set, {} entries", g_binds.size());
	}

	std::vector<Bind> DefaultBinds()
	{
		// System Tab (the Pause control) on Start, where the unbound controller Journal was.
		Bind b;
		b.context = "Gameplay";
		b.event = "Pause";
		b.device = 2;
		b.key = 0x0010;
		return { b };
	}

	bool BindModifier(std::string_view a_event, int a_device, std::uint16_t& a_modifier)
	{
		std::scoped_lock l(g_lock);
		const char* context = ContextName(0);  // Gameplay; the Controls list only shows that context
		for (const auto& b : g_binds)
		{
			if (b.device != a_device || !IEquals(b.context, context) || !IEquals(b.event, a_event)) { continue; }
			if (b.modifier == 0) { return false; }  // bound, but with no modifier to show
			a_modifier = b.modifier;
			return true;
		}
		return false;
	}

	std::uint16_t ParseButton(std::string_view a_text, int a_device)
	{
		if (a_device == 2)
		{
			for (const auto& [name, code] : kPadButtons)
			{
				if (IEquals(name, a_text)) { return code; }
			}
		}
		if (a_device == 0)
		{
			// A keyboard key may be written by name ("Left Shift") as well as by code ("0x2a").
			for (const auto& [name, code] : kKeyboardKeys)
			{
				if (IEquals(name, a_text)) { return code; }
			}
		}
		try
		{
			std::size_t used = 0;
			const auto value = std::stoul(std::string(a_text), &used, 0);
			if (used != a_text.size() || value >= 0xFF && a_device != 2 || value > 0xFFFF) { return kUnmapped; }
			return static_cast<std::uint16_t>(value);
		}
		catch (...)
		{
			return kUnmapped;
		}
	}

	const char* ButtonName(std::uint16_t a_key, int a_device)
	{
		if (a_device == 2)
		{
			for (const auto& [name, code] : kPadButtons)
			{
				if (code == a_key) { return name; }
			}
			return "";
		}
		// Keyboard: name it where the table knows it, so a modifier reads "Left Shift" and not "0x2a".
		// Mouse has no name table; its callers fall back to hex, as before.
		if (a_device == 0)
		{
			for (const auto& [name, code] : kKeyboardKeys)
			{
				if (code == a_key) { return name; }
			}
		}
		return "";
	}

	std::vector<DelinkedAction> GetDelinked()
	{
		std::scoped_lock l(g_lock);
		return g_delinked;
	}

	bool FindDelinkedRow(std::string_view a_row, DelinkedAction& a_out)
	{
		std::scoped_lock l(g_lock);
		for (const auto& d : g_delinked)
		{
			if (d.row == a_row) { a_out = d; return true; }
		}
		return false;
	}

	std::vector<std::uint16_t> LiveKeysIn(int a_context, std::string_view a_event, int a_device)
	{
		std::vector<std::uint16_t> out;
		auto* map = RE::ControlMap::GetSingleton();
		auto* mappings = map ? MappingsFor(map, a_context, a_device) : nullptr;
		if (!mappings) { return out; }
		for (auto* m : Find(*mappings, a_event)) { out.push_back(m->inputKey); }
		return out;
	}

	void SetBind(int a_context, std::string_view a_event, int a_device, std::uint16_t a_key, bool a_withModifier)
	{
		std::scoped_lock l(g_lock);
		const char* context = ContextName(a_context);
		for (auto& b : g_binds)
		{
			if (b.device != a_device || !IEquals(b.context, context) || !IEquals(b.event, a_event)) { continue; }
			logger::info("bind {}|{}|{}: set from the Controls menu, {} -> {}{}", b.context, b.event, DeviceName(a_device), Hex(b.key), Hex(a_key), a_withModifier ? " + Modifier" : "");
			b.key = a_key;
			b.modifier = 0;
			b.useDesignatedModifier = a_withModifier;
			return;
		}
		Bind b;
		b.context = context;
		b.event = std::string(a_event);
		b.device = a_device;
		b.key = a_key;
		b.useDesignatedModifier = a_withModifier;
		logger::info("bind {}|{}|{}: added from the Controls menu -> {}{}", b.context, b.event, DeviceName(a_device), Hex(a_key), a_withModifier ? " + Modifier" : "");
		g_binds.push_back(std::move(b));
	}

	bool HolderOf(int a_context, int a_device, std::uint16_t a_key, std::uint16_t a_modifier, Holder& a_out)
	{
		auto* map = RE::ControlMap::GetSingleton();
		auto* mappings = map ? MappingsFor(map, a_context, a_device) : nullptr;
		if (!mappings) { return false; }
		for (const auto& m : *mappings)
		{
			if (m.inputKey != a_key || m.modifier != a_modifier || !m.eventID.c_str()) { continue; }
			a_out.event = m.eventID.c_str();
			a_out.remappable = m.remappable;
			return true;
		}
		return false;
	}

	bool LiveBinding(int a_context, std::string_view a_event, int a_device, std::uint16_t& a_key, std::uint16_t& a_modifier)
	{
		auto* map = RE::ControlMap::GetSingleton();
		auto* mappings = map ? MappingsFor(map, a_context, a_device) : nullptr;
		if (!mappings) { return false; }
		const auto found = Find(*mappings, a_event);
		if (found.empty()) { return false; }
		a_key = found.front()->inputKey;
		a_modifier = found.front()->modifier;
		return true;
	}

	std::vector<KeylessControl> BoundOnFamily(bool a_gamepad)
	{
		std::vector<std::pair<std::string, int>> events;
		{
			std::scoped_lock l(g_lock);
			for (const auto& b : g_binds)
			{
				if (!IEquals(b.context, "Gameplay") || b.key == kUnmapped) { continue; }
				if (a_gamepad ? b.device != 2 : (b.device != 0 && b.device != 1)) { continue; }
				events.emplace_back(b.event, 0);
			}
		}
		std::vector<KeylessControl> out;
		for (const auto& [event, unused] : events)
		{
			const int order = OrderInContext(event);
			if (order < 0) { continue; }
			if (std::any_of(out.begin(), out.end(), [&](const KeylessControl& a_k) { return IEquals(a_k.event, event); })) { continue; }
			out.push_back({ event, order });
		}
		return out;
	}

	bool UpdateBind(int a_context, std::string_view a_event, int a_device, std::uint16_t a_key)
	{
		std::scoped_lock l(g_lock);
		const char* context = ContextName(a_context);
		for (auto& b : g_binds)
		{
			if (b.device != a_device || !IEquals(b.context, context) || !IEquals(b.event, a_event)) { continue; }
			if (b.key == a_key) { return false; }
			logger::info("bind {}|{}|{}: follows the Controls menu, {} -> {}", b.context, b.event, DeviceName(a_device), Hex(b.key), Hex(a_key));
			b.key = a_key;
			return true;
		}
		return false;
	}

	bool RemoveBind(int a_context, std::string_view a_event, int a_device)
	{
		std::scoped_lock l(g_lock);
		const char* context = ContextName(a_context);
		const auto it = std::find_if(g_binds.begin(), g_binds.end(), [&](const Bind& b) {
			return b.device == a_device && IEquals(b.context, context) && IEquals(b.event, a_event);
		});
		if (it == g_binds.end()) { return false; }
		logger::info("bind {}|{}|{}: removed (unbound in the Controls menu)", it->context, it->event, DeviceName(a_device));
		g_binds.erase(it);
		return true;
	}

	bool Unbind(int a_context, std::string_view a_event, int a_device, std::string& a_why)
	{
		auto* map = RE::ControlMap::GetSingleton();
		auto* mappings = map ? MappingsFor(map, a_context, a_device) : nullptr;
		if (!mappings) { a_why = "that context is not loaded"; logger::warn("unbind {}|{}|{}: {}", ContextName(a_context), a_event, DeviceName(a_device), a_why); return false; }
		auto found = Find(*mappings, a_event);
		if (found.empty()) { a_why = "the game has no mapping for that control on that device"; logger::warn("unbind {}|{}|{}: {}", ContextName(a_context), a_event, DeviceName(a_device), a_why); return false; }
		{
			std::scoped_lock l(g_lock);
			if (FindEntry(a_context, a_event, a_device) != g_entries.end()) { a_why = "already in the list"; return true; }
			Entry e;
			e.context = ContextName(a_context);
			e.event = std::string(a_event);
			e.device = a_device;
			g_entries.push_back(std::move(e));
		}
		logger::info("unbind {}|{}|{}: added to the list", ContextName(a_context), a_event, DeviceName(a_device));
		ApplyAll("unbind");
		return true;
	}

	bool Rebind(int a_context, std::string_view a_event, int a_device, std::string& a_why)
	{
		Entry e;
		{
			std::scoped_lock l(g_lock);
			auto it = FindEntry(a_context, a_event, a_device);
			if (it == g_entries.end()) { a_why = "that control is not in the list"; logger::debug("rebind {}|{}|{}: {}", ContextName(a_context), a_event, DeviceName(a_device), a_why); return false; }
			e = *it;
			g_entries.erase(it);
		}
		auto* map = RE::ControlMap::GetSingleton();
		auto* mappings = map ? MappingsFor(map, a_context, a_device) : nullptr;
		if (!mappings) { a_why = "that context is not loaded"; logger::warn("rebind {}|{}|{}: {}", e.context, e.event, DeviceName(a_device), a_why); return false; }
		auto found = Find(*mappings, a_event);
		const std::size_t n = std::min(found.size(), e.original.size());
		std::string after;
		for (std::size_t i = 0; i < n; ++i)
		{
			found[i]->inputKey = e.original[i].key;
			found[i]->modifier = e.original[i].modifier;
			if (!after.empty()) { after += ","; }
			after += Hex(found[i]->inputKey);
		}
		SortByKey(*mappings);
		logger::info("rebind {}|{}|{}: {} mapping(s) back to {}", e.context, e.event, DeviceName(a_device), n, after.empty() ? "-" : after);
		return true;
	}

	void ApplyAll(const char* a_reason)
	{
		auto* map = RE::ControlMap::GetSingleton();
		if (!map) { logger::warn("apply ({}): the ControlMap singleton is null; nothing applied", a_reason); return; }
		std::scoped_lock l(g_lock);
		g_lastConverted = ConvertLinkedMappings(map, a_reason);  // before the lists, bEnabled=0 included
		g_convertedTotal += g_lastConverted;
		if (g_lastConverted) { logger::info("apply ({}): {} linked menu mapping(s) turned into separate mappings", a_reason, g_lastConverted); }
		// The DEFAULT F / Y, before the lists: an [Unbound] or [Bound] line for Item Menus|YButton - the Favorite Item row -
		// is applied after it and wins (the owner, 2026-09-28: "they should be rebindable now that they are properly
		// separated from their linked mappings"). bEnabled=0 included.
		g_lastFavourite = PinFavouriteButton(map, a_reason);
		if (!settings::general::enabled)
		{
			logger::info("apply ({}): bEnabled=0; {} entr{} left as the game has them", a_reason, g_entries.size(), g_entries.size() == 1 ? "y" : "ies");
			return;
		}
		int touched = 0, missing = 0;
		std::vector<Mappings*> dirty;
		for (auto& e : g_entries)
		{
			const int ctx = ContextIndex(e.context);
			auto* mappings = ctx >= 0 ? MappingsFor(map, ctx, e.device) : nullptr;
			if (!mappings) { ++missing; logger::warn("apply ({}): {}|{}|{} - context unknown or not loaded", a_reason, e.context, e.event, DeviceName(e.device)); continue; }
			auto found = Find(*mappings, e.event);
			if (found.empty()) { ++missing; logger::warn("apply ({}): {}|{}|{} - the game has no such control on that device (check the name against controlmap.txt)", a_reason, e.context, e.event, DeviceName(e.device)); continue; }
			if (e.original.empty())
			{
				for (auto* m : found) { e.original.push_back({ m->inputKey, m->modifier }); }
			}
			int here = 0;
			for (auto* m : found) { if (m->inputKey != kUnmapped) { m->inputKey = kUnmapped; ++here; } }
			if (here)
			{
				touched += here;
				if (std::find(dirty.begin(), dirty.end(), mappings) == dirty.end()) { dirty.push_back(mappings); }
				logger::debug("apply ({}): {}|{}|{} - {} key(s) cleared", a_reason, e.context, e.event, DeviceName(e.device), here);
			}
		}
		for (auto* m : dirty) { SortByKey(*m); }
		g_lastRemappable = ApplyRemappable(map, a_reason);
		g_lastBound = ApplyBinds(map, a_reason);
		g_lastApply = a_reason;
		++g_applyCount;
		g_lastTouched = touched;
		logger::info("apply ({}): {} entr{}, {} key(s) cleared in {} device list(s)", a_reason, g_entries.size(), g_entries.size() == 1 ? "y" : "ies", touched, dirty.size());
		if (missing) { logger::warn("apply ({}): {} entr{} could not be applied (see above)", a_reason, missing, missing == 1 ? "y" : "ies"); }
	}

	bool KeylessOnFamily(std::string_view a_event, bool a_gamepad, int* a_order)
	{
		auto* map = RE::ControlMap::GetSingleton();
		auto* ctx = map ? Context(map, 0) : nullptr;  // Gameplay (index 0 on every runtime): the context the Controls list shows
		if (!ctx) { return false; }
		int found = 0;
		int order = -1;
		const int first = a_gamepad ? 2 : 0;  // the list shows keyboard AND mouse together, or the gamepad
		const int last = a_gamepad ? 2 : 1;
		for (int d = first; d <= last; ++d)
		{
			for (auto* m : Find(ctx->deviceMappings[d], a_event))
			{
				++found;
				if (order < 0) { order = m->indexInContext; }
				if (m->inputKey != kUnmapped) { return false; }
			}
		}
		if (a_order) { *a_order = order; }
		return found > 0;
	}

	int OrderInContext(std::string_view a_event)
	{
		auto* map = RE::ControlMap::GetSingleton();
		auto* ctx = map ? Context(map, 0) : nullptr;
		if (!ctx) { return -1; }
		for (int d = 0; d < 3; ++d)
		{
			auto found = Find(ctx->deviceMappings[d], a_event);
			if (!found.empty()) { return found.front()->indexInContext; }
		}
		return -1;
	}

	std::vector<std::uint16_t> LiveKeys(std::string_view a_event, int a_device)
	{
		std::vector<std::uint16_t> out;
		auto* map = RE::ControlMap::GetSingleton();
		auto* mappings = map ? MappingsFor(map, 0, a_device) : nullptr;
		if (!mappings) { return out; }
		for (auto* m : Find(*mappings, a_event)) { out.push_back(m->inputKey); }
		return out;
	}

	bool IsListed(std::string_view a_event, int a_device)
	{
		std::scoped_lock l(g_lock);
		return FindEntry(0, a_event, a_device) != g_entries.end();
	}

	bool Forget(int a_context, std::string_view a_event, int a_device)
	{
		std::scoped_lock l(g_lock);
		auto it = FindEntry(a_context, a_event, a_device);
		if (it == g_entries.end()) { return false; }
		g_entries.erase(it);
		logger::info("forget {}|{}|{}: removed from the list; the control keeps the key it has now", ContextName(a_context), a_event, DeviceName(a_device));
		return true;
	}

	namespace
	{
		std::string PathText(const std::filesystem::path& a_path)
		{
			const auto u8 = a_path.u8string();
			return std::string(u8.begin(), u8.end());
		}

		std::filesystem::path PathFromText(const std::string& a_text)
		{
			return std::filesystem::path(std::u8string(a_text.begin(), a_text.end()));
		}

		std::vector<std::uint16_t> Sorted(std::vector<std::uint16_t> a_keys)
		{
			std::sort(a_keys.begin(), a_keys.end());
			return a_keys;
		}

		// Caller holds g_lock. Gameplay|a_event on a_device now has a_live keys: make the INI lists say so.
		int RecordLocked(std::string_view a_event, int a_device, const std::vector<std::uint16_t>& a_live, const char* a_reason)
		{
			const std::uint16_t def = DefaultKey(a_event, a_device);
			const auto real = std::find_if(a_live.begin(), a_live.end(), [](std::uint16_t a_k) { return a_k != kUnmapped; });
			const bool hasKey = real != a_live.end();
			const char* context = ContextName(0);
			auto entry = FindEntry(0, a_event, a_device);
			auto bind = std::find_if(g_binds.begin(), g_binds.end(), [&](const Bind& b) {
				return b.device == a_device && IEquals(b.context, context) && IEquals(b.event, a_event);
			});
			int changed = 0;
			if (!hasKey)
			{
				if (bind != g_binds.end()) { g_binds.erase(bind); ++changed; }
				if (entry == g_entries.end() && def != kUnmapped)
				{
					Entry e;
					e.context = context;
					e.event = std::string(a_event);
					e.device = a_device;
					e.original.push_back({ def, 0 });  // a rebind gives the controlmap.txt key back (modifier 0: see ApplyBinds)
					g_entries.push_back(std::move(e));
					++changed;
				}
				if (changed) { logger::info("own remaps ({}): {}|{}|{} has no key - written to [Unbound]", a_reason, context, a_event, DeviceName(a_device)); }
				return changed;
			}
			if (entry != g_entries.end()) { g_entries.erase(entry); ++changed; }
			if (std::find(a_live.begin(), a_live.end(), def) != a_live.end())
			{
				if (bind != g_binds.end()) { g_binds.erase(bind); ++changed; }
				if (changed) { logger::info("own remaps ({}): {}|{}|{} is back on its controlmap.txt key {} - its INI lines are dropped", a_reason, context, a_event, DeviceName(a_device), Hex(def)); }
				return changed;
			}
			if (bind == g_binds.end())
			{
				Bind b;
				b.context = context;
				b.event = std::string(a_event);
				b.device = a_device;
				b.key = *real;
				g_binds.push_back(std::move(b));
				++changed;
			}
			else if (bind->key != *real)
			{
				bind->key = *real;
				++changed;
			}
			if (changed) { logger::info("own remaps ({}): {}|{}|{} -> {} written to [Bound] (controlmap.txt gives {})", a_reason, context, a_event, DeviceName(a_device), Hex(*real), Hex(def)); }
			return changed;
		}
	}

	std::uint16_t DefaultKey(std::string_view a_event, int a_device)
	{
		if (a_device < 0 || a_device > 2) { return kUnmapped; }
		const auto& defaults = GameplayDefaults();
		const auto it = defaults.find(Lower(a_event));
		return it == defaults.end() ? kUnmapped : it->second.key[a_device];
	}

	GameplaySnapshot SnapshotGameplay()
	{
		GameplaySnapshot s;
		auto* map = RE::ControlMap::GetSingleton();
		if (!map) { logger::warn("own remaps: the ControlMap singleton is null; no snapshot taken"); return s; }
		for (int d = 0; d < 3; ++d)
		{
			auto* mappings = MappingsFor(map, 0, d);
			if (!mappings) { continue; }
			for (const auto& m : *mappings)
			{
				const char* event = m.eventID.c_str();
				if (!event || !*event) { continue; }
				auto it = std::find_if(s.events.begin(), s.events.end(), [&](const std::string& a_have) { return IEquals(a_have, event); });
				const std::size_t i = static_cast<std::size_t>(it - s.events.begin());
				if (it == s.events.end())
				{
					s.events.emplace_back(event);
					s.keys.emplace_back();
				}
				s.keys[i][d].push_back(m.inputKey);
			}
		}
		return s;
	}

	int RecordChanges(const GameplaySnapshot& a_before, const char* a_reason)
	{
		const GameplaySnapshot after = SnapshotGameplay();
		if (GameplayDefaults().empty()) { logger::warn("own remaps ({}): controlmap.txt defaults are unavailable, so a control left with no key cannot be told from its default; unbinds are not recorded", a_reason); }
		std::scoped_lock l(g_lock);
		int changed = 0;
		for (std::size_t i = 0; i < after.events.size(); ++i)
		{
			const auto was = std::find_if(a_before.events.begin(), a_before.events.end(), [&](const std::string& a_have) { return IEquals(a_have, after.events[i]); });
			for (int d = 0; d < 3; ++d)
			{
				const std::vector<std::uint16_t> before = was == a_before.events.end() ? std::vector<std::uint16_t>{} : a_before.keys[static_cast<std::size_t>(was - a_before.events.begin())][d];
				if (Sorted(before) == Sorted(after.keys[i][d])) { continue; }
				changed += RecordLocked(after.events[i], d, after.keys[i][d], a_reason);
			}
		}
		if (changed)
		{
			g_ownLines += changed;
			g_lastOwn = std::format("{}: {} INI line(s) changed", a_reason, changed);
		}
		logger::debug("own remaps ({}): {} control(s) compared, {} INI line(s) changed", a_reason, after.events.size(), changed);
		return changed;
	}

	int ImportLiveRemaps(const char* a_reason)
	{
		const GameplaySnapshot live = SnapshotGameplay();
		const auto& defaults = GameplayDefaults();
		if (defaults.empty()) { logger::warn("own remaps ({}): controlmap.txt defaults are unavailable; nothing imported", a_reason); return 0; }
		std::scoped_lock l(g_lock);
		int changed = 0;
		for (std::size_t i = 0; i < live.events.size(); ++i)
		{
			const auto def = defaults.find(Lower(live.events[i]));
			if (def == defaults.end()) { continue; }  // not a control controlmap.txt lists in Gameplay
			for (int d = 0; d < 3; ++d)
			{
				const auto& keys = live.keys[i][d];
				if (keys.empty()) { continue; }                                          // no mapping for it on that device
				if (FindEntry(0, live.events[i], d) != g_entries.end()) { continue; }  // the INI already decides it
				const bool bound = std::any_of(g_binds.begin(), g_binds.end(), [&](const Bind& b) {
					return b.device == d && IEquals(b.context, "Gameplay") && IEquals(b.event, live.events[i]);
				});
				if (bound) { continue; }
				const bool hasKey = std::any_of(keys.begin(), keys.end(), [](std::uint16_t a_k) { return a_k != kUnmapped; });
				const bool same = hasKey ? std::find(keys.begin(), keys.end(), def->second.key[d]) != keys.end() : def->second.key[d] == kUnmapped;
				if (same) { continue; }
				changed += RecordLocked(live.events[i], d, keys, a_reason);
			}
		}
		if (changed)
		{
			g_ownLines += changed;
			g_lastOwn = std::format("{}: {} INI line(s) imported from the live control map", a_reason, changed);
		}
		logger::info("own remaps ({}): {} control(s) checked against controlmap.txt, {} INI line(s) imported", a_reason, live.events.size(), changed);
		return changed;
	}

	std::string CustomMapPath()
	{
		// The game opens "ControlMap_Custom.txt" by bare name, so it lives in the process's working folder (the game
		// folder; Stock Game under MO2).
		std::error_code ec;
		const auto folder = std::filesystem::current_path(ec);
		if (ec) { return {}; }
		return PathText(folder / "ControlMap_Custom.txt");
	}

	bool RemoveCustomMap(const char* a_reason)
	{
		const std::string path = CustomMapPath();
		if (path.empty()) { logger::warn("own remaps ({}): the working folder is unknown; ControlMap_Custom.txt is not looked for", a_reason); return false; }
		const std::filesystem::path p = PathFromText(path);
		std::error_code ec;
		if (!std::filesystem::exists(p, ec)) { logger::debug("own remaps ({}): no ControlMap_Custom.txt at {}", a_reason, path); return false; }
		const auto size = std::filesystem::file_size(p, ec);
		ec.clear();
		if (!std::filesystem::remove(p, ec) || ec)
		{
			logger::warn("own remaps ({}): could not remove {}: {}", a_reason, path, ec ? ec.message() : std::string("not removed"));
			return false;
		}
		logger::info("own remaps ({}): removed {} ({} bytes) - the controls it held are in the INI", a_reason, path, size);
		std::scoped_lock l(g_lock);
		g_customMapRemoved = true;
		g_lastOwn = std::format("{}: ControlMap_Custom.txt removed", a_reason);
		return true;
	}

	void OwnRemapsAtDataLoad()
	{
		if (!settings::general::keepRemapsInIni)
		{
			logger::info("own remaps: bKeepRemapsInIni=0; the game keeps its own ControlMap_Custom.txt");
			return;
		}
		if (!settings::general::enabled)
		{
			logger::info("own remaps: bEnabled=0; ControlMap_Custom.txt is left alone so no remap is lost");
			return;
		}
		const std::string path = CustomMapPath();
		std::error_code ec;
		if (path.empty() || !std::filesystem::exists(PathFromText(path), ec))
		{
			logger::info("own remaps: no ControlMap_Custom.txt in the game folder{}; the INI holds every remap", path.empty() ? " (working folder unknown)" : "");
			return;
		}
		logger::info("own remaps: {} found; its changes are moved into the INI", path);
		if (ImportLiveRemaps("data loaded") > 0) { settings::Save(); }
		RemoveCustomMap("data loaded");
	}

	std::vector<KeylessControl> ListedKeylessOnFamily(bool a_gamepad)
	{
		std::vector<std::string> events;
		{
			std::scoped_lock l(g_lock);
			if (!settings::general::enabled) { return {}; }
			for (const auto& e : g_entries)
			{
				if (!IEquals(e.context, "Gameplay") || a_gamepad != (e.device == 2)) { continue; }
				if (std::none_of(events.begin(), events.end(), [&](const std::string& a_have) { return IEquals(a_have, e.event); })) { events.push_back(e.event); }
			}
		}
		std::vector<KeylessControl> out;
		for (const auto& ev : events)
		{
			int order = -1;
			if (KeylessOnFamily(ev, a_gamepad, &order)) { out.push_back({ ev, order }); }
		}
		return out;
	}

	void RestoreAll(const char* a_reason)
	{
		auto* map = RE::ControlMap::GetSingleton();
		if (!map) { logger::warn("restore ({}): the ControlMap singleton is null", a_reason); return; }
		std::scoped_lock l(g_lock);
		int restored = 0;
		std::vector<Mappings*> dirty;
		for (const auto& e : g_entries)
		{
			const int ctx = ContextIndex(e.context);
			auto* mappings = ctx >= 0 ? MappingsFor(map, ctx, e.device) : nullptr;
			if (!mappings) { continue; }
			auto found = Find(*mappings, e.event);
			const std::size_t n = std::min(found.size(), e.original.size());
			for (std::size_t i = 0; i < n; ++i)
			{
				if (found[i]->inputKey != e.original[i].key) { found[i]->inputKey = e.original[i].key; found[i]->modifier = e.original[i].modifier; ++restored; }
			}
			if (n && std::find(dirty.begin(), dirty.end(), mappings) == dirty.end()) { dirty.push_back(mappings); }
		}
		for (auto* m : dirty) { SortByKey(*m); }
		logger::info("restore ({}): {} key(s) given back", a_reason, restored);
	}

	void Install()
	{
		if (g_sinkInstalled) { return; }
		if (auto* ui = RE::UI::GetSingleton())
		{
			ui->AddEventSink<RE::MenuOpenCloseEvent>(MenuSink::GetSingleton());
			g_sinkInstalled = true;
			logger::info("sink registered: Journal Menu close re-applies the list ({} contexts on this runtime)", ContextCount());
		}
		else
		{
			logger::warn("RE::UI singleton is null at kDataLoaded; the journal-close re-apply is not installed (loads and new games still re-apply)");
		}
	}

	std::string DumpJson(int a_context)
	{
		auto* map = RE::ControlMap::GetSingleton();
		if (!map) { return R"({"ok":false,"error":"ControlMap singleton is null"})"; }
		std::string out = std::format(R"({{"ok":true,"op":"dump","contextCount":{},"contexts":[)", ContextCount());
		bool firstCtx = true;
		for (int c = 0; c < ContextCount(); ++c)
		{
			if (a_context >= 0 && c != a_context) { continue; }
			auto* ctx = Context(map, c);
			if (!firstCtx) { out += ","; }
			firstCtx = false;
			out += std::format(R"({{"index":{},"name":"{}","loaded":{})", c, ContextName(c), ctx ? "true" : "false");
			if (ctx)
			{
				for (int d = 0; d < 3; ++d)
				{
					out += std::format(R"(,"{}":[)", kDevices[d]);
					bool first = true;
					for (auto& m : ctx->deviceMappings[d])
					{
						if (!first) { out += ","; }
						first = false;
						out += std::format(R"({{"event":"{}","key":"{}","modifier":"{}","remappable":{}}})",
										   EscapeJson(m.eventID.c_str() ? m.eventID.c_str() : ""), Hex(m.inputKey), Hex(m.modifier), m.remappable ? "true" : "false");
					}
					out += "]";
				}
			}
			out += "}";
		}
		out += "]}";
		return out;
	}

	std::string StateJson()
	{
		std::scoped_lock l(g_lock);
		std::string out = std::format(R"("unbinder":{{"contextCount":{},"lastApply":"{}","applyCount":{},"lastTouched":{},"linksConverted":{},"linksConvertedSession":{},"favouriteSet":{},"favouriteLinksCut":{},"sink":{},"entries":[)",
									  ContextCount(), EscapeJson(g_lastApply), g_applyCount, g_lastTouched, g_lastConverted, g_convertedTotal, g_lastFavourite, g_favouriteLinksCut,
									  g_sinkInstalled ? "true" : "false");
		bool first = true;
		for (const auto& e : g_entries)
		{
			if (!first) { out += ","; }
			first = false;
			out += std::format(R"({{"context":"{}","event":"{}","device":"{}","original":[)", EscapeJson(e.context), EscapeJson(e.event), DeviceName(e.device));
			bool f2 = true;
			for (const auto& k : e.original) { if (!f2) { out += ","; } f2 = false; out += std::format(R"({{"key":"{}"}})", Hex(k.key)); }
			out += "]}";
		}
		out += std::format(R"(],"customMap":{{"path":"{}","removedThisSession":{}}},"lastOwn":"{}","ownLines":{},"lastBound":{},"binds":[)",
						  EscapeJson(CustomMapPath()), g_customMapRemoved ? "true" : "false", EscapeJson(g_lastOwn), g_ownLines, g_lastBound);
		first = true;
		for (const auto& b : g_binds)
		{
			if (!first) { out += ","; }
			first = false;
			out += std::format(R"({{"context":"{}","event":"{}","device":"{}","key":"{}","button":"{}"}})", EscapeJson(b.context), EscapeJson(b.event), DeviceName(b.device),
							   Hex(b.key), ButtonName(b.key, b.device));
		}
		out += "]}";
		return out;
	}
}
