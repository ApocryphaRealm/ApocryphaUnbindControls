// Unbind Vanilla Controls, the controller graphic (was UVC Controller Layout until 1.4.0) - own code, GPL-3.0-or-later. Turns UVC's rows into the lines of each button's box.
#include "PCH.h"

#include "layout/LayoutRows.h"

#include "layout/LayoutStrings.h"
#include "Functions.h"
#include "GamepadPP.h"
#include "Unbinder.h"
#include "UVCRequestAPI.h"
#include "utils/Logger.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <optional>
#include <string_view>

namespace rows
{
	namespace
	{
		constexpr std::int32_t kGamepad = 2;

		std::optional<Button> ButtonOfKey(std::uint32_t a_key)
		{
			switch (a_key)
			{
			case 0x0001: return kDUp;
			case 0x0002: return kDDown;
			case 0x0004: return kDLeft;
			case 0x0008: return kDRight;
			case 0x0010: return kStart;
			case 0x0020: return kBack;
			case 0x0040:
			case 0x000b: return kLStick;
			case 0x0080:
			case 0x000c: return kRStick;
			case 0x0100: return kLB;
			case 0x0200: return kRB;
			case 0x1000: return kA;
			case 0x2000: return kB;
			case 0x4000: return kX;
			case 0x8000: return kY;
			case 0x0009: return kLT;
			case 0x000a: return kRT;
			default: return std::nullopt;
			}
		}

		// The game's translator, the same one the journal's rows go through: "$Sneak" gives the row's own word, in the game's language. A
		// name with no token is used as it stands.
		std::string Translated(const std::string& a_name)
		{
			std::string result;
			if (SKSE::Translation::Translate("$" + a_name, result) && !result.empty() && result.front() != '$') { return result; }
			logger::trace("rows: no token for \"{}\"", a_name);
			return a_name;
		}

		// The game's word for a token, if it has one. A UI mod's translation file counts.
		std::optional<std::string> GameWord(const std::string& a_token)
		{
			std::string result;
			if (SKSE::Translation::Translate("$" + a_token, result) && !result.empty() && result.front() != '$') { return result; }
			return std::nullopt;
		}

		// "LocalMapMoveMode" to "Local Map Move Mode": for an event nobody has a word for, a mod's among them.
		std::string SpacedOut(const std::string& a_event)
		{
			std::string out;
			for (std::size_t i = 0; i < a_event.size(); ++i)
			{
				const char c = a_event[i];
				if (i > 0 && std::isupper(static_cast<unsigned char>(c)) && (std::islower(static_cast<unsigned char>(a_event[i - 1])) || (i + 1 < a_event.size() && std::islower(static_cast<unsigned char>(a_event[i + 1])))) && a_event[i - 1] != ' ')
				{
					out += ' ';
				}
				out += c;
			}
			return out;
		}

		// The control map's events the game's translation files have no word for (Interface\translate_*.txt, read 2026-11-05: 838 tokens against
		// Interface\Controls\PC\controlmap.txt), those on the contexts the graphic shows. The debug and Creations contexts are left out.
		struct EventWord
		{
			const char* event;
			const char* key;
			const char* english;
		};
		constexpr std::array kEventWords = {
			EventWord{ "Up", "UVCL_PadUp", "Up" },
			EventWord{ "Down", "UVCL_PadDown", "Down" },
			EventWord{ "Left", "UVCL_PadLeft", "Left" },
			EventWord{ "Right", "UVCL_PadRight", "Right" },
			EventWord{ "Click", "UVCL_EvClick", "Click" },
			EventWord{ "Cursor", "UVCL_EvCursor", "Cursor" },
			EventWord{ "Left Stick", "UVCL_EvLeftStick", "Left Stick" },
			EventWord{ "Zoom In", "UVCL_EvZoomIn", "Zoom In" },
			EventWord{ "Zoom Out", "UVCL_EvZoomOut", "Zoom Out" },
			EventWord{ "Item Zoom", "UVCL_EvItemZoom", "Zoom Item" },
			EventWord{ "Rotate", "UVCL_EvRotate", "Rotate" },
			EventWord{ "LeftEquip", "UVCL_EvLeftEquip", "Equip Left" },
			EventWord{ "RightEquip", "UVCL_EvRightEquip", "Equip Right" },
			EventWord{ "PreviousFocus", "UVCL_EvPreviousFocus", "Previous Focus" },
			EventWord{ "NextFocus", "UVCL_EvNextFocus", "Next Focus" },
			EventWord{ "TabSwitch", "UVCL_EvTabSwitch", "Switch Tab" },
			EventWord{ "PrevPage", "UVCL_EvPrevPage", "Previous Page" },
			EventWord{ "NextPage", "UVCL_EvNextPage", "Next Page" },
			EventWord{ "PageUp", "UVCL_EvPageUp", "Page Up" },
			EventWord{ "PageDown", "UVCL_EvPageDown", "Page Down" },
			EventWord{ "PlacePlayerMarker", "UVCL_EvPlaceMarker", "Place Marker" },
			EventWord{ "PlayerPosition", "UVCL_EvPlayerPosition", "Player Position" },
			EventWord{ "LocalMapMoveMode", "UVCL_EvLocalMapMove", "Move Local Map" },
			EventWord{ "MapLookMode", "UVCL_EvMapLook", "Map Look" },
			EventWord{ "RotatePick", "UVCL_EvRotatePick", "Turn Pick" },
			EventWord{ "RotateLock", "UVCL_EvRotateLock", "Turn Lock" },
			EventWord{ "DebugMode", "UVCL_EvDebugMode", "Debug Mode" },
			EventWord{ "CameraZUp", "UVCL_EvCameraUp", "Camera Up" },
			EventWord{ "CameraZDown", "UVCL_EvCameraDown", "Camera Down" },
			EventWord{ "WorldZUp", "UVCL_EvWorldUp", "World Up" },
			EventWord{ "WorldZDown", "UVCL_EvWorldDown", "World Down" },
			EventWord{ "LockToZPlane", "UVCL_EvLockHeight", "Lock Height" },
			EventWord{ "Screenshot", "UVCL_EvScreenshot", "Screenshot" },
			EventWord{ "Multi-Screenshot", "UVCL_EvMultiScreenshot", "Multi-Screenshot" },
			EventWord{ "CameraPath", "UVCL_EvCameraPath", "Camera Path" },
			EventWord{ "Console", "UVCL_EvConsole", "Console" },
		};

		// An event whose job has a word in the game's own files, under another name. X and Y are what each context does with them: on the
		// journal, as its QuestsPage and the Controls page use them; in an item menu, as the control map's prompts do.
		std::optional<std::string> GameWordFor(const std::string& a_event, const std::string& a_block)
		{
			const bool journal = a_block == "Journal";
			const bool items = a_block == "Item Menus" || a_block == "ITEMS";
			if (a_event == "XButton" && journal) { return GameWord("Show on Map"); }
			if (a_event == "YButton" && journal) { return GameWord("Defaults"); }
			if (a_event == "XButton" && items) { return GameWord("Drop"); }
			if (a_event == "YButton" && items) { return GameWord("Favorite"); }
			if (a_event == "LocalMap") { return GameWord("Local Map"); }
			if (a_event == "ChargeItem") { return GameWord("Charge"); }
			if (a_event == "DownloadAll") { return GameWord("Download All"); }
			return std::nullopt;
		}

		std::string EventText(const std::string& a_event, const std::string& a_block)
		{
			if (a_event == "Move") { return TR("UVCL_Move", "Move"); }
			if (a_event == "Look") { return TR("UVCL_Look", "Look"); }
			constexpr std::string_view hotkey = "Hotkey";
			if (a_event.starts_with(hotkey) && a_event.size() > hotkey.size())
			{
				return std::format("{} {}", TR("UVCL_Hotkey", "Hotkey"), a_event.substr(hotkey.size()));
			}
			if (auto word = GameWordFor(a_event, a_block)) { return *word; }
			if (auto word = GameWord(a_event)) { return *word; }
			for (const auto& word : kEventWords)
			{
				if (a_event == word.event) { return TR(word.key, word.english); }
			}
			logger::trace("rows: no word for \"{}\"", a_event);
			return SpacedOut(a_event);
		}

		// A mod's row reads by what it does: "Target Lock", not "Target Lock (True Directional Movement)" (the owner, 2026-11-05).
		std::string WithoutModName(std::string a_label)
		{
			if (a_label.ends_with(')'))
			{
				const auto open = a_label.rfind(" (");
				if (open != std::string::npos && open > 0) { a_label.resize(open); }
			}
			return a_label;
		}

		// A button named in a line ("L2 + Shout"): the name printed on the pad, or for a face button or the D-pad, words.
		std::string ModifierName(std::uint32_t a_key, layoutsettings::Faces a_faces)
		{
			const auto button = ButtonOfKey(a_key);
			if (!button) { return std::format("0x{:X}", a_key); }
			const std::string shortName = ShortName(*button, a_faces);
			if (!shortName.empty()) { return shortName; }
			const bool ps = a_faces == layoutsettings::Faces::kPlayStation;
			switch (*button)
			{
			case kA: return ps ? TR("UVCL_Cross", "Cross") : "A";
			case kB: return ps ? TR("UVCL_Circle", "Circle") : "B";
			case kX: return ps ? TR("UVCL_Square", "Square") : "X";
			case kY: return ps ? TR("UVCL_Triangle", "Triangle") : "Y";
			case kDUp: return TR("UVCL_DUp", "D-pad up");
			case kDDown: return TR("UVCL_DDown", "D-pad down");
			case kDLeft: return TR("UVCL_DLeft", "D-pad left");
			case kDRight: return TR("UVCL_DRight", "D-pad right");
			default: return {};
			}
		}

		// The contexts, in the order the graphic steps through them: the ones a player thinks about first, then the menus. Debug and
		// console contexts stay out: nobody binds those. A mod's row in a block of UVC's page is in that block's context here.
		struct ContextDef
		{
			const char* id;
			const char* key;
			const char* english;
		};
		constexpr std::array kContexts = {
			ContextDef{ "Gameplay", "UVCL_Gameplay", "Gameplay" },
			ContextDef{ "Swimming", "UVCL_Swimming", "Swimming" },
			ContextDef{ "Horseback", "UVCL_Horseback", "On Horseback" },
			ContextDef{ "Map", "UVCL_MapContext", "Map" },
			ContextDef{ "Menu Mode", "UVCL_Menus", "Menus" },
			ContextDef{ "Item Menus", "UVCL_ItemMenus", "Item Menus" },
			ContextDef{ "Inventory", "UVCL_Inventory", "Inventory" },
			ContextDef{ "Favorites", "UVCL_Favorites", "Favorites" },
			ContextDef{ "Stats", "UVCL_Stats", "Skills" },
			ContextDef{ "Cursor", "UVCL_Cursor", "Cursor" },
			ContextDef{ "Book", "UVCL_Book", "Books" },
			ContextDef{ "Journal", "UVCL_Journal", "Journal" },
			ContextDef{ "Lockpicking", "UVCL_Lockpicking", "Lockpicking" },
			ContextDef{ "TFC", "UVCL_FreeCamera", "Free Camera" },
			ContextDef{ "Favor", "UVCL_Favor", "Favors" },
		};

		int ContextOf(const std::string& a_block)
		{
			if (a_block == "Modifier" || a_block == "Mod") { return 0; }
			if (a_block == "HORSEBACK") { return 2; }
			if (a_block == "IN MAP") { return 3; }
			if (a_block == "ITEMS") { return 5; }  // Back Pocket's row, in UVC's ITEMS block
			for (std::size_t i = 0; i < kContexts.size(); ++i)
			{
				if (a_block == kContexts[i].id) { return static_cast<int>(i); }
			}
			return -1;
		}

		struct Reader
		{
			layoutsettings::Faces faces;
			std::vector<Table>* tables;
			int seen = 0;
		};

		void AddLine(Table& a_table, Button a_button, std::string a_text, bool a_mod)
		{
			auto& lines = a_table[static_cast<std::size_t>(a_button)];
			if (std::none_of(lines.begin(), lines.end(), [&](const Line& a_l) { return a_l.text == a_text; }))
			{
				lines.push_back({ std::move(a_text), a_mod });
			}
		}

		void OnRow(const UVCRequestAPI::BindingRow* a_row, void* a_user)
		{
			auto* reader = static_cast<Reader*>(a_user);
			if (!a_row || !reader || !a_row->block || !a_row->event) { return; }
			++reader->seen;
			const std::string block = a_row->block;
			const std::string event = a_row->event;
			const std::string label = a_row->label ? a_row->label : "";
			const auto button = ButtonOfKey(a_row->key);
			if (!button)
			{
				logger::debug("rows: {}|{}: key 0x{:X} is no button on the graphic", block, event, a_row->key);
				return;
			}
			const int context = ContextOf(block);
			if (context < 0)
			{
				logger::trace("rows: {}|{}: this context is not on the graphic", block, event);
				return;
			}
			const bool mod = block == "Mod" || block == "HORSEBACK" || block == "IN MAP" || block == "ITEMS" || !label.empty();

			// A stick is one box, for pushing it and for clicking it. A click is marked: "L3: Sneak".
			std::string prefix;
			if (a_row->key == 0x0040) { prefix += ShortName(kLStick, reader->faces) + ": "; }
			if (a_row->key == 0x0080) { prefix += ShortName(kRStick, reader->faces) + ": "; }
			if (a_row->modifier != 0 && a_row->modifier != UVCRequestAPI::kNoKey)
			{
				prefix += ModifierName(a_row->modifier, reader->faces) + " + ";
			}
			std::string text = prefix + (label.empty() ? EventText(event, block) : WithoutModName(Translated(label)));
			AddLine((*reader->tables)[static_cast<std::size_t>(context)], *button, text, mod);
			// The designated modifier holds wherever the Gameplay buttons do.
			if (block == "Modifier")
			{
				AddLine((*reader->tables)[1], *button, text, mod);
				AddLine((*reader->tables)[2], *button, text, mod);
			}
		}
	}

	bool ApiThere()
	{
		// Asked every journal frame; every plugin is loaded by kDataLoaded, the first time this runs.
		static const bool there = UVCRequestAPI::GetAPI().forEachBinding != nullptr;
		return there;
	}

	std::vector<Context> Read(layoutsettings::Faces a_faces, int& a_rowsSeen)
	{
		std::vector<Table> tables(kContexts.size());
		Reader reader{ a_faces, &tables };
		const auto api = UVCRequestAPI::GetAPI();
		if (api.forEachBinding) { api.forEachBinding(kGamepad, &OnRow, &reader); }
		// Gamepad++'s buttons (1.5.0): it acts on them in the world, so they are listed where the Gameplay buttons are, as a mod's line.
		for (const auto& slot : gpp::Slots())
		{
			if (!slot.registered) { continue; }
			const auto button = ButtonOfKey(slot.pad);
			if (!button) { continue; }
			const std::string text = Translated(slot.row);
			for (const std::size_t context : { std::size_t{ 0 }, std::size_t{ 1 }, std::size_t{ 2 } }) { AddLine(tables[context], *button, text, true); }
			logger::debug("rows: Gamepad++ {} on {} listed as \"{}\"", slot.id, ButtonId(*button), text);
		}
		// Sprint or Sneak for Controller (1.6.0): the button the on-foot Sprint control has also crouches, so it gets that mod's line
		// in Gameplay, wherever the player has put Sprint.
		if (functions::SprintOrSneakPresent())
		{
			const std::uint16_t key = unbinder::LayoutShownKey(unbinder::kLayoutLand, "Sprint", kGamepad);
			if (const auto button = ButtonOfKey(key))
			{
				const std::string text = WithoutModName(Translated(std::string(functions::kSprintOrSneakRow)));
				AddLine(tables[0], *button, text, true);
				logger::debug("rows: Sprint or Sneak's line on {} as \"{}\"", ButtonId(*button), text);
			}
			else { logger::debug("rows: Sprint or Sneak is installed, but Sprint has no gamepad button (0x{:X})", key); }
		}
		std::vector<Context> out;
		for (std::size_t i = 0; i < kContexts.size(); ++i)
		{
			auto& table = tables[i];
			const bool any = std::any_of(table.begin(), table.end(), [](const std::vector<Line>& a_l) { return !a_l.empty(); });
			if (!any) { continue; }
			for (auto& lines : table)
			{
				std::stable_sort(lines.begin(), lines.end(), [](const Line& a_l, const Line& a_r) { return a_l.mod < a_r.mod; });
			}
			out.push_back({ kContexts[i].id, TR(kContexts[i].key, kContexts[i].english), std::move(table) });
		}
		a_rowsSeen = reader.seen;
		logger::debug("rows: {} rows read from UVC, {} contexts with something bound", reader.seen, out.size());
		return out;
	}

	std::string ShortName(Button a_button, layoutsettings::Faces a_faces)
	{
		const bool ps = a_faces == layoutsettings::Faces::kPlayStation;
		const bool steam = a_faces == layoutsettings::Faces::kSteam;
		const bool numbered = ps || steam;  // the Steam Controller's shoulders read L1 and L2, as printed on it
		switch (a_button)
		{
		case kLT: return numbered ? "L2" : "LT";
		case kLB: return numbered ? "L1" : "LB";
		case kRT: return numbered ? "R2" : "RT";
		case kRB: return numbered ? "R1" : "RB";
		case kBack: return ps ? "Share" : (steam ? "View" : "Back");
		case kStart: return ps ? "Options" : (steam ? "Menu" : "Start");
		case kLStick: return numbered ? "L3" : "LS";
		case kRStick: return numbered ? "R3" : "RS";
		default: return {};
		}
	}

	const char* ButtonId(Button a_button)
	{
		constexpr std::array<const char*, kButtonCount> names = { "LT", "LB", "Back", "DUp", "DLeft", "DDown", "DRight", "LStick", "RT", "RB", "Start", "Y", "B", "A", "X", "RStick" };
		return a_button >= 0 && a_button < kButtonCount ? names[static_cast<std::size_t>(a_button)] : "?";
	}
}
