#include "PCH.h"

#include "Unbinder.h"

#include "GamepadPP.h"

#include "ControlsList.h"
#include "Functions.h"
#include "Settings.h"
#include "SystemMenu.h"
#include "UVCRequestAPI.h"
#include "utils/Logger.h"

#include "REX/W32/KERNEL32.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <format>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace unbinder
{
	void OnJournalForSwim(bool a_open);  // the swimming context, below

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
			std::uint16_t key[3]{ kUnmapped, kUnmapped, kUnmapped };  // keyboard, mouse, gamepad: the FIRST key
			// Every key, sorted. A control can have several on one device - Favorites is "0x0001,0x0002" on the gamepad
			// (D-pad up AND down) - and "is this control back on its default?" has to compare the whole set: comparing
			// the first key alone took Favorites on D-pad up only for its default and dropped its INI lines, and the next
			// start gave it both buttons again (1.1.3, a Nexus report).
			std::vector<std::uint16_t> all[3];
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

		// Every key of a field, sorted: "0x0001,0x0002" -> { 0x0001, 0x0002 }; "0x2a+0x0f" -> { 0x2a }; a link or 0xff -> {}.
		std::vector<std::uint16_t> ParseKeys(std::string_view a_field)
		{
			std::vector<std::uint16_t> out;
			if (a_field.empty() || a_field.front() == '!') { return out; }
			while (!a_field.empty())
			{
				const std::size_t comma = a_field.find(',');
				const std::uint16_t key = ParseKey(a_field.substr(0, comma));
				if (key != kUnmapped) { out.push_back(key); }
				if (comma == std::string_view::npos) { break; }
				a_field.remove_prefix(comma + 1);
			}
			std::sort(out.begin(), out.end());
			return out;
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
				for (int d = 0; d < 3; ++d)
				{
					keys.key[d] = ParseKey(fields[1 + d]);
					keys.all[d] = ParseKeys(fields[1 + d]);
				}
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
				{ "stats|ybutton", "Make Legendary" },
				{ "map|click", "Select Location" }, { "map|up", "Pan Up" }, { "map|down", "Pan Down" }, { "map|left", "Pan Left" }, { "map|right", "Pan Right" }
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
				// The Favorite Item row, whether or not the control map linked the button. Rows for menu actions came only from
				// the links ConvertLinkedMappings turned into mappings, and a control map that gives YButton keys of its own
				// (the installer's Controlmap.txt Fixed and Cleaned: 0x21 / 0x8000) has no link to turn, so the row was missing
				// on both pages and the Favourite button could not be rebound (a Nexus report, 2026-10-23; 1.1.3).
				RecordDelinked(kItemMenus, kEvent, d);
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
					// Already exactly there: BOTH halves must match, or a bind that only changes its modifier is skipped - and the
					// control's OTHER mappings on this device must have no key. controlmap.txt gives some controls several keys
					// on one device (Favorites: D-pad up AND down); a bind means "this key and only this key", so testing the
					// first mapping alone skipped Favorites|gamepad|DPadUp at every start (its first mapping is already Up after
					// the key sort) and left D-pad down on it as well (1.1.3, a Nexus report).
					// Exactly one mapping on the key, every other one empty (0xFF can sort before a controller key such as Y).
					if (!found.empty())
					{
						const auto onKey = std::count_if(found.begin(), found.end(), [&](const RE::ControlMap::UserEventMapping* a_m) { return a_m->inputKey == b->key && a_m->modifier == wantModifier; });
						const auto empty = std::count_if(found.begin(), found.end(), [](const RE::ControlMap::UserEventMapping* a_m) { return a_m->inputKey == kUnmapped; });
						if (onKey == 1 && onKey + empty == static_cast<std::ptrdiff_t>(found.size())) { continue; }
					}

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
				OnJournalForSwim(a_event->opening);
				if (a_event->opening)
				{
					// Gamepad++'s buttons (1.5.0) may have changed in its own menu since the last read: read before the Controls page shows them.
					gpp::Refresh("journal opened");
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
		// ship it with my current unbound keys as the default"): the Tween Menu's shortcuts plus Quicksave, Quickload,
		// Auto-Move and Toggle Always Run on the keyboard; Wait and Journal on the controller, where Start opens the System tab
		// instead (DefaultBinds). Device: 0 keyboard, 2 gamepad. Favorites stays BOUND since 1.1.3 (the owner, 2026-10-20, after
		// a player lost the Favorites menu on Q): the author's Wheeler setup that unbinds it is a commented line in the INI.
		constexpr std::pair<const char*, int> kDefaults[] = {
			{ "Journal", 0 },
			{ "Quick Inventory", 0 },
			{ "Quick Magic", 0 },
			{ "Quick Map", 0 },
			{ "Quick Stats", 0 },
			{ "Wait", 0 },
			{ "Wait", 2 },
			{ "Journal", 2 },
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
	// ("360_LB", "360_LT"). The game's own file names every button 360_ whatever pad is plugged in (the journal only
	// swaps PS3_ for PS5_ on its own); a replacer may name them otherwise - Norden UI's gamepad.txt uses PS3_ - and
	// the Controls sheet's [Controls] GamepadFaces setting can redraw either as the other set without touching this. Guessing lowercase "lt" and assuming the d-pad had no glyph got both halves wrong
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
	// to any other row as "reserved". Favorites on the gamepad likewise (1.1.3): the game clears its flag there, so it had no
	// row on the controller page to bind it on.
	std::vector<Remappable> DefaultRemappable()
	{
		return {
			{ "Gameplay", "Right Attack/Block", 2 },
			{ "Gameplay", "Left Attack/Block", 2 },
			{ "Gameplay", "Favorites", 2 },
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
		// The shipped INI's [Bound] lines (rule 16): System Tab (the Pause control) on Start, where the unbound controller
		// Journal was; Tween Menu on Back; Shout on RT; Right Attack/Block on RB; Left Attack/Block on LB; Sprint on B; Toggle
		// POV on D-pad up; Favorites on D-pad down alone (1.1.3: the game gives it up AND down, and up is Toggle POV's).
		constexpr std::pair<const char*, std::uint16_t> kDefaults[] = {
			{ "Pause", 0x0010 },
			{ "Tween Menu", 0x0020 },
			{ "Shout", 0x000a },
			{ "Right Attack/Block", 0x0200 },
			{ "Left Attack/Block", 0x0100 },
			{ "Sprint", 0x2000 },
			{ "Toggle POV", 0x0001 },
			{ "Favorites", 0x0002 },
		};
		std::vector<Bind> out;
		for (const auto& [event, key] : kDefaults)
		{
			Bind b;
			b.context = "Gameplay";
			b.event = event;
			b.device = 2;
			b.key = key;
			out.push_back(std::move(b));
		}
		return out;
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

	// A button name as it is compared (1.2.1): lower case, spaces and underscores dropped, "left"/"right" shortened to l/r
	// and "control" to "ctrl" - so "LShift", "lshift", "Left Shift" and "left_shift" are one name. The table's own
	// comment promised "lshift" parsed; a plain case-insensitive compare never did (batch 11, 2026-10-23: "LShift" and
	// "LAlt" refused by the DevBench function op).
	static std::string ButtonKey(std::string_view a_text)
	{
		std::string s;
		for (const char c : a_text)
		{
			if (c == ' ' || c == '_') { continue; }
			s += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		}
		for (const auto& [from, to] : { std::pair<std::string_view, std::string_view>{ "left", "l" }, { "right", "r" } })
		{
			if (s.rfind(from, 0) == 0) { s = std::string(to) + s.substr(from.size()); break; }
		}
		if (const auto at = s.find("control"); at != std::string::npos) { s.replace(at, 7, "ctrl"); }
		return s;
	}

	std::uint16_t ParseButton(std::string_view a_text, int a_device)
	{
		const std::string wanted = ButtonKey(a_text);
		if (a_device == 2)
		{
			for (const auto& [name, code] : kPadButtons)
			{
				if (IEquals(name, a_text) || ButtonKey(name) == wanted) { return code; }
			}
		}
		if (a_device == 0)
		{
			// A keyboard key may be written by name ("Left Shift", "LShift") as well as by code ("0x2a").
			for (const auto& [name, code] : kKeyboardKeys)
			{
				if (IEquals(name, a_text) || ButtonKey(name) == wanted) { return code; }
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
		// The first mapping that HAS a key: a control with several mappings on one device (Favorites on the gamepad) keeps
		// the unused ones on 0xFF, and 0xFF sorts before a controller key such as Y (0x8000), so the front one can be empty
		// while the control is bound.
		const auto real = std::find_if(found.begin(), found.end(), [](const RE::ControlMap::UserEventMapping* a_m) { return a_m->inputKey != kUnmapped; });
		const auto* m = real != found.end() ? *real : found.front();
		a_key = m->inputKey;
		a_modifier = m->modifier;
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

	// ---- The swimming context (Unbinder.h) ----
	namespace
	{
		std::vector<Bind> g_swim;                          // [Swimming]; under g_lock
		std::vector<Bind> g_horse;                         // [Horseback]; under g_lock
		std::vector<Bind> g_boat;                          // [Boat]; under g_lock
		std::atomic<bool> g_haveSwimLines{ false };
		std::atomic<bool> g_haveHorseLines{ false };
		std::atomic<bool> g_haveBoatLines{ false };
		std::atomic<int> g_layoutOn{ kLayoutLand };        // which layout is on the live map (written under g_lock)
		std::atomic<int> g_stateStable{ kLayoutLand };     // the player's swimming or mounted state, once it has settled
		std::atomic<bool> g_swimJournal{ false };          // the journal is open: the Controls page works on the land layout
		std::atomic<bool> g_swimTaskQueued{ false };
		std::atomic<int> g_swimFlips{ 0 };
		std::atomic<int> g_swimWaited{ 0 };                // frames a swap waited for a held key

		struct SavedMapping
		{
			std::string event;
			std::uint16_t key = kUnmapped;
			std::uint16_t modifier = 0;
		};
		std::array<std::vector<SavedMapping>, 3> g_land;   // the Gameplay mappings as they were on land; under g_lock

		struct HeldButton
		{
			int device = 0;
			std::uint32_t code = 0;
			std::int64_t seenMs = 0;
		};
		std::mutex g_heldLock;
		std::vector<HeldButton> g_held;

		std::int64_t NowMs()
		{
			return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
		}

		constexpr int kGameplayContext = 0;
		constexpr std::int64_t kSwimSettleMs = 150;  // the state must hold this long: wading at the waterline flickers

		std::vector<Bind>& LinesLocked(int a_layout) { return a_layout == kLayoutBoat ? g_boat : (a_layout == kLayoutHorse ? g_horse : g_swim); }
		const char* LayoutSection(int a_layout) { return a_layout == kLayoutBoat ? "Boat" : (a_layout == kLayoutHorse ? "Horseback" : "Swimming"); }

		// The layout the player's state asks for. The journal's Controls page always works on the land layout.
		int WantedLayout()
		{
			if (!settings::general::enabled || g_swimJournal.load()) { return kLayoutLand; }
			const int state = g_stateStable.load();
			if (state == kLayoutWater && g_haveSwimLines.load()) { return kLayoutWater; }
			if (state == kLayoutHorse && g_haveHorseLines.load()) { return kLayoutHorse; }
			if (state == kLayoutBoat && g_haveBoatLines.load()) { return kLayoutBoat; }
			return kLayoutLand;
		}
		constexpr std::int64_t kHeldForgetMs = 300;  // a held button repeats every frame; silence means its release was lost

		// The SWIMMING block: the Gameplay controls that mean something in water. The keyboard page also gets the movement
		// keys, which the controller has on its stick.
		struct SwimRow
		{
			const char* event;
			bool gamepad;
			const char* label = nullptr;  // the row's name when it is not the control's
		};
		constexpr SwimRow kSwimRows[] = {
			{ "Jump", true }, { "Sneak", true }, { "Sprint", true }, { "Activate", true }, { "Ready Weapon", true },
			{ "Right Attack/Block", true }, { "Left Attack/Block", true }, { "Shout", true },
			{ "Forward", false }, { "Back", false }, { "Strafe Left", false }, { "Strafe Right", false }, { "Run", false },
		};
		// The HORSEBACK block (the owner, 2026-11-03: "an actual separate control map, because you don't need something like
		// sneaking while on a horse"): the controls a rider can use. Jump rears or jumps, Sprint gallops, Activate dismounts,
		// weapons and bows work mounted, and the menus still open. The game's controlmap.txt has no mounted context to read this
		// from, so the list is by what the game does: Sneak does nothing mounted, and neither spells nor shouts can be used.
		constexpr SwimRow kHorseRows[] = {
			{ "Jump", true }, { "Sprint", true }, { "Activate", true }, { "Ready Weapon", true },
			{ "Right Attack/Block", true }, { "Left Attack/Block", true }, { "Toggle POV", true }, { "Auto-Move", true },
			{ "Tween Menu", true }, { "Favorites", true }, { "Wait", true }, { "Journal", true }, { "Pause", true },
			{ "Forward", false }, { "Back", false }, { "Strafe Left", false }, { "Strafe Right", false }, { "Run", false }, { "Toggle Always Run", false },
			{ "Quick Inventory", false }, { "Quick Magic", false }, { "Quick Stats", false }, { "Quick Map", false },
		};
		// HorsePower pets the horse on the Sneak user event while mounted, so with its Manual Horseback Petting on, Sneak has a job
		// on a horse: this row is that control under the name of what it does there. Its key is a real [Horseback] Sneak line.
		constexpr SwimRow kHorsePetRow[] = { { "Sneak", true, "Pet Horse (HorsePower)" } };
		// The BOAT block (1.6.0): every Gameplay control a player might want elsewhere while rowing, steering or seated in a
		// boat. Each row starts on its on-foot key; nothing is freed. A boat mod reads some of these itself (Oars and Sails:
		// Forward/Back/Strafe and the left stick row and steer, Activate stands you up, Auto-Move rows on), so a key moved here
		// moves that too.
		constexpr SwimRow kBoatRows[] = {
			{ "Jump", true }, { "Sprint", true }, { "Sneak", true }, { "Activate", true }, { "Ready Weapon", true },
			{ "Right Attack/Block", true }, { "Left Attack/Block", true }, { "Shout", true }, { "Toggle POV", true }, { "Auto-Move", true },
			{ "Tween Menu", true }, { "Favorites", true }, { "Wait", true }, { "Journal", true }, { "Pause", true },
			{ "Forward", false }, { "Back", false }, { "Strafe Left", false }, { "Strafe Right", false }, { "Run", false }, { "Toggle Always Run", false },
			{ "Quick Inventory", false }, { "Quick Magic", false }, { "Quick Stats", false }, { "Quick Map", false },
		};

		// A control the HORSEBACK layout takes off its keys while it is on, so they are free while riding. A line of its own wins.
		bool FreedWhileMounted(std::string_view a_event)
		{
			return IEquals(a_event, "Shout") || (IEquals(a_event, "Sneak") && !functions::ManualPettingOn());
		}

		// ---- requests from other mods (Unbinder.h) -------------------------------------------------------------------
		struct ModRequest
		{
			std::string plugin;
			std::string action;              // the user event or the mod's own action
			std::string label;               // the row's name
			int context = 0;                 // the engine's context index; 0 for a layout
			int layout = kLayoutLand;
			int device = 0;
			std::uint32_t asked = kUnmapped;
			std::uint32_t alternative = kUnmapped;
			std::uint16_t key = kUnmapped;   // what it has now
			int status = UVCRequestAPI::kQueued;
			bool accepted = false;           // has a mapping and a row
			bool pending = true;             // not decided yet
			std::string why;
		};
		std::vector<ModRequest> g_requests;            // under g_lock
		std::vector<RequestChoice> g_requestChoices;   // [ModRequests]; under g_lock
		std::atomic<bool> g_requestsReady{ false };    // UVC's own first apply has run
		constexpr int kRequestSheetContext = 1000;     // a request's row goes after the other rows of its pass

		// A function row's key, read before g_lock is taken (the Functions list has its own lock).
		struct FunctionKey
		{
			std::string name;
			int device = 0;
			std::uint16_t key = kUnmapped;
		};
		std::vector<FunctionKey> GameplayFunctionKeys()
		{
			std::vector<FunctionKey> out;
			const auto list = functions::GetFunctions();
			for (std::size_t i = 0; i < list.size(); ++i)
			{
				const auto& f = list[i];
				if (f.target.readOnly || !functions::IsPresentAt(i)) { continue; }
				if (!f.target.block.empty() && !IEquals(f.target.block, functions::kHorseBlock)) { continue; }  // another block is another context
				for (int d = 0; d < 3; ++d)
				{
					if (f.bind[d].key != functions::kUnbound) { out.push_back({ f.name, d, f.bind[d].key }); }
				}
			}
			return out;
		}

		std::string RequestContextName(const std::string_view a_fallback, int a_layout)
		{
			return a_layout != kLayoutLand ? std::string(LayoutSection(a_layout)) : std::string(a_fallback);
		}

		std::string RequestContextName(const ModRequest& a_r)
		{
			return RequestContextName(ContextName(a_r.context), a_r.layout);
		}

		std::string RequestRow(const ModRequest& a_r)
		{
			return std::format("Mod Request: {}: {}: {}", a_r.plugin, a_r.action, RequestContextName(a_r));
		}

		std::string KeyText(std::uint32_t a_key, int a_device)
		{
			if (a_key == kUnmapped) { return "none"; }
			const char* name = ButtonName(static_cast<std::uint16_t>(a_key), a_device);
			return name[0] ? std::string(name) : Hex(static_cast<std::uint16_t>(a_key));
		}

		bool ParseRequestContext(std::string_view a_name, int& a_context, int& a_layout)
		{
			a_context = 0;
			a_layout = kLayoutLand;
			if (IEquals(a_name, "Swimming")) { a_layout = kLayoutWater; return true; }
			if (IEquals(a_name, "Horseback")) { a_layout = kLayoutHorse; return true; }
			if (IEquals(a_name, "Boat")) { a_layout = kLayoutBoat; return true; }
			a_context = ContextIndex(a_name);
			return a_context >= 0;
		}

		bool SameRequest(const ModRequest& a_r, std::string_view a_plugin, std::string_view a_action, int a_context, int a_layout, int a_device)
		{
			return a_r.device == a_device && a_r.context == a_context && a_r.layout == a_layout && IEquals(a_r.plugin, a_plugin) && IEquals(a_r.action, a_action);
		}

		// Every mapping of a context on a device as it is on LAND: while a layout is on, the Gameplay array holds that layout, and the saved one is the truth.
		template <class F>
		void ForEachLandMapping(RE::ControlMap* a_map, int a_context, int a_device, F a_f)
		{
			if (a_context == kGameplayContext && g_layoutOn.load() != kLayoutLand)
			{
				for (const auto& s : g_land[a_device]) { a_f(std::string_view(s.event), s.key, s.modifier); }
				return;
			}
			if (auto* mappings = a_map ? MappingsFor(a_map, a_context, a_device) : nullptr)
			{
				for (const auto& m : *mappings) { a_f(std::string_view(m.eventID.c_str() ? m.eventID.c_str() : ""), m.inputKey, m.modifier); }
			}
		}

		// What holds a_key where a_r would use it, "" when nothing does. The same key under a land modifier is another input
		// to the engine, so it does not count. Caller holds g_lock.
		std::string KeyHolderLocked(RE::ControlMap* a_map, const std::vector<FunctionKey>& a_functions, const ModRequest& a_r, std::uint16_t a_key)
		{
			if (a_key == kUnmapped) { return {}; }
			if (a_r.device == 0 && IsAmfReserved(a_key)) { return "AMF's reserved keys"; }
			for (const auto& m : g_modifiers)
			{
				if (m.device == a_r.device && m.key == a_key) { return "the designated Modifier"; }
			}
			for (const auto& o : g_requests)
			{
				if (&o == &a_r || !o.accepted || o.device != a_r.device || o.key != a_key || o.context != a_r.context) { continue; }
				// a key on land holds in every layout of that context; the swimming and the horseback layout do not meet
				if (o.layout == a_r.layout || o.layout == kLayoutLand || a_r.layout == kLayoutLand) { return std::format("\"{}\" ({})", o.label, o.plugin); }
			}
			if (a_r.context == kGameplayContext)
			{
				for (const auto& f : a_functions)
				{
					if (f.device == a_r.device && f.key == a_key) { return std::format("\"{}\"", f.name); }
				}
			}
			std::string holder;
			const std::vector<Bind> noLines;
			const std::vector<Bind>& lines = a_r.layout != kLayoutLand ? LinesLocked(a_r.layout) : noLines;
			for (const auto& b : lines)
			{
				if (b.device == a_r.device && b.key == a_key && !IEquals(b.event, a_r.action)) { return std::format("\"{}\"", b.event); }
			}
			ForEachLandMapping(a_map, a_r.context, a_r.device, [&](std::string_view a_event, std::uint16_t a_k, std::uint16_t a_mod) {
				if (!holder.empty() || a_k != a_key || a_mod != 0 || IEquals(a_event, a_r.action)) { return; }
				if (a_r.layout != kLayoutLand)
				{
					// In a layout a control with a line of its own has left that key, and the horse layout frees its keys.
					const bool moved = std::any_of(lines.begin(), lines.end(), [&](const Bind& a_b) { return a_b.device == a_r.device && IEquals(a_b.event, a_event); });
					if (moved || (a_r.layout == kLayoutHorse && !g_horse.empty() && FreedWhileMounted(a_event))) { return; }
				}
				holder = std::format("\"{}\"", a_event);
			});
			return holder;
		}

		// The key a_r's action had from an earlier answer comes off the map (and off the saved land layout). Caller holds g_lock.
		void ClearGrantLocked(RE::ControlMap* a_map, const ModRequest& a_r, std::uint16_t a_key)
		{
			if (a_key == kUnmapped || a_r.layout != kLayoutLand) { return; }
			if (auto* mappings = a_map ? MappingsFor(a_map, a_r.context, a_r.device) : nullptr)
			{
				bool changed = false;
				for (auto* m : Find(*mappings, a_r.action))
				{
					if (m->inputKey == a_key) { m->inputKey = kUnmapped; changed = true; }
				}
				if (changed) { SortByKey(*mappings); }
			}
			if (a_r.context == kGameplayContext)
			{
				for (auto& s : g_land[a_r.device])
				{
					if (s.key == a_key && IEquals(s.event, a_r.action)) { s.key = kUnmapped; }
				}
			}
		}

		void DecideLocked(RE::ControlMap* a_map, const std::vector<FunctionKey>& a_functions, ModRequest& a_r)
		{
			const std::uint16_t previous = a_r.key;
			const std::string context = RequestContextName(a_r);
			const auto answer = [&](int a_status, std::uint16_t a_key, bool a_accepted, std::string a_why) {
				if (previous != a_key) { ClearGrantLocked(a_map, a_r, previous); }
				a_r.status = a_status;
				a_r.key = a_key;
				a_r.accepted = a_accepted;
				a_r.pending = false;
				a_r.why = std::move(a_why);
				logger::info("request: {}'s \"{}\" on {} in {} answered {}: key {}; {}", a_r.plugin, a_r.action, DeviceName(a_r.device), context, UVCRequestAPI::StatusName(a_status), KeyText(a_key, a_r.device), a_r.why);
			};
			if (!settings::general::enabled)
			{
				answer(UVCRequestAPI::kBadRequest, kUnmapped, false, "Unbind Vanilla Controls is switched off (bEnabled=0)");
				return;
			}
			for (const auto& c : g_requestChoices)
			{
				if (c.device == a_r.device && IEquals(c.plugin, a_r.plugin) && IEquals(c.action, a_r.action) && IEquals(c.context, context))
				{
					answer(UVCRequestAPI::kPlayerChoice, c.key, true, "the player set this row");
					return;
				}
			}
			// A control of the game that already has a key on this device has it by the player's map: that stands.
			if (a_r.layout != kLayoutLand)
			{
				for (const auto& b : LinesLocked(a_r.layout))
				{
					if (b.device == a_r.device && IEquals(b.event, a_r.action))
					{
						answer(UVCRequestAPI::kRefused, kUnmapped, false, std::format("the player's [{}] line for that control stands", LayoutSection(a_r.layout)));
						return;
					}
				}
			}
			else
			{
				std::uint16_t own = kUnmapped;
				ForEachLandMapping(a_map, a_r.context, a_r.device, [&](std::string_view a_event, std::uint16_t a_k, std::uint16_t) {
					if (own == kUnmapped && a_k != kUnmapped && a_k != previous && IEquals(a_event, a_r.action)) { own = a_k; }
				});
				if (own != kUnmapped)
				{
					answer(UVCRequestAPI::kRefused, kUnmapped, false, std::format("that control already has {} on {}; the player's binding stands", KeyText(own, a_r.device), DeviceName(a_r.device)));
					return;
				}
			}
			const std::uint16_t asked = a_r.asked > 0xFFFF ? kUnmapped : static_cast<std::uint16_t>(a_r.asked);
			const std::uint16_t alternative = a_r.alternative > 0xFFFF ? kUnmapped : static_cast<std::uint16_t>(a_r.alternative);
			if (asked == kUnmapped && alternative == kUnmapped)
			{
				answer(UVCRequestAPI::kGranted, kUnmapped, true, "a row with no key, as asked; the player can give it one");
				return;
			}
			const std::string askedHolder = KeyHolderLocked(a_map, a_functions, a_r, asked);
			if (asked != kUnmapped && askedHolder.empty())
			{
				answer(UVCRequestAPI::kGranted, asked, true, "the key is free");
				return;
			}
			const std::string reason = asked == kUnmapped ? std::string("no key asked for first") : std::format("{} holds {}", askedHolder, KeyText(asked, a_r.device));
			if (alternative != kUnmapped)
			{
				const std::string alternativeHolder = KeyHolderLocked(a_map, a_functions, a_r, alternative);
				if (alternativeHolder.empty())
				{
					answer(UVCRequestAPI::kAlternative, alternative, true, std::format("{}; the alternative is free", reason));
					return;
				}
				answer(UVCRequestAPI::kRefused, kUnmapped, false, std::format("{}, and {} holds {}", reason, alternativeHolder, KeyText(alternative, a_r.device)));
				return;
			}
			answer(UVCRequestAPI::kRefused, kUnmapped, false, reason);
		}

		void RebuildRequestRowsLocked()
		{
			std::erase_if(g_delinked, [](const DelinkedAction& a_d) { return a_d.modRequest; });
			for (const auto& r : g_requests)
			{
				if (!r.accepted) { continue; }
				const std::string row = RequestRow(r);
				auto it = std::find_if(g_delinked.begin(), g_delinked.end(), [&](const DelinkedAction& a_d) { return a_d.row == row; });
				if (it == g_delinked.end())
				{
					DelinkedAction d;
					d.context = r.context;
					d.event = r.action;
					d.row = row;
					d.block = "Mod Requests";
					d.title = r.layout == kLayoutWater ? "SWIMMING" : (r.layout == kLayoutHorse ? "HORSEBACK" : (r.layout == kLayoutBoat ? "BOAT" : "MOD REQUESTS"));
					d.action = std::format("{} ({})", r.label, r.plugin);
					d.layout = r.layout;
					d.sheetContext = kRequestSheetContext;
					d.modRequest = true;
					g_delinked.push_back(std::move(d));
					it = std::prev(g_delinked.end());
				}
				if (r.device == 2) { it->gamepad = true; }
				else { it->keyboardFamily = true; }
			}
		}

		bool RequestWantsLayoutLocked(int a_layout)
		{
			return std::any_of(g_requests.begin(), g_requests.end(), [&](const ModRequest& a_r) { return a_r.accepted && a_r.layout == a_layout && a_r.key != kUnmapped; });
		}

		// A layout is on when the player has lines for it or a request holds a key in it.
		void RefreshHaveLinesLocked()
		{
			g_haveSwimLines.store(!g_swim.empty() || RequestWantsLayoutLocked(kLayoutWater));
			g_haveHorseLines.store(!g_horse.empty() || RequestWantsLayoutLocked(kLayoutHorse));
			g_haveBoatLines.store(!g_boat.empty() || RequestWantsLayoutLocked(kLayoutBoat));
		}

		// A mapping of the request's action exists on its device from here on: a mod's own action is a new user event on the
		// engine's array, with no group flags (no disabled-controls state can gate it) and not remappable (the game's Controls
		// page does not list it; UVC's row does). Key 0xFF until something gives it one.
		RE::ControlMap::UserEventMapping* EnsureRequestMapping(Mappings& a_mappings, const ModRequest& a_r)
		{
			auto found = Find(a_mappings, a_r.action);
			if (!found.empty()) { return found.front(); }
			RE::ControlMap::UserEventMapping mapping{};
			mapping.eventID = a_r.action.c_str();
			mapping.inputKey = kUnmapped;
			mapping.modifier = 0;
			mapping.indexInContext = 127;
			mapping.remappable = false;
			mapping.linked = false;
			a_mappings.push_back(mapping);
			logger::info("request: {}'s \"{}\" has a mapping of its own on {} in {}", a_r.plugin, a_r.action, DeviceName(a_r.device), RequestContextName(a_r));
			found = Find(a_mappings, a_r.action);
			return found.empty() ? nullptr : found.front();
		}

		// The accepted requests onto the land layout, after the player's lists: what a player's line took is the request's loss.
		// Caller holds g_lock.
		void ApplyRequestsLocked(RE::ControlMap* a_map, const char* a_reason)
		{
			std::vector<Mappings*> dirty;
			bool rowsChanged = false;
			for (auto& r : g_requests)
			{
				if (!r.accepted) { continue; }
				auto* mappings = MappingsFor(a_map, r.context, r.device);
				if (!mappings)
				{
					logger::warn("request ({}): {}'s \"{}\": context {} is not loaded", a_reason, r.plugin, r.action, RequestContextName(r));
					continue;
				}
				auto* mapping = EnsureRequestMapping(*mappings, r);
				if (std::find(dirty.begin(), dirty.end(), mappings) == dirty.end()) { dirty.push_back(mappings); }
				if (!mapping || r.layout != kLayoutLand) { continue; }
				std::string holder;
				if (r.key != kUnmapped)
				{
					for (const auto& m : *mappings)
					{
						if (m.inputKey == r.key && m.modifier == 0 && m.eventID.c_str() && !IEquals(m.eventID.c_str(), r.action))
						{
							holder = m.eventID.c_str();
							break;
						}
					}
				}
				if (!holder.empty())
				{
					logger::warn("request ({}): {}'s \"{}\" lost {} to \"{}\"", a_reason, r.plugin, r.action, KeyText(r.key, r.device), holder);
					r.why = std::format("\"{}\" took {}", holder, KeyText(r.key, r.device));
					r.status = UVCRequestAPI::kDisplaced;
					r.key = kUnmapped;
					rowsChanged = true;
				}
				mapping->inputKey = r.key;
				mapping->modifier = 0;
				mapping->linked = false;
			}
			for (auto* m : dirty) { SortByKey(*m); }
			if (rowsChanged) { RebuildRequestRowsLocked(); }
		}

		bool IsRequestEventLocked(std::string_view a_event, int a_device)
		{
			return std::any_of(g_requests.begin(), g_requests.end(), [&](const ModRequest& a_r) { return a_r.context == kGameplayContext && a_r.device == a_device && IEquals(a_r.action, a_event); });
		}

		// The game's own main thread, as the engine records it (RE::Main::threadID). Two earlier ways of noting it (the
		// thread the last apply ran on; then the thread of the first SKSE task after kDataLoaded) both picked a loader
		// thread after a load from the main menu (combined runs B-20261105-231553-63a6 and B-20261106-000532-7e48).
		bool OnGameThread()
		{
			const auto* main = RE::Main::GetSingleton();
			return main && main->threadID != 0 && main->threadID == REX::W32::GetCurrentThreadId();
		}

		struct MapRow
		{
			const char* event;
			bool keyboard;
			bool gamepad;
		};
		// The analog inputs (Look, Cursor) and the mouse-only ones (MapLookMode, LocalMapMoveMode) are not rebindable presses.
		constexpr MapRow kMapRows[] = {
			{ "Cancel", true, true }, { "Click", false, true }, { "PlayerPosition", true, true }, { "LocalMap", true, true },
			{ "Zoom In", false, true }, { "Zoom Out", false, true },
			{ "Up", true, false }, { "Down", true, false }, { "Left", true, false }, { "Right", true, false },
		};

		// The SWIMMING, HORSEBACK, BOAT and IN MAP rows join the menu rows' list once per session (caller holds g_lock). A row is
		// listed only for a control the map in force really has. The map's actions are not links in every control map, so
		// IN MAP cannot depend on the link conversion: the owner's map and the shipped Fixed and Cleaned one have few or none.
		void RecordContextRows(RE::ControlMap* a_map)
		{
			const auto addRows = [&](const auto& a_rows, int a_layout, const char* a_block, const char* a_title) {
				for (const auto& r : a_rows)
				{
					const std::string row = std::string(a_block) + ": " + r.event;
					if (std::any_of(g_delinked.begin(), g_delinked.end(), [&](const DelinkedAction& d) { return d.row == row; })) { continue; }
					bool keyboard = false, gamepad = false;
					for (int d = 0; d < 3; ++d)
					{
						auto* mappings = MappingsFor(a_map, kGameplayContext, d);
						if (!mappings || Find(*mappings, r.event).empty()) { continue; }
						if (d == 2) { gamepad = r.gamepad; }
						else { keyboard = true; }
					}
					if (!keyboard && !gamepad) { continue; }
					DelinkedAction d;
					d.context = kGameplayContext;
					d.event = r.event;
					d.row = row;
					d.block = a_block;
					d.title = a_title;
					d.action = r.label ? r.label : r.event;
					d.keyboardFamily = keyboard;
					d.gamepad = gamepad;
					d.layout = a_layout;
					d.sheetContext = kGameplayContext;
					g_delinked.push_back(std::move(d));
				}
			};
			addRows(kSwimRows, kLayoutWater, "Swimming", "SWIMMING");
			addRows(kHorseRows, kLayoutHorse, "Horseback", "HORSEBACK");
			if (functions::ManualPettingOn()) { addRows(kHorsePetRow, kLayoutHorse, "Horseback", "HORSEBACK"); }
			addRows(kBoatRows, kLayoutBoat, "Boat", "BOAT");

			// IN MAP (the owner, 2026-11-03: "if there's not already a map context ... we need to add"). The engine's Map
			// context has always been there and [Bound] lines already reach it; what was missing is rows for it.
			const int mapContext = ContextIndex("Map");
			for (const auto& r : kMapRows)
			{
				const auto hasKey = [&](int a_device) {
					auto* mappings = MappingsFor(a_map, mapContext, a_device);
					if (!mappings) { return false; }
					const auto found = Find(*mappings, r.event);
					return std::any_of(found.begin(), found.end(), [](const RE::ControlMap::UserEventMapping* a_m) { return a_m->inputKey != kUnmapped; });
				};
				if (r.gamepad && hasKey(2)) { RecordDelinked(mapContext, r.event, 2); }
				if (r.keyboard && hasKey(0)) { RecordDelinked(mapContext, r.event, 0); }
			}
		}

		// Caller holds g_lock.
		void RestoreLandLocked(RE::ControlMap* a_map, const char* a_reason)
		{
			if (g_layoutOn.load() == kLayoutLand) { return; }
			for (int d = 0; d < 3; ++d)
			{
				auto* mappings = MappingsFor(a_map, kGameplayContext, d);
				if (!mappings) { continue; }
				// The mappings of one control differ only in their key, so each takes the next saved key of its control.
				std::vector<bool> used(g_land[d].size(), false);
				for (auto& m : *mappings)
				{
					const char* id = m.eventID.c_str();
					bool found = false;
					for (std::size_t i = 0; id && i < g_land[d].size() && !found; ++i)
					{
						if (used[i] || !IEquals(g_land[d][i].event, id)) { continue; }
						used[i] = true;
						m.inputKey = g_land[d][i].key;
						m.modifier = g_land[d][i].modifier;
						found = true;
					}
					// a mapping the water layout had to create for this device
					if (!found) { m.inputKey = kUnmapped; m.modifier = 0; }
				}
				SortByKey(*mappings);
			}
			g_layoutOn.store(kLayoutLand);
			g_swimFlips.fetch_add(1);
			logger::info("control layout ({}): the land layout is back", a_reason);
		}

		// Caller holds g_lock.
		void ApplyLayoutLocked(RE::ControlMap* a_map, int a_layout, const char* a_reason)
		{
			const char* section = LayoutSection(a_layout);
			for (int d = 0; d < 3; ++d)
			{
				g_land[d].clear();
				auto* mappings = MappingsFor(a_map, kGameplayContext, d);
				if (!mappings) { continue; }
				for (const auto& m : *mappings)
				{
					g_land[d].push_back({ m.eventID.c_str() ? m.eventID.c_str() : "", m.inputKey, m.modifier });
				}
			}
			std::string moved;
			for (int d = 0; d < 3; ++d)
			{
				auto* mappings = MappingsFor(a_map, kGameplayContext, d);
				if (!mappings) { continue; }
				std::vector<const Bind*> lines;
				for (const auto& b : LinesLocked(a_layout))
				{
					if (b.device == d) { lines.push_back(&b); }
				}
				const auto isLine = [&](const char* a_event) {
					return std::any_of(lines.begin(), lines.end(), [&](const Bind* a_b) { return IEquals(a_b->event, a_event); });
				};
				// "With no lines nothing changes": a layout that is on only for a mod's request frees nothing of the player's.
				if (a_layout == kLayoutHorse && !g_horse.empty())
				{
					for (auto& m : *mappings)
					{
						if (m.inputKey == kUnmapped || !m.eventID.c_str() || !FreedWhileMounted(m.eventID.c_str()) || isLine(m.eventID.c_str())) { continue; }
						moved += std::format("{}{} {} free", moved.empty() ? "" : ", ", m.eventID.c_str(), DeviceName(d));
						m.inputKey = kUnmapped;
					}
				}
				const auto requestsHere = [&]() {
					// After the player's lines, and never taking a key: what holds it now keeps it.
					for (const auto& r : g_requests)
					{
						if (!r.accepted || r.layout != a_layout || r.device != d || r.key == kUnmapped) { continue; }
						const auto held = std::find_if(mappings->begin(), mappings->end(), [&](const RE::ControlMap::UserEventMapping& a_m) {
							return a_m.inputKey == r.key && a_m.modifier == 0 && a_m.eventID.c_str() && !IEquals(a_m.eventID.c_str(), r.action);
						});
						if (held != mappings->end())
						{
							logger::warn("{} ({}): {}'s \"{}\" -> {} not applied: \"{}\" holds it", section, a_reason, r.plugin, r.action, Hex(r.key), held->eventID.c_str());
							continue;
						}
						auto* mapping = EnsureRequestMapping(*mappings, r);
						if (!mapping) { continue; }
						mapping->inputKey = r.key;
						mapping->modifier = 0;
						moved += std::format("{}{} {} {}", moved.empty() ? "" : ", ", r.action, DeviceName(d), Hex(r.key));
					}
				};
				if (lines.empty())
				{
					requestsHere();
					SortByKey(*mappings);
					continue;
				}
				// A key a line takes leaves whatever holds it: this is the point of the context. The same key with a land
				// modifier is another input to the engine, so it stays.
				for (const auto* b : lines)
				{
					if (b->key == kUnmapped) { continue; }
					for (auto& m : *mappings)
					{
						if (m.inputKey != b->key || m.modifier != 0 || !m.eventID.c_str() || isLine(m.eventID.c_str())) { continue; }
						moved += std::format("{}{} loses {}", moved.empty() ? "" : ", ", m.eventID.c_str(), Hex(b->key));
						m.inputKey = kUnmapped;
					}
				}
				for (const auto* b : lines)
				{
					for (auto* m : Find(*mappings, b->event))
					{
						m->inputKey = kUnmapped;
						m->modifier = 0;
					}
				}
				std::vector<std::uint16_t> given;
				for (const auto* b : lines)
				{
					if (b->key == kUnmapped)
					{
						moved += std::format("{}{} {} none", moved.empty() ? "" : ", ", b->event, DeviceName(d));
						continue;
					}
					if (d == 0 && IsAmfReserved(b->key))
					{
						logger::warn("{} ({}): {}|keyboard -> {} refused: AMF reserves that key", section, a_reason, b->event, Hex(b->key));
						continue;
					}
					if (std::find(given.begin(), given.end(), b->key) != given.end())
					{
						logger::warn("{} ({}): {}|{} -> {} not applied: an earlier line of that block was given it first", section, a_reason, b->event, DeviceName(d), Hex(b->key));
						continue;
					}
					auto found = Find(*mappings, b->event);
					if (found.empty())
					{
						// no mapping on this device: copied from another device, as a [Bound] line does
						RE::ControlMap::UserEventMapping copy{};
						bool have = false;
						for (int other = 0; other < 3 && !have; ++other)
						{
							if (other == d) { continue; }
							auto* theirs = MappingsFor(a_map, kGameplayContext, other);
							if (!theirs) { continue; }
							auto theirFound = Find(*theirs, b->event);
							if (!theirFound.empty())
							{
								copy = *theirFound.front();
								have = true;
							}
						}
						if (!have)
						{
							logger::warn("{} ({}): {} is not a control this game has; the line does nothing", section, a_reason, b->event);
							continue;
						}
						copy.inputKey = b->key;
						copy.modifier = 0;
						copy.linked = false;
						mappings->push_back(copy);
					}
					else { found.front()->inputKey = b->key; }
					given.push_back(b->key);
					moved += std::format("{}{} {} {}", moved.empty() ? "" : ", ", b->event, DeviceName(d), Hex(b->key));
				}
				requestsHere();
				SortByKey(*mappings);
			}
			g_layoutOn.store(a_layout);
			g_swimFlips.fetch_add(1);
			logger::info("{} ({}): that layout is on: {}", section, a_reason, moved);
		}

		// True while a key the swap would move is down. Caller holds g_lock.
		bool SwapWouldMoveHeldKeyLocked(RE::ControlMap* a_map)
		{
			std::vector<std::pair<int, std::uint32_t>> keys;
			const int on = g_layoutOn.load();
			const auto note = [&](const std::vector<Bind>& a_lines) {
				for (const auto& b : a_lines)
				{
					if (b.key != kUnmapped) { keys.emplace_back(b.device, b.key); }
					if (on != kLayoutLand)
					{
						for (const auto& s : g_land[b.device])
						{
							if (IEquals(s.event, b.event) && s.key != kUnmapped) { keys.emplace_back(b.device, s.key); }
						}
					}
					else if (auto* mappings = MappingsFor(a_map, kGameplayContext, b.device))
					{
						for (auto* m : Find(*mappings, b.event))
						{
							if (m->inputKey != kUnmapped) { keys.emplace_back(b.device, m->inputKey); }
						}
					}
				}
			};
			// The layout going off and the one coming on.
			if (on != kLayoutLand) { note(LinesLocked(on)); }
			const int wantedLayout = WantedLayout();
			if (wantedLayout != kLayoutLand && wantedLayout != on) { note(LinesLocked(wantedLayout)); }
			for (const auto& r : g_requests)
			{
				if (r.accepted && r.key != kUnmapped && r.layout != kLayoutLand && (r.layout == on || r.layout == wantedLayout)) { keys.emplace_back(r.device, r.key); }
			}
			// The controls the HORSEBACK layout frees move too.
			if (!g_horse.empty() && (on == kLayoutHorse || wantedLayout == kLayoutHorse))
			{
				for (int d = 0; d < 3; ++d)
				{
					if (on == kLayoutHorse)
					{
						for (const auto& s : g_land[d])
						{
							if (s.key != kUnmapped && FreedWhileMounted(s.event)) { keys.emplace_back(d, s.key); }
						}
					}
					else if (auto* mappings = MappingsFor(a_map, kGameplayContext, d))
					{
						for (const auto& m : *mappings)
						{
							if (m.inputKey != kUnmapped && m.eventID.c_str() && FreedWhileMounted(m.eventID.c_str())) { keys.emplace_back(d, m.inputKey); }
						}
					}
				}
			}
			const auto now = NowMs();
			std::scoped_lock h(g_heldLock);
			std::erase_if(g_held, [&](const HeldButton& a_h) { return now - a_h.seenMs > kHeldForgetMs; });
			return std::any_of(g_held.begin(), g_held.end(), [&](const HeldButton& a_h) {
				return std::find(keys.begin(), keys.end(), std::pair<int, std::uint32_t>{ a_h.device, a_h.code }) != keys.end();
			});
		}

		void SwimSync()
		{
			g_swimTaskQueued.store(false);
			auto* map = RE::ControlMap::GetSingleton();
			if (!map) { return; }
			std::scoped_lock l(g_lock);
			const int wanted = WantedLayout();
			if (wanted == g_layoutOn.load()) { return; }
			// The journal's Controls page is never made to wait: it needs the land layout now.
			if (!g_swimJournal.load() && SwapWouldMoveHeldKeyLocked(map))
			{
				g_swimWaited.fetch_add(1);
				return;  // the next HUD frame asks again
			}
			RestoreLandLocked(map, g_swimJournal.load() ? "journal open" : "the player's state changed");
			if (wanted != kLayoutLand) { ApplyLayoutLocked(map, wanted, LayoutWhere(wanted)); }
		}

		// A horse, or any other mount but a dragon: a dragon's back is its own set of controls and stays as it is.
		bool OnHorseback(RE::PlayerCharacter* a_player)
		{
			if (!a_player->IsOnMount()) { return false; }
			RE::NiPointer<RE::Actor> mount;
			if (!a_player->GetMount(mount) || !mount) { return false; }
			return !mount->HasKeywordString("ActorTypeDragon");
		}

		// ---- The boat context's detection (Unbinder.h, THE BOAT CONTEXT) ----
		std::mutex g_boatCfgLock;
		std::vector<std::string> g_boatPluginNames{ "OarsAndSails.esl", "Longship.esp" };  // [BoatDetection] sPlugins
		std::vector<std::string> g_boatKeywordNames;                                      // [BoatDetection] sKeywords
		std::atomic<bool> g_boatSeatedRide{ true };                                       // [BoatDetection] bSeatedRide
		std::vector<const RE::TESFile*> g_boatFiles;      // the listed plugins that are loaded; under g_boatCfgLock
		std::vector<RE::BGSKeyword*> g_boatKeywords;      // the listed keywords that exist; under g_boatCfgLock
		std::atomic<bool> g_boatArmed{ false };           // a listed plugin is loaded (or a listed keyword exists)
		std::atomic<bool> g_boatDataLoaded{ false };
		std::atomic<int> g_boatHow{ 0 };                  // 0 none, 1 furniture, 2 mount, 3 seated ride: the last reading, for op=boat
		std::atomic<std::uint32_t> g_boatFormID{ 0 };     // the furniture or mount it read, 0 for a seated ride

		const char* BoatHowName(int a_how)
		{
			switch (a_how)
			{
			case 1: return "furniture";
			case 2: return "mount";
			case 3: return "seated ride";
			default: return "none";
			}
		}

		void ResolveBoatDetection(const char* a_when)
		{
			auto* dh = RE::TESDataHandler::GetSingleton();
			if (!dh) { return; }
			std::scoped_lock l(g_boatCfgLock);
			g_boatFiles.clear();
			g_boatKeywords.clear();
			std::string found, missing;
			for (const auto& name : g_boatPluginNames)
			{
				const RE::TESFile* file = dh->LookupModByName(name);
				if (file && file->compileIndex != 0xFF)
				{
					g_boatFiles.push_back(file);
					found += std::format("{}{}", found.empty() ? "" : ", ", name);
				}
				else { missing += std::format("{}{}", missing.empty() ? "" : ", ", name); }
			}
			for (const auto& name : g_boatKeywordNames)
			{
				auto* keyword = RE::TESForm::LookupByEditorID<RE::BGSKeyword>(name);
				if (keyword)
				{
					g_boatKeywords.push_back(keyword);
					found += std::format("{}keyword {}", found.empty() ? "" : ", ", name);
				}
				else { missing += std::format("{}keyword {}", missing.empty() ? "" : ", ", name); }
			}
			g_boatArmed.store(!g_boatFiles.empty() || !g_boatKeywords.empty());
			logger::info("boat context ({}): {}; loaded: {}; not loaded: {}; seated ride {}", a_when, g_boatArmed.load() ? "armed" : "off (no listed boat plugin or keyword is loaded)",
				found.empty() ? "none" : found, missing.empty() ? "none" : missing, g_boatSeatedRide.load() ? "counts" : "does not count");
		}

		// A form whose first file is a listed plugin, or that carries a listed keyword. Caller holds g_boatCfgLock.
		bool IsBoatFormLocked(RE::TESForm* a_form)
		{
			if (!a_form) { return false; }
			if (const auto* file = a_form->GetFile(0))
			{
				if (std::find(g_boatFiles.begin(), g_boatFiles.end(), file) != g_boatFiles.end()) { return true; }
			}
			if (!g_boatKeywords.empty())
			{
				if (const auto* keywords = a_form->As<RE::BGSKeywordForm>())
				{
					for (auto* k : g_boatKeywords)
					{
						if (keywords->HasKeyword(k)) { return true; }
					}
				}
			}
			return false;
		}

		// How far the player is above (+) or below (-) the water of the cell they are in; false where there is no water.
		bool WaterGap(RE::PlayerCharacter* a_player, float& a_gap)
		{
			auto* cell = a_player->GetParentCell();
			if (!cell) { return false; }
			float water = 0.0f;
			const RE::NiPoint3 at = a_player->GetPosition();
			if (!cell->GetWaterHeight(at, water) || water < -1.0e6f || water > 1.0e6f) { return false; }
			a_gap = at.z - water;
			return true;
		}

		// Oars and Sails' seat (RowRim SitDown / SetControls, read from its source): for the whole ride it switches off the
		// movement, fighting, jumping, sneaking and activate controls and leaves looking and the menus on (a scene's
		// DisablePlayerControls takes the menus too), and the seat is on the water. The player's collision, which it also
		// turns off, is not readable from the reference's flags (1.6.0's first test: HasCollision stayed true while seated).
		bool SeatedRideSignature(RE::PlayerCharacter* a_player)
		{
			using F = RE::UserEvents::USER_EVENT_FLAG;
			const auto* map = RE::ControlMap::GetSingleton();
			if (!map || map->IsMovementControlsEnabled() || map->IsActivateControlsEnabled() || map->AreControlsEnabled(F::kJumping) ||
				map->AreControlsEnabled(F::kSneaking) || !map->IsMenuControlsEnabled() || !map->IsLookingControlsEnabled())
			{
				return false;
			}
			float gap = 0.0f;
			return WaterGap(a_player, gap) && gap > -60.0f && gap < 200.0f;  // seated in a hull: a little above the water
		}

		// Is the player rowing, steering or seated in a boat? Cheap: a flag check and at most two form lookups a frame.
		int BoatReading(RE::PlayerCharacter* a_player, std::uint32_t& a_formID)
		{
			a_formID = 0;
			if (!g_boatArmed.load(std::memory_order_relaxed)) { return 0; }
			std::scoped_lock l(g_boatCfgLock);
			if (const auto furniture = a_player->GetOccupiedFurniture().get())
			{
				if (IsBoatFormLocked(furniture->GetBaseObject()) || IsBoatFormLocked(furniture.get()))
				{
					a_formID = furniture->GetFormID();
					return 1;
				}
				return 0;  // a chair on a ship's deck is a chair
			}
			if (a_player->IsOnMount())
			{
				RE::NiPointer<RE::Actor> mount;
				if (a_player->GetMount(mount) && mount && (IsBoatFormLocked(mount->GetActorBase()) || IsBoatFormLocked(mount.get())))
				{
					a_formID = mount->GetFormID();
					return 2;
				}
				return 0;
			}
			if (g_boatSeatedRide.load(std::memory_order_relaxed) && !g_boatFiles.empty() && SeatedRideSignature(a_player)) { return 3; }
			return 0;
		}

		// Every frame the HUD advances. Reads the player's real swimming and mounted state, the ones the game's own conditions
		// read, and only those some line needs. Runs on the UI job, so the control map itself is only changed from the task it
		// queues (as the journal's close does).
		void OnHudFrame()
		{
			const bool haveSwim = g_haveSwimLines.load(std::memory_order_relaxed);
			const bool haveHorse = g_haveHorseLines.load(std::memory_order_relaxed);
			const bool haveBoat = g_haveBoatLines.load(std::memory_order_relaxed);
			if (!haveSwim && !haveHorse && !haveBoat && g_layoutOn.load(std::memory_order_relaxed) == kLayoutLand) { return; }
			auto* player = RE::PlayerCharacter::GetSingleton();
			const auto* state = player ? player->AsActorState() : nullptr;
			if (!state) { return; }
			static int s_last = kLayoutLand;
			static std::int64_t s_changedAt = 0;
			std::uint32_t boatForm = 0;
			const int boatHow = haveBoat ? BoatReading(player, boatForm) : 0;
			const int current = boatHow != 0 ? kLayoutBoat :
			                    (haveHorse && OnHorseback(player) ? kLayoutHorse : (haveSwim && state->IsSwimming() ? kLayoutWater : kLayoutLand));
			const auto now = NowMs();
			if (current != s_last)
			{
				if (current == kLayoutBoat || s_last == kLayoutBoat)
				{
					logger::info("boat context: the player is {} ({}{})", current == kLayoutBoat ? "in a boat" : "out of the boat", BoatHowName(current == kLayoutBoat ? boatHow : g_boatHow.load()),
						boatForm ? std::format(", {:08X}", boatForm) : std::string());
				}
				s_last = current;
				s_changedAt = now;
			}
			g_boatHow.store(boatHow, std::memory_order_relaxed);
			g_boatFormID.store(boatForm, std::memory_order_relaxed);
			if (now - s_changedAt >= kSwimSettleMs) { g_stateStable.store(s_last); }
			if (WantedLayout() == g_layoutOn.load() || g_swimTaskQueued.exchange(true)) { return; }
			if (auto* tasks = SKSE::GetTaskInterface()) { tasks->AddTask([]() { SwimSync(); }); }
			else { g_swimTaskQueued.store(false); }
		}

		struct HudAdvanceMovie
		{
			static void thunk(RE::HUDMenu* a_menu, float a_interval, std::uint32_t a_currentTime)
			{
				func(a_menu, a_interval, a_currentTime);
				OnHudFrame();
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};
	}

	void InstallSwimWatch()
	{
		REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_HUDMenu[0] };
		HudAdvanceMovie::func = vtbl.write_vfunc(0x5, HudAdvanceMovie::thunk);
		logger::info("hook: HUDMenu::AdvanceMovie (vtable {:X}, slot 5) wrapped; the player's swimming state is read after each HUD frame", vtbl.address());
	}

	void OnJournalForSwim(bool a_open)
	{
		g_swimJournal.store(a_open);
		// The HUD does not always advance behind the journal, so its opening asks for the land layout itself.
		if (a_open && g_layoutOn.load() != kLayoutLand && !g_swimTaskQueued.exchange(true))
		{
			if (auto* tasks = SKSE::GetTaskInterface()) { tasks->AddTask([]() { SwimSync(); }); }
			else { g_swimTaskQueued.store(false); }
		}
	}

	void NoteButton(int a_device, std::uint32_t a_code, bool a_up)
	{
		if (!g_haveSwimLines.load(std::memory_order_relaxed) && !g_haveHorseLines.load(std::memory_order_relaxed) && !g_haveBoatLines.load(std::memory_order_relaxed)) { return; }
		const auto now = NowMs();
		std::scoped_lock h(g_heldLock);
		const auto it = std::find_if(g_held.begin(), g_held.end(), [&](const HeldButton& a_h) { return a_h.device == a_device && a_h.code == a_code; });
		if (a_up)
		{
			if (it != g_held.end()) { g_held.erase(it); }
		}
		else if (it != g_held.end()) { it->seenMs = now; }
		else { g_held.push_back({ a_device, a_code, now }); }
	}

	std::vector<Bind> GetSwimBinds()
	{
		std::scoped_lock l(g_lock);
		return g_swim;
	}

	void SetSwimBinds(std::vector<Bind> a_binds)
	{
		std::scoped_lock l(g_lock);
		g_swim = std::move(a_binds);
		RefreshHaveLinesLocked();
	}

	std::vector<Bind> GetHorseBinds()
	{
		std::scoped_lock l(g_lock);
		return g_horse;
	}

	void SetHorseBinds(std::vector<Bind> a_binds)
	{
		std::scoped_lock l(g_lock);
		g_horse = std::move(a_binds);
		RefreshHaveLinesLocked();
	}

	void SetBoatDetection(std::vector<std::string> a_plugins, std::vector<std::string> a_keywords, bool a_seatedRide)
	{
		{
			std::scoped_lock l(g_boatCfgLock);
			g_boatPluginNames = std::move(a_plugins);
			g_boatKeywordNames = std::move(a_keywords);
		}
		g_boatSeatedRide.store(a_seatedRide);
		if (g_boatDataLoaded.load()) { ResolveBoatDetection("INI read again"); }
	}

	void BoatAtDataLoaded()
	{
		g_boatDataLoaded.store(true);
		ResolveBoatDetection("data loaded");
	}

	std::vector<Bind> GetBoatBinds()
	{
		std::scoped_lock l(g_lock);
		return g_boat;
	}

	void SetBoatBinds(std::vector<Bind> a_binds)
	{
		std::scoped_lock l(g_lock);
		g_boat = std::move(a_binds);
		RefreshHaveLinesLocked();
	}

	const char* LayoutWhere(int a_layout)
	{
		return a_layout == kLayoutBoat ? "in a boat" : (a_layout == kLayoutHorse ? "on horseback" : "in water");
	}

	const char* LayoutSectionName(int a_layout)
	{
		return LayoutSection(a_layout);
	}

	std::uint16_t LayoutShownKey(int a_layout, std::string_view a_event, int a_device)
	{
		auto* map = RE::ControlMap::GetSingleton();
		if (!map || a_device < 0 || a_device > 2) { return kUnmapped; }
		std::scoped_lock l(g_lock);
		const auto& lines = LinesLocked(a_layout);
		for (const auto& b : lines)
		{
			if (b.device == a_device && IEquals(b.event, a_event)) { return b.key; }
		}
		// No line: the land key - unless a line took it, and then this control has no key in this layout
		std::uint16_t land = kUnmapped;
		if (g_layoutOn.load() != kLayoutLand)
		{
			for (const auto& s : g_land[a_device])
			{
				if (IEquals(s.event, a_event) && s.key != kUnmapped)
				{
					land = s.key;
					break;
				}
			}
		}
		else if (auto* mappings = MappingsFor(map, kGameplayContext, a_device))
		{
			for (auto* m : Find(*mappings, a_event))
			{
				if (m->inputKey != kUnmapped)
				{
					land = m->inputKey;
					break;
				}
			}
		}
		for (const auto& b : lines)
		{
			if (b.device == a_device && b.key == land) { return kUnmapped; }
		}
		return land;
	}

	bool SetLayoutKey(int a_layout, std::string_view a_event, int a_device, std::uint16_t a_key, std::string& a_why)
	{
		if (a_key != kUnmapped && a_device == 0 && IsAmfReserved(a_key))
		{
			a_why = "AMF reserves that key";
			return false;
		}
		std::scoped_lock l(g_lock);
		auto& lines = LinesLocked(a_layout);
		for (const auto& b : lines)
		{
			if (a_key != kUnmapped && b.device == a_device && b.key == a_key && !IEquals(b.event, a_event))
			{
				a_why = std::format("\"{}\" already holds it {}", b.event, LayoutWhere(a_layout));
				return false;
			}
		}
		const auto it = std::find_if(lines.begin(), lines.end(), [&](const Bind& b) { return b.device == a_device && IEquals(b.event, a_event); });
		if (it != lines.end()) { it->key = a_key; }
		else
		{
			Bind b;
			b.context = LayoutSection(a_layout);
			b.event = std::string(a_event);
			b.device = a_device;
			b.key = a_key;
			lines.push_back(std::move(b));
		}
		(a_layout == kLayoutBoat ? g_haveBoatLines : (a_layout == kLayoutHorse ? g_haveHorseLines : g_haveSwimLines)).store(true);
		return true;
	}

	namespace
	{
		// The boat block of op=swim/horse/boat: what each signal reads now, so a wrong answer shows which one. Caller holds g_lock.
		std::string BoatStateJsonLocked(RE::PlayerCharacter* a_player, int a_stable, int a_on, const std::string& a_lines)
		{
			std::uint32_t form = 0;
			const int how = a_player ? BoatReading(a_player, form) : 0;
			const auto* map = RE::ControlMap::GetSingleton();
			const auto furniture = a_player ? a_player->GetOccupiedFurniture().get() : nullptr;
			float gap = -99999.0f;
			if (a_player) { WaterGap(a_player, gap); }
			std::string plugins;
			{
				std::scoped_lock l(g_boatCfgLock);
				for (const auto* f : g_boatFiles) { plugins += std::format(R"({}"{}")", plugins.empty() ? "" : ",", EscapeJson(f->GetFilename())); }
			}
			return std::format(R"(,"boat":{{"armed":{},"loadedPlugins":[{}],"seatedRideCounts":{},"inBoat":{},"how":"{}","formID":"{:08X}","settled":{},"boatLayoutOn":{},)"
			                   R"("signals":{{"movementControls":{},"activateControls":{},"jumpingControls":{},"sneakingControls":{},"menuControls":{},"lookingControls":{},"waterGap":{:.0f},"seatedRideSignature":{},"collision":{},"furniture":"{:08X}","onMount":{}}},"lines":[{}]}})",
				g_boatArmed.load() ? "true" : "false", plugins, g_boatSeatedRide.load() ? "true" : "false", how != 0 ? "true" : "false", BoatHowName(how), form,
				a_stable == kLayoutBoat ? "true" : "false", a_on == kLayoutBoat ? "true" : "false",
				map && map->IsMovementControlsEnabled() ? "true" : "false", map && map->IsActivateControlsEnabled() ? "true" : "false",
				map && map->AreControlsEnabled(RE::UserEvents::USER_EVENT_FLAG::kJumping) ? "true" : "false",
				map && map->AreControlsEnabled(RE::UserEvents::USER_EVENT_FLAG::kSneaking) ? "true" : "false",
				map && map->IsMenuControlsEnabled() ? "true" : "false", map && map->IsLookingControlsEnabled() ? "true" : "false",
				gap, a_player && SeatedRideSignature(a_player) ? "true" : "false",
				a_player && a_player->HasCollision() ? "true" : "false", furniture ? furniture->GetFormID() : 0u, a_player && a_player->IsOnMount() ? "true" : "false", a_lines);
		}
	}

	std::string SwimStateJson()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		const auto* state = player ? player->AsActorState() : nullptr;
		std::scoped_lock l(g_lock);
		const int on = g_layoutOn.load();
		const int stable = g_stateStable.load();
		const auto linesJson = [](const std::vector<Bind>& a_lines) {
			std::string out;
			for (const auto& b : a_lines)
			{
				out += std::format(R"({}{{"event":"{}","device":"{}","key":"{}"}})", out.empty() ? "" : ",", EscapeJson(b.event), DeviceName(b.device), Hex(b.key));
			}
			return out;
		};
		return std::format(R"("swimming":{{"isSwimming":{},"settled":{},"waterLayoutOn":{},"journalOpen":{},"swaps":{},"framesWaitedForHeldKey":{},"lines":[{}]}},"horseback":{{"onMount":{},"settled":{},"horseLayoutOn":{},"petting":{},"lines":[{}]}})",
						   state && state->IsSwimming() ? "true" : "false", stable == kLayoutWater ? "true" : "false", on == kLayoutWater ? "true" : "false",
						   g_swimJournal.load() ? "true" : "false", g_swimFlips.load(), g_swimWaited.load(), linesJson(g_swim),
						   player && player->IsOnMount() ? "true" : "false", stable == kLayoutHorse ? "true" : "false", on == kLayoutHorse ? "true" : "false", functions::ManualPettingOn() ? "true" : "false", linesJson(g_horse)) +
		       BoatStateJsonLocked(player, stable, on, linesJson(g_boat));
	}

	void RedecideRequests()
	{
		auto* map = RE::ControlMap::GetSingleton();
		const auto functionKeys = GameplayFunctionKeys();
		std::scoped_lock l(g_lock);
		for (auto& r : g_requests) { DecideLocked(map, functionKeys, r); }
		RebuildRequestRowsLocked();
		RefreshHaveLinesLocked();
	}

	namespace
	{
		void DecidePendingRequests()
		{
			auto* map = RE::ControlMap::GetSingleton();
			const auto functionKeys = GameplayFunctionKeys();
			bool any = false;
			{
				std::scoped_lock l(g_lock);
				for (auto& r : g_requests)
				{
					if (!r.pending) { continue; }
					DecideLocked(map, functionKeys, r);
					any = true;
				}
				if (any)
				{
					RebuildRequestRowsLocked();
					RefreshHaveLinesLocked();
				}
			}
			if (any) { ApplyAll("mod request"); }
		}
	}

	void RequestsAtDataLoaded()
	{
		g_requestsReady.store(true);
		DecidePendingRequests();
	}

	RequestAnswer AskRequest(std::string_view a_plugin, std::string_view a_action, std::string_view a_label, std::string_view a_context, int a_device, std::uint32_t a_key, std::uint32_t a_alternative)
	{
		int context = 0, layout = kLayoutLand;
		const auto bad = [](std::string_view a_text) { return a_text.empty() || a_text.find_first_of("|\r\n") != std::string_view::npos; };
		std::string problem;
		if (bad(a_plugin)) { problem = "the plugin name is empty or holds a '|'"; }
		else if (bad(a_action)) { problem = "the action name is empty or holds a '|'"; }
		else if (a_label.find_first_of("|\r\n") != std::string_view::npos) { problem = "the label holds a '|'"; }
		else if (a_device < 0 || a_device > 2) { problem = "the device is not 0 keyboard, 1 mouse or 2 gamepad"; }
		else if (!ParseRequestContext(a_context, context, layout)) { problem = std::format("\"{}\" is not a context", a_context); }
		logger::info("request: {} asks for \"{}\" on {} in {}: key {}, alternative {}", a_plugin, a_action, DeviceName(a_device), a_context, KeyText(a_key, a_device), KeyText(a_alternative, a_device));
		if (!problem.empty())
		{
			logger::warn("request: {}'s \"{}\" answered badRequest: {}", a_plugin, a_action, problem);
			return { UVCRequestAPI::kBadRequest, kUnmapped, problem };
		}
		const bool now = g_requestsReady.load() && OnGameThread();
		{
			std::scoped_lock l(g_lock);
			auto it = std::find_if(g_requests.begin(), g_requests.end(), [&](const ModRequest& a_r) { return SameRequest(a_r, a_plugin, a_action, context, layout, a_device); });
			if (it == g_requests.end())
			{
				ModRequest r;
				r.plugin = std::string(a_plugin);
				r.action = std::string(a_action);
				r.context = context;
				r.layout = layout;
				r.device = a_device;
				g_requests.push_back(std::move(r));
				it = std::prev(g_requests.end());
			}
			it->label = a_label.empty() ? std::string(a_action) : std::string(a_label);
			it->asked = a_key;
			it->alternative = a_alternative;
			it->pending = true;
			it->status = UVCRequestAPI::kQueued;
			it->why = "waiting for the game's main thread and its data";
		}
		if (!now)
		{
			if (g_requestsReady.load())
			{
				if (auto* tasks = SKSE::GetTaskInterface()) { tasks->AddTask([]() { DecidePendingRequests(); }); }
			}
			logger::info("request: {}'s \"{}\" answered queued: decided at the next chance", a_plugin, a_action);
			return { UVCRequestAPI::kQueued, kUnmapped, "too early or off the main thread; it is decided at the next chance, and Query gives the answer" };
		}
		DecidePendingRequests();
		std::scoped_lock l(g_lock);
		const auto it = std::find_if(g_requests.begin(), g_requests.end(), [&](const ModRequest& a_r) { return SameRequest(a_r, a_plugin, a_action, context, layout, a_device); });
		if (it == g_requests.end()) { return { UVCRequestAPI::kBadRequest, kUnmapped, "the request is gone" }; }
		return { it->status, it->key, it->why };
	}

	RequestAnswer ReleaseRequest(std::string_view a_plugin, std::string_view a_action, std::string_view a_context, int a_device)
	{
		int context = 0, layout = kLayoutLand;
		if (!ParseRequestContext(a_context, context, layout)) { return { UVCRequestAPI::kBadRequest, kUnmapped, std::format("\"{}\" is not a context", a_context) }; }
		const bool now = g_requestsReady.load() && OnGameThread();
		{
			std::scoped_lock l(g_lock);
			const auto it = std::find_if(g_requests.begin(), g_requests.end(), [&](const ModRequest& a_r) { return SameRequest(a_r, a_plugin, a_action, context, layout, a_device); });
			if (it == g_requests.end())
			{
				logger::info("request: {} released \"{}\" on {} in {}, which it had not asked for", a_plugin, a_action, DeviceName(a_device), a_context);
				return { UVCRequestAPI::kNotRequested, kUnmapped, "no such request" };
			}
			// Off the main thread the map is not touched here; the mapping the request made stays with its key until the
			// task below. A request that never reached the map has nothing to take back.
			if (now) { ClearGrantLocked(RE::ControlMap::GetSingleton(), *it, it->key); }
			else if (it->key != kUnmapped && it->layout == kLayoutLand && g_requestsReady.load())
			{
				if (auto* tasks = SKSE::GetTaskInterface())
				{
					tasks->AddTask([context = it->context, device = it->device, action = it->action, key = it->key]() {
						auto* map = RE::ControlMap::GetSingleton();
						std::scoped_lock task(g_lock);
						ModRequest gone;
						gone.context = context;
						gone.device = device;
						gone.action = action;
						ClearGrantLocked(map, gone, key);
					});
				}
			}
			g_requests.erase(it);
			RebuildRequestRowsLocked();
			RefreshHaveLinesLocked();
		}
		logger::info("request: {} released \"{}\" on {} in {}", a_plugin, a_action, DeviceName(a_device), a_context);
		if (g_requestsReady.load())
		{
			if (now) { ApplyAll("mod request released"); }
			else if (auto* tasks = SKSE::GetTaskInterface()) { tasks->AddTask([]() { ApplyAll("mod request released"); }); }
		}
		return { UVCRequestAPI::kReleased, kUnmapped, "released; the player's choice for it, if any, stays in the INI" };
	}

	RequestAnswer QueryRequest(std::string_view a_plugin, std::string_view a_action, std::string_view a_context, int a_device)
	{
		int context = 0, layout = kLayoutLand;
		if (a_device < 0 || a_device > 2) { return { UVCRequestAPI::kBadRequest, kUnmapped, "the device is not 0 keyboard, 1 mouse or 2 gamepad" }; }
		if (!ParseRequestContext(a_context, context, layout)) { return { UVCRequestAPI::kBadRequest, kUnmapped, std::format("\"{}\" is not a context", a_context) }; }
		{
			std::scoped_lock l(g_lock);
			const auto it = std::find_if(g_requests.begin(), g_requests.end(), [&](const ModRequest& a_r) { return SameRequest(a_r, a_plugin, a_action, context, layout, a_device); });
			if (it != g_requests.end())
			{
				logger::debug("request: {} queried \"{}\": {}", a_plugin, a_action, UVCRequestAPI::StatusName(it->status));
				return { it->status, it->key, it->why };
			}
		}
		if (!OnGameThread()) { return { UVCRequestAPI::kNotRequested, kUnmapped, "not requested; a control of the game can only be read from the main thread" }; }
		// Not a request: what the control has now.
		if (layout != kLayoutLand)
		{
			return { UVCRequestAPI::kNotRequested, LayoutShownKey(layout, a_action, a_device), "not requested; the key that control has in that layout" };
		}
		auto* map = RE::ControlMap::GetSingleton();
		std::scoped_lock l(g_lock);
		std::uint32_t key = kUnmapped;
		ForEachLandMapping(map, context, a_device, [&](std::string_view a_event, std::uint16_t a_k, std::uint16_t) {
			if (key == kUnmapped && a_k != kUnmapped && IEquals(a_event, a_action)) { key = a_k; }
		});
		return { UVCRequestAPI::kNotRequested, key, "not requested; the key that control has now" };
	}

	std::vector<BindingRow> AllBindings(int a_device)
	{
		std::vector<BindingRow> out;
		auto* map = RE::ControlMap::GetSingleton();
		// Read-only, under g_lock, from whatever thread the caller is on: UVC Controller Layout asks from the journal's
		// AdvanceMovie, which is not always the engine's main thread, and a thread check here left its graphic empty.
		if (!map || a_device < 0 || a_device > 2) { return out; }
		{
			std::scoped_lock l(g_lock);
			for (int context = 0; context < ContextCount(); ++context)
			{
				const std::string block = ContextName(context);
				ForEachLandMapping(map, context, a_device, [&](std::string_view a_event, std::uint16_t a_k, std::uint16_t a_mod) {
					if (a_k == kUnmapped || a_event.empty()) { return; }
					std::string label;
					for (const auto& r : g_requests)
					{
						if (r.accepted && r.layout == kLayoutLand && r.context == context && r.device == a_device && IEquals(r.action, a_event))
						{
							label = std::format("{} ({})", r.label, r.plugin);
							break;
						}
					}
					out.push_back({ block, std::string(a_event), std::move(label), a_k, a_mod });
				});
			}
			for (const auto& m : g_modifiers)
			{
				if (m.device == a_device && m.key != kUnmapped) { out.push_back({ "Modifier", kModifierRowName, {}, m.key, 0 }); }
			}
		}
		// The Swimming and Horseback layouts whole: every Gameplay control as it is there, its own line or else what it keeps from land.
		// LayoutShownKey takes the lock itself.
		std::vector<BindingRow> land;
		for (const auto& r : out)
		{
			if (r.block == ContextName(kGameplayContext)) { land.push_back(r); }
		}
		for (const int layout : { kLayoutWater, kLayoutHorse, kLayoutBoat })
		{
			const char* block = LayoutSection(layout);
			std::vector<Bind> lines;
			std::uint32_t designatedKey = 0;
			{
				std::scoped_lock l(g_lock);
				lines = LinesLocked(layout);
				for (const auto& m : g_modifiers)
				{
					if (m.device == a_device) { designatedKey = m.key; }
				}
			}
			std::vector<std::string> listed;
			for (const auto& b : lines)
			{
				if (b.device != a_device) { continue; }
				listed.push_back(b.event);
				if (b.key != kUnmapped) { out.push_back({ block, b.event, {}, b.key, b.useDesignatedModifier ? designatedKey : b.modifier }); }
			}
			for (const auto& r : land)
			{
				if (std::any_of(listed.begin(), listed.end(), [&](const std::string& a_e) { return IEquals(a_e, r.event); })) { continue; }
				if (layout == kLayoutHorse && FreedWhileMounted(r.event)) { continue; }
				const std::uint16_t key = LayoutShownKey(layout, r.event, a_device);
				if (key != kUnmapped) { out.push_back({ block, r.event, r.label, key, r.modifier }); }
			}
		}
		for (const auto& f : functions::GetPresentFunctions())
		{
			const auto& bind = f.bind[static_cast<std::size_t>(a_device)];
			if (bind.key != kUnmapped)
			{
				out.push_back({ f.target.block.empty() ? "Mod" : f.target.block, f.name, f.name, bind.key, bind.modifier });
				if (!f.target.alsoBlock.empty()) { out.push_back({ f.target.alsoBlock, f.name, f.name, bind.key, bind.modifier }); }
			}
		}
		return out;
	}

	std::vector<RequestChoice> GetRequestChoices()
	{
		std::scoped_lock l(g_lock);
		return g_requestChoices;
	}

	void SetRequestChoices(std::vector<RequestChoice> a_list)
	{
		std::scoped_lock l(g_lock);
		g_requestChoices = std::move(a_list);
	}

	std::uint16_t RequestRowKey(std::string_view a_row, int a_device)
	{
		std::scoped_lock l(g_lock);
		for (const auto& r : g_requests)
		{
			if (r.device == a_device && r.accepted && RequestRow(r) == a_row) { return r.key; }
		}
		return kUnmapped;
	}

	bool SetRequestRowKey(std::string_view a_row, int a_device, std::uint16_t a_key, std::string& a_why)
	{
		auto* map = RE::ControlMap::GetSingleton();
		const auto functionKeys = GameplayFunctionKeys();
		std::scoped_lock l(g_lock);
		const auto it = std::find_if(g_requests.begin(), g_requests.end(), [&](const ModRequest& a_r) { return a_r.device == a_device && a_r.accepted && RequestRow(a_r) == a_row; });
		if (it == g_requests.end())
		{
			a_why = std::format("that mod asked for no key on the {}", DeviceName(a_device));
			return false;
		}
		const std::string holder = KeyHolderLocked(map, functionKeys, *it, a_key);
		if (!holder.empty())
		{
			a_why = std::format("{} holds it", holder);
			return false;
		}
		const std::string context = RequestContextName(*it);
		const auto choice = std::find_if(g_requestChoices.begin(), g_requestChoices.end(), [&](const RequestChoice& a_c) {
			return a_c.device == a_device && IEquals(a_c.plugin, it->plugin) && IEquals(a_c.action, it->action) && IEquals(a_c.context, context);
		});
		if (choice != g_requestChoices.end()) { choice->key = a_key; }
		else { g_requestChoices.push_back({ it->plugin, it->action, context, a_device, a_key }); }
		if (it->key != a_key) { ClearGrantLocked(map, *it, it->key); }
		it->key = a_key;
		it->status = UVCRequestAPI::kPlayerChoice;
		it->why = "the player set this row";
		RefreshHaveLinesLocked();
		logger::info("request: the player set {}'s \"{}\" on {} in {} to {}", it->plugin, it->action, DeviceName(a_device), context, KeyText(a_key, a_device));
		return true;
	}

	std::string RequestsJson()
	{
		std::scoped_lock l(g_lock);
		std::string out = std::format(R"({{"ok":true,"op":"requests","ready":{},"requests":[)", g_requestsReady.load() ? "true" : "false");
		bool first = true;
		for (const auto& r : g_requests)
		{
			out += std::format(R"({}{{"plugin":"{}","action":"{}","label":"{}","context":"{}","device":"{}","asked":"{}","alternative":"{}","key":"{}","status":"{}","accepted":{},"row":"{}","why":"{}"}})",
							   first ? "" : ",", EscapeJson(r.plugin), EscapeJson(r.action), EscapeJson(r.label), RequestContextName(r), DeviceName(r.device), KeyText(r.asked, r.device), KeyText(r.alternative, r.device),
							   KeyText(r.key, r.device), UVCRequestAPI::StatusName(r.status), r.accepted ? "true" : "false", EscapeJson(RequestRow(r)), EscapeJson(r.why));
			first = false;
		}
		out += R"(],"playerChoices":[)";
		first = true;
		for (const auto& c : g_requestChoices)
		{
			out += std::format(R"({}{{"plugin":"{}","action":"{}","context":"{}","device":"{}","key":"{}"}})", first ? "" : ",", EscapeJson(c.plugin), EscapeJson(c.action), EscapeJson(c.context), DeviceName(c.device), KeyText(c.key, c.device));
			first = false;
		}
		out += "]}";
		return out;
	}

	std::string ControlWhyJson(std::string_view a_event)
	{
		auto* map = RE::ControlMap::GetSingleton();
		if (!map) { return R"({"ok":false,"op":"why","error":"ControlMap singleton is null"})"; }
		std::scoped_lock l(g_lock);
		std::string out = std::format(R"({{"ok":true,"op":"why","event":"{}","sneakingControlsEnabled":{},"movementControlsEnabled":{},"fightingControlsEnabled":{},"layoutOn":{},"journalOpen":{},"devices":{{)",
									  EscapeJson(a_event), map->IsSneakingControlsEnabled() ? "true" : "false", map->IsMovementControlsEnabled() ? "true" : "false", map->IsFightingControlsEnabled() ? "true" : "false", g_layoutOn.load(), g_swimJournal.load() ? "true" : "false");
		for (int d = 0; d < 3; ++d)
		{
			out += std::format(R"({}"{}":{{"default":"{}","live":[)", d ? "," : "", DeviceName(d), KeyText(DefaultKey(a_event, d), d));
			auto* mappings = MappingsFor(map, kGameplayContext, d);
			bool first = true;
			if (mappings)
			{
				for (auto* m : Find(*mappings, a_event))
				{
					out += std::format(R"({}{{"key":"{}","modifier":"{}","groupFlags":"{:#x}"}})", first ? "" : ",", KeyText(m->inputKey, d), Hex(m->modifier), static_cast<std::uint32_t>(m->userEventGroupFlag.underlying()));
					first = false;
				}
			}
			out += "]}";
		}
		out += "}}";
		return out;
	}

	void ApplyAll(const char* a_reason)
	{
		auto* map = RE::ControlMap::GetSingleton();
		if (!map) { logger::warn("apply ({}): the ControlMap singleton is null; nothing applied", a_reason); return; }
		// No thread is noted here (1.3.0): kPostLoadGame runs this on the save loader's thread after a load from the main
		// menu, and noting that made UVC_ForEachBinding return nothing and requests answer "queued". See OnGameThread.
		std::scoped_lock l(g_lock);
		// The lists are written over the land layout; a water or horseback layout returns on the next HUD frame.
		RestoreLandLocked(map, a_reason);
		g_lastConverted = ConvertLinkedMappings(map, a_reason);  // before the lists, bEnabled=0 included
		RecordContextRows(map);  // the SWIMMING, HORSEBACK and IN MAP rows, from the keys the map has before the lists change them
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
		ApplyRequestsLocked(map, a_reason);
		// The game's own switch for the Sneak control (DisablePlayerControls and ToggleControls clear it), said when it changes:
		// with it off no Sneak key does anything, whatever this mod's lists say, and a reload does not turn it back on.
		{
			static bool s_sneakOff = false;
			const bool off = !map->IsSneakingControlsEnabled();
			if (off != s_sneakOff)
			{
				s_sneakOff = off;
				logger::info("apply ({}): the game's Sneak controls are {}", a_reason, off ? "DISABLED (something called DisablePlayerControls or ToggleControls; no Sneak key works meanwhile)" : "enabled again");
			}
		}
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

		// The control's live keys on a_device are exactly the keys controlmap.txt gives it there - the whole set, not the
		// first key: Favorites on D-pad up alone is NOT its default of D-pad up and down (1.1.3).
		bool OnDefaultKeys(std::string_view a_event, int a_device, const std::vector<std::uint16_t>& a_live)
		{
			if (a_device < 0 || a_device > 2) { return false; }
			const auto& defaults = GameplayDefaults();
			const auto it = defaults.find(Lower(a_event));
			if (it == defaults.end()) { return false; }
			std::vector<std::uint16_t> real;
			for (const auto k : a_live) { if (k != kUnmapped) { real.push_back(k); } }
			std::sort(real.begin(), real.end());
			real.erase(std::unique(real.begin(), real.end()), real.end());
			return real == it->second.all[a_device];
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
			if (OnDefaultKeys(a_event, a_device, a_live))
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
				if (IsRequestEventLocked(after.events[i], d)) { continue; }  // a mod's request: the request list holds it, not the INI's lists
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
				if (IsRequestEventLocked(live.events[i], d)) { continue; }               // a mod's request, not a remap
				if (FindEntry(0, live.events[i], d) != g_entries.end()) { continue; }  // the INI already decides it
				const bool bound = std::any_of(g_binds.begin(), g_binds.end(), [&](const Bind& b) {
					return b.device == d && IEquals(b.context, "Gameplay") && IEquals(b.event, live.events[i]);
				});
				if (bound) { continue; }
				const bool hasKey = std::any_of(keys.begin(), keys.end(), [](std::uint16_t a_k) { return a_k != kUnmapped; });
				const bool same = hasKey ? OnDefaultKeys(live.events[i], d, keys) : def->second.key[d] == kUnmapped;
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
		// The game leaves a control with no key off its Controls list, whatever took the key. A control it lets the player
		// remap on this family is put back too, not only the INI list's, so no control can be unbound out of reach (the owner,
		// 2026-10-20, after a player found no row to bind Favorites back). A keyless control the game does not let the player
		// remap on this family stays off the page, as the game has it.
		if (auto* map = RE::ControlMap::GetSingleton(); map)
		{
			if (auto* ctx = Context(map, 0); ctx)
			{
				for (int d = a_gamepad ? 2 : 0; d <= (a_gamepad ? 2 : 1); ++d)
				{
					for (const auto& m : ctx->deviceMappings[d])
					{
						const char* id = m.eventID.c_str();
						if (!id || !*id || !m.remappable) { continue; }
						if (std::none_of(events.begin(), events.end(), [&](const std::string& a_have) { return IEquals(a_have, id); })) { events.emplace_back(id); }
					}
				}
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
		RestoreLandLocked(map, a_reason);
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
