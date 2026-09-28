#include "PCH.h"

#include "ControlsList.h"

#include "Functions.h"
#include "Settings.h"
#include "SystemMenu.h"
#include "Unbinder.h"
#include "utils/Logger.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <atomic>
#include <format>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace controlslist
{
	namespace
	{
		constexpr std::string_view kJournal = "Journal Menu";

		// Where the System page lives: vanilla's instance names first (both journals installed here use them), then
		// the shapes a replacer that wraps the hierarchy is known to use (the same list AMF's System row probes).
		constexpr const char* kPageCandidates[] = {
			"_root.QuestJournalFader.Menu_mc.SystemFader.Page_mc",
			"_root.Menu_mc.SystemFader.Page_mc",
			"_level0.QuestJournalFader.Menu_mc.SystemFader.Page_mc",
		};
		// The list itself: SystemPage keeps it as MappingList (= InputMappingPanel.List_mc, set in its constructor).
		constexpr const char* kListMembers[] = { ".MappingList", ".InputMappingPanel.List_mc" };

		constexpr std::uint32_t kUnmappedID = 0xFF;  // the engine's own "no key"
		constexpr int kMaxRowClips = 64;             // BSScrollingList builds Entry0..EntryN from the clips on stage

		std::mutex g_lock;       // the strings below; the DevBench thread reads them
		std::string g_listPath;  // this open's list, empty until found
		std::string g_pagePath;  // the System page that owns it
		std::string g_lastResult = "the journal has not been opened yet";
		std::string g_lastRemap = "no remap watched yet";

		std::atomic<bool> g_hooked{ false };
		std::atomic<bool> g_sinkInstalled{ false };
		std::atomic<bool> g_journalOpen{ false };
		// A controller press that carries "Pause" (System Tab) opens the journal on the engine's SAVED tab, while keyboard Esc
		// always gets System; the owner wants the controller to behave like Esc ("i want it to always open system"). The sink
		// notes the press, OnJournalOpen arms a switch when that press opened the journal, and the first frames switch tab.
		std::atomic<long long> g_pauseDownMs{ 0 };      // steady-clock ms of the latest controller "Pause" press, 0 = none
		std::atomic<bool> g_systemTabPending{ false };  // this open came from that press
		int g_systemTabFrames = 0;                      // main thread: frames left to confirm the switch
		std::atomic<bool> g_showsGamepad{ false };  // what the list showed in the latest frame
		std::atomic<int> g_blankFrames{ 0 };        // frames that hid at least one key, this open
		std::atomic<int> g_rowsBlankNow{ 0 };       // rows drawn blank in the latest frame
		std::atomic<int> g_rowsAdded{ 0 };          // rows put back into the list, this open
		bool g_searchFailedLogged = false;          // main thread
		std::set<std::string> g_loggedRows;         // main thread; rows already logged this open

		// ---- the remap watch ----------------------------------------------------------------------------------
		struct PressedKey
		{
			int device = -1;         // 0 keyboard, 1 mouse, 2 gamepad
			std::uint32_t code = 0;  // DirectInput scan code, mouse button, or XInput mask - the ControlMap's own values
		};
		std::atomic<bool> g_remapArmed{ false };  // the sink records only while a remap is on
		std::atomic<bool> g_remapActive{ false };
		// Diagnostic (DevBench op=listen): until this steady-clock time in ms, every button event is logged with the user
		// event the game attached to it - the way to see what a controller button actually sends.
		std::atomic<long long> g_listenUntilMs{ 0 };

		long long SteadyNowMs()
		{
			return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
		}
		std::mutex g_pressLock;
		PressedKey g_pressed;  // g_pressLock
		bool g_hasPress = false;  // g_pressLock
		// A combination (the owner, 2026-09-28: "confirm that you can set the shout/power function to left trigger plus left
		// bumper"): when the first button of a capture is the device's designated Modifier, the button pressed while it is
		// still held is the key, and the bind is "key + Modifier". g_pressLock.
		PressedKey g_pressed2;
		bool g_hasPress2 = false;
		bool g_firstReleased = false;
		std::string g_remapEvent;                       // main thread
		constexpr int kSheetCols = 4;    // columns on screen at once (the sheet slides to keep the selection on it)
		constexpr int kSheetSlots = 15;  // a title and fourteen rows per column
		struct SheetColumn
		{
			std::string title;        // "" for a column that continues the block to its left
			bool titleSpans = false;  // GAMEPLAY: the title is centred over this column and the next
			std::vector<std::string> rows;  // row keys (RowKey) into the list's entries
		};
		struct SheetState
		{
			int col = 0;
			int row = 0;
			int firstCol = 0;
			bool capture = false;       // a row is waiting for its key
			int captureContext = -1;    // the context a game control or menu action is bound in (-1: an extra row)
			std::string captureEvent;   // its event id
			long long releaseAtMs = 0;  // when the page's remap guard comes off after that capture
			std::vector<SheetColumn> columns;
			std::vector<std::string> drawn;  // what each cell last showed, so SetEntry runs only on a change
		};
		SheetState g_sheet;  // main thread (the journal's frame and its input both run there)
		std::array<std::vector<std::uint16_t>, 3> g_before;  // main thread: the control's keys when the remap began
		unbinder::GameplaySnapshot g_beforeAll;             // main thread: every Gameplay control's keys when the remap began

		void SetResult(std::string a_text)
		{
			std::scoped_lock l(g_lock);
			g_lastResult = std::move(a_text);
		}

		void SetRemapResult(std::string a_text)
		{
			std::scoped_lock l(g_lock);
			g_lastRemap = std::move(a_text);
		}

		std::string ListPathCopy()
		{
			std::scoped_lock l(g_lock);
			return g_listPath;
		}

		std::string PagePathCopy()
		{
			std::scoped_lock l(g_lock);
			return g_pagePath;
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

		std::string KeysText(const std::vector<std::uint16_t>& a_keys)
		{
			if (a_keys.empty()) { return "-"; }
			std::string out;
			for (const auto k : a_keys)
			{
				if (!out.empty()) { out += ","; }
				out += std::format("0x{:02x}", k);
			}
			return out;
		}

		RE::GFxMovieView* JournalMovie()
		{
			auto* ui = RE::UI::GetSingleton();
			if (!ui) { return nullptr; }
			auto menu = ui->GetMenu(kJournal);
			return (menu && menu->uiMovie) ? menu->uiMovie.get() : nullptr;
		}

		bool IEqualsView(std::string_view a_lhs, std::string_view a_rhs)
		{
			return a_lhs.size() == a_rhs.size() &&
			       std::equal(a_lhs.begin(), a_lhs.end(), a_rhs.begin(),
			                  [](unsigned char a, unsigned char b) { return std::tolower(a) == std::tolower(b); });
		}

		std::string StringMember(const RE::GFxValue& a_obj, const char* a_name)
		{
			RE::GFxValue v;
			return (a_obj.GetMember(a_name, &v) && v.IsString() && v.GetString()) ? std::string(v.GetString()) : std::string();
		}

		bool NumberMember(const RE::GFxValue& a_obj, const char* a_name, double& a_out)
		{
			RE::GFxValue v;
			if (!a_obj.GetMember(a_name, &v) || !v.IsNumber()) { return false; }
			a_out = v.GetNumber();
			return true;
		}

		bool IsGamepadButtonName(std::string_view a_name)
		{
			return a_name.starts_with("360_") || a_name.starts_with("PS3_") || a_name.starts_with("PS4_") || a_name.starts_with("PS5_");
		}

		// The list shows one device family at a time; the button names the game sent say which. (The input manager's
		// gamepad flag does not: an earlier 1.0.2 build trusted it and blanked Start and Back while the list showed the
		// controller.)
		bool ListShowsGamepad(const RE::GFxValue& a_entries)
		{
			for (std::uint32_t i = 0; i < a_entries.GetArraySize(); ++i)
			{
				RE::GFxValue entry;
				if (!a_entries.GetElement(i, &entry) || !entry.IsObject()) { continue; }
				// An extra row ([Functions]) is skipped outright: its key text is composed by this mod from its own
				// binding, never sent by the game, so it can say nothing about which device family the page is
				// showing - the same trap the modifier prefix sprang in 1.0.8, one step further out.
				RE::GFxValue isFunction;
				if (entry.GetMember("_uvcFunction", &isFunction) && isFunction.IsBool() && isFunction.GetBool()) { continue; }
				// Prefer the game's ORIGINAL string when this mod has prefixed a modifier onto the row.
				// Without this the detector reads "LT + 360_Y", which starts with neither "360_" nor a PS
				// prefix, and the whole list is taken for the keyboard - blanking the wrong family from the
				// second frame onward, since the prefix persists on the entry.
				std::string name = StringMember(entry, "_uvcBaseName");
				if (name.empty()) { name = StringMember(entry, "buttonName"); }
				if (name.empty()) { continue; }
				return IsGamepadButtonName(name);
			}
			return false;
		}

		// What an added row must put in buttonName so the journal draws its key tile.
		//
		// On the gamepad this is the game's OWN name for the button, read from its Interface\Controls\PC\gamepad.txt
		// (unbinder::GamepadButtonName) rather than composed here: the d-pad entries carry no prefix ("Up", "Left")
		// while the rest do ("PS3_LB"), and the prefix is the stock file's regardless of the pad, because the art set
		// is chosen further down. A row carrying a name this mod invented - "LT" - matches no art and draws no tile
		// at all, which is what happened (the owner, 2026-09-16).
		//
		// On the keyboard and mouse the column shows the name the GAME gives the key (BSInputDeviceManager's own lookup,
		// the one its real rows use), because the key tile is drawn from that name: "Esc" has a tile, this mod's INI name
		// "Escape" does not and came out as small text on the 1.1.1 menu rows (Menus: Cancel). This mod's own name is the
		// fallback when the game has none.
		std::string RowButtonName(std::uint16_t a_key, int a_device)
		{
			if (a_key == kUnmappedID) { return {}; }
			if (a_device == 2) { return unbinder::GamepadButtonName(a_key); }
			if (auto* devices = RE::BSInputDeviceManager::GetSingleton())
			{
				RE::BSFixedString gameName;
				if (devices->GetButtonNameFromID(static_cast<RE::INPUT_DEVICE>(a_device), static_cast<std::int32_t>(a_key), gameName) && gameName.c_str() && gameName.c_str()[0])
				{
					return gameName.c_str();
				}
			}
			const char* name = unbinder::ButtonName(a_key, a_device);
			return (name && name[0]) ? std::string(name) : std::format("0x{:02x}", a_key);
		}

		// Copies a REAL row's members onto a row this mod is adding, so the two are indistinguishable to the list.
		//
		// Why this and not a bare object with text/buttonName/buttonID: the Controls list is a
		// Shared.CenteredScrollingList with a Shared.ListFilterer, and a filterer decides whether to DRAW a row from
		// bookkeeping the art keeps on the entry - filterFlag in the shared code, and whatever else a replacer adds.
		// A row missing that member is not drawn, which looks exactly like the row was never added. Norden UI's
		// quest_journal.swf is the case that raised it (it ships its own journal at a higher priority than Quest
		// Journal Overhaul, and carries Shared.ListFilterer, EntryMatchesFilter and filterFlag), but the fix is not
		// about Norden UI: cloning means this mod never has to know which members THIS journal's list cares about.
		//
		// The row's identity - its text, its key, this mod's own marks - is set by the caller afterwards, and
		// sortIndex is left out because both callers work it out for the position they are inserting at.
		void CloneRowMembers(const RE::GFxValue& a_template, RE::GFxValue& a_row, const char* a_forRow)
		{
			if (!a_template.IsObject() || !a_row.IsObject()) { return; }
			static const std::set<std::string> kOwn = { "text", "buttonName", "buttonID", "sortIndex", "_uvcAdded", "_uvcFunction", "_uvcBaseName", "_uvcRowKey", "_uvcTitle",
			                                             "_uvcBlank" };
			std::string copied;
			a_template.VisitMembers([&](const char* a_name, const RE::GFxValue& a_value) {
				if (!a_name || kOwn.count(a_name)) { return; }
				a_row.SetMember(a_name, a_value);
				copied += (copied.empty() ? "" : ", ") + std::string(a_name);
			});
			if (g_loggedRows.insert(std::format("clone|{}", a_forRow)).second)
			{
				logger::debug("controls list: \"{}\" took its bookkeeping from a real row ({})", a_forRow,
							  copied.empty() ? "nothing to copy - this journal's rows carry no extra members" : copied);
			}
		}

		struct RowFacts
		{
			std::string text;        // the user event, e.g. "Quick Inventory"
			std::string buttonName;  // what the game sent for the key's art
			double buttonID = -1.0;  // the key code the game sent, -1 when absent
			double sortIndex = -1.0;
			bool added = false;      // put back by this mod
			bool blank = false;
			bool function = false;             // an extra row from [Functions], not a user event
			std::size_t functionIndex = 0;
			const char* why = "has a key on this device";
		};

		RowFacts ReadRow(const RE::GFxValue& a_entry, bool a_gamepad)
		{
			RowFacts f;
			f.text = StringMember(a_entry, "text");
			f.buttonName = StringMember(a_entry, "buttonName");
			NumberMember(a_entry, "buttonID", f.buttonID);
			NumberMember(a_entry, "sortIndex", f.sortIndex);
			RE::GFxValue added;
			f.added = a_entry.GetMember("_uvcAdded", &added) && added.IsBool() && added.GetBool();
			// A menu row or a block title (1.1.1): its label is an action name that can also be a Gameplay control's
			// ("Toggle Always Run", "Run"), so it is never looked up as one. It is blank only when it has no key.
			RE::GFxValue rowKey;
			if (a_entry.GetMember("_uvcRowKey", &rowKey) && rowKey.IsString() && rowKey.GetString() && rowKey.GetString()[0])
			{
				f.function = true;
				if (f.buttonName.empty())
				{
					f.blank = true;
					f.why = std::string_view(rowKey.GetString()).rfind("title:", 0) == 0 ? "a block title" : "a menu row with no key on this device";
				}
				return f;
			}
			// An extra row ([Functions]) is not a user event, so the live map knows nothing about it: its key comes
			// from the row's own binding, and the row is blank until the player gives it one.
			if (IEqualsView(f.text, unbinder::kModifierRowName))
			{
				f.function = true;  // drawn like an extra row: this mod owns its value, the live map knows nothing of it
				if (unbinder::ModifierFor(a_gamepad ? 2 : 0) == 0xFF)
				{
					f.blank = true;
					f.why = a_gamepad ? "the designated modifier row has no gamepad button" : "the designated modifier row has no key";
				}
				return f;
			}
			std::size_t index = 0;
			if (!f.text.empty() && functions::IsFunctionRow(f.text, &index))
			{
				f.function = true;
				f.functionIndex = index;
				if (functions::ShownBinding(index, a_gamepad).key == functions::kUnbound)
				{
					f.blank = true;
					f.why = a_gamepad ? "an extra row with no gamepad button" : "an extra row with no keyboard or mouse key";
				}
				return f;
			}
			if (!f.text.empty() && unbinder::KeylessOnFamily(f.text, a_gamepad))
			{
				f.blank = true;
				f.why = a_gamepad ? "no gamepad button on the live map" : "no keyboard or mouse key on the live map";
			}
			return f;
		}

		bool IsVisible(const RE::GFxValue& a_obj)
		{
			RE::GFxValue::DisplayInfo info;
			return a_obj.GetDisplayInfo(&info) && info.GetVisible();
		}

		void SetVisible(RE::GFxValue& a_obj, bool a_visible)
		{
			RE::GFxValue::DisplayInfo info;
			if (!a_obj.GetDisplayInfo(&info) || info.GetVisible() == a_visible) { return; }
			info.SetVisible(a_visible);
			a_obj.SetDisplayInfo(info);
		}

		// Returns how many art members the row has. Hiding also hides the vanilla `buttonPlacing` box; showing again
		// never touches it, because the vanilla row function sets that one itself on every draw.
		int SetArtVisible(RE::GFxValue& a_clip, bool a_visible)
		{
			int found = 0;
			for (const char* name : { "ButtonArt", "buttonArt" })
			{
				RE::GFxValue art;
				if (!a_clip.GetMember(name, &art) || !art.IsDisplayObject()) { continue; }
				SetVisible(art, a_visible);
				++found;
			}
			if (!a_visible)
			{
				RE::GFxValue placing;
				if (a_clip.GetMember("buttonPlacing", &placing) && placing.IsDisplayObject()) { SetVisible(placing, false); }
			}
			return found;
		}

		// Finds this open's Controls list. Cheap enough to retry each frame until the movie has built it.
		bool FindList(RE::GFxMovieView* a_movie, RE::GFxValue& a_list)
		{
			{
				std::scoped_lock l(g_lock);
				if (!g_listPath.empty()) { return a_movie->GetVariable(&a_list, g_listPath.c_str()) && a_list.IsDisplayObject(); }
			}
			for (const char* page : kPageCandidates)
			{
				for (const char* member : kListMembers)
				{
					const std::string candidate = std::string(page) + member;
					if (a_movie->GetVariable(&a_list, candidate.c_str()) && a_list.IsDisplayObject())
					{
						{
							std::scoped_lock l(g_lock);
							g_listPath = candidate;
							g_pagePath = page;
						}
						SetResult(std::format("Controls list found at \"{}\"", candidate));
						logger::info("controls list: found at \"{}\"; unbound controls are listed with no key", candidate);
						return true;
					}
				}
			}
			return false;
		}

		// THE LAYOUT (the owner, 2026-09-28: "align all the control page rows to the left so that their text is to the left and
		// that they all have the same font size and that the controls are arranged in blocks that are distinct, like for menus
		// block and then other blocks"). The list is split into blocks, each opened by a title row - GAMEPLAY over the game's
		// own controls, EXTRA CONTROLS over this mod's extra rows, then one per menu (MENUS, ITEMS, INVENTORY ...) - and every
		// row's name is drawn left-aligned at the list's own size in a column wide enough that nothing is shrunk to fit.
		//
		// A title row is an entry like any other (a row the list draws) with no key; pressing it sets nothing. Inside a menu
		// block a row is labelled by its action alone ("Cancel"), so names repeat between blocks and a row this mod adds is
		// identified by _uvcRowKey instead of its text.
		constexpr const char* kGameplayTitle = "GAMEPLAY";
		constexpr const char* kExtraTitle = "EXTRA CONTROLS";
		constexpr double kNameWidth = 212.0;  // the name column: the field starts at 0 and the key tile at 228

		std::string RowKey(const RE::GFxValue& a_entry)
		{
			const std::string key = StringMember(a_entry, "_uvcRowKey");
			return key.empty() ? StringMember(a_entry, "text") : key;
		}

		std::string TitleKey(std::string_view a_title) { return "title:" + std::string(a_title); }

		RE::GFxValue MakeTitleRow(RE::GFxMovieView* a_movie, const RE::GFxValue* a_template, std::string_view a_title)
		{
			RE::GFxValue value;
			a_movie->CreateObject(&value);
			const std::string text(a_title);
			if (a_template && a_template->IsObject()) { CloneRowMembers(*a_template, value, text.c_str()); }
			value.SetMember("text", RE::GFxValue(text.c_str()));
			value.SetMember("buttonName", RE::GFxValue(""));
			value.SetMember("buttonID", RE::GFxValue(static_cast<double>(kUnmappedID)));
			value.SetMember("_uvcAdded", RE::GFxValue(true));
			value.SetMember("_uvcFunction", RE::GFxValue(true));  // never the row that decides the device family
			value.SetMember("_uvcTitle", RE::GFxValue(true));
			value.SetMember("_uvcRowKey", RE::GFxValue(TitleKey(a_title).c_str()));
			value.SetMember("_uvcBaseName", RE::GFxValue(""));
			return value;
		}

		// Appends a title row unless one with that key is already in the list.
		bool AppendTitle(RE::GFxMovieView* a_movie, RE::GFxValue& a_entries, std::vector<std::string>& a_present, std::string_view a_title)
		{
			const std::string key = TitleKey(a_title);
			if (std::find(a_present.begin(), a_present.end(), key) != a_present.end()) { return false; }
			RE::GFxValue last;
			const bool haveLast = a_entries.GetArraySize() > 0 && a_entries.GetElement(a_entries.GetArraySize() - 1, &last) && last.IsObject();
			RE::GFxValue value = MakeTitleRow(a_movie, haveLast ? &last : nullptr, a_title);
			double sortIndex = 0.0;
			if (haveLast && NumberMember(last, "sortIndex", sortIndex)) { value.SetMember("sortIndex", RE::GFxValue(sortIndex + 1.0)); }
			const auto size = a_entries.GetArraySize();
			a_entries.SetArraySize(size + 1);
			a_entries.SetElement(size, value);
			a_present.push_back(key);
			return true;
		}

		// GAMEPLAY goes above the game's own rows: the array is rebuilt with the title first, the way AddMissingRows puts
		// rows back, and its sortIndex is one below the first row's.
		bool AddGameplayTitle(RE::GFxMovieView* a_movie, RE::GFxValue& a_entries)
		{
			const std::uint32_t count = a_entries.GetArraySize();
			if (count == 0) { return false; }
			RE::GFxValue first;
			if (!a_entries.GetElement(0, &first) || !first.IsObject()) { return false; }
			if (RowKey(first) == TitleKey(kGameplayTitle)) { return false; }
			std::vector<RE::GFxValue> rows;
			rows.reserve(count + 1);
			RE::GFxValue title = MakeTitleRow(a_movie, &first, kGameplayTitle);
			double sortIndex = 0.0;
			if (NumberMember(first, "sortIndex", sortIndex)) { title.SetMember("sortIndex", RE::GFxValue(sortIndex - 1.0)); }
			rows.push_back(title);
			for (std::uint32_t i = 0; i < count; ++i)
			{
				RE::GFxValue v;
				if (a_entries.GetElement(i, &v)) { rows.push_back(v); }
			}
			a_entries.SetArraySize(static_cast<std::uint32_t>(rows.size()));
			for (std::uint32_t i = 0; i < rows.size(); ++i) { a_entries.SetElement(i, rows[i]); }
			return true;
		}

		// Every row clip on stage: the name left-aligned at the list's own size, in the full name column, never shrunk.
		// Set on the clip each frame it differs - the list reuses clips as it scrolls - and a title row is drawn in the
		// list's highlight gold so a block is told apart at a glance.
		void LayOutRowClip(RE::GFxValue& a_clip, bool a_title, double a_width = kNameWidth)
		{
			RE::GFxValue field;
			if (!a_clip.GetMember("textField", &field) || !field.IsDisplayObject()) { return; }
			double width = 0.0;
			if (!NumberMember(field, "_width", width) || width != a_width) { field.SetMember("_width", RE::GFxValue(a_width)); }
			RE::GFxValue autoSize;
			if (!field.GetMember("textAutoSize", &autoSize) || !autoSize.IsString() || std::string_view(autoSize.GetString()) != "none")
			{
				field.SetMember("textAutoSize", RE::GFxValue("none"));
			}
			RE::GFxValue format;
			if (!field.Invoke("getTextFormat", &format, nullptr, 0) || !format.IsObject()) { return; }
			const std::string align = StringMember(format, "align");
			double color = -1.0;
			NumberMember(format, "color", color);
			const double wantColor = a_title ? static_cast<double>(0xC8A96E) : static_cast<double>(0xFFFFFF);
			const bool colorOwned = a_title || color == static_cast<double>(0xC8A96E);  // only undo the gold this mod set
			if (align == "left" && (!colorOwned || color == wantColor)) { return; }
			format.SetMember("align", RE::GFxValue("left"));
			if (colorOwned) { format.SetMember("color", RE::GFxValue(wantColor)); }
			field.Invoke("setTextFormat", nullptr, &format, 1);
			field.Invoke("setNewTextFormat", nullptr, &format, 1);
			if (colorOwned) { field.SetMember("textColor", RE::GFxValue(wantColor)); }
		}

		// The game leaves a control with no key out of the list altogether. Every control the INI list unbinds on the
		// device family being shown is put back as a row with no key, where controlmap.txt orders it, with a sortIndex
		// between its neighbours' so the list's own re-sorts (after a remap) keep it there. Returns true when rows were
		// added (the caller redraws the list).
		bool AddMissingRows(RE::GFxMovieView* a_movie, RE::GFxValue& a_entries, bool a_gamepad)
		{
			auto missing = unbinder::ListedKeylessOnFamily(a_gamepad);
			for (auto& bound : unbinder::BoundOnFamily(a_gamepad))
			{
				if (std::none_of(missing.begin(), missing.end(), [&](const unbinder::KeylessControl& a_k) { return a_k.event == bound.event; })) { missing.push_back(std::move(bound)); }
			}
			if (missing.empty()) { return false; }

			struct Row
			{
				RE::GFxValue value;
				std::string text;
				int order = -1;
				double sortIndex = 0.0;
				bool hasSort = false;
			};
			std::vector<Row> rows;
			const std::uint32_t count = a_entries.GetArraySize();
			rows.reserve(count + missing.size());
			for (std::uint32_t i = 0; i < count; ++i)
			{
				Row r;
				if (!a_entries.GetElement(i, &r.value)) { continue; }
				if (r.value.IsObject())
				{
					r.text = RowKey(r.value);
					r.order = r.text.empty() ? -1 : unbinder::OrderInContext(r.text);
					r.hasSort = NumberMember(r.value, "sortIndex", r.sortIndex);
				}
				rows.push_back(std::move(r));
			}

			int added = 0;
			for (const auto& control : missing)
			{
				const bool present = std::any_of(rows.begin(), rows.end(), [&](const Row& a_row) { return a_row.text == control.event; });
				if (present) { continue; }

				std::size_t pos = rows.size();
				for (std::size_t i = 0; i < rows.size(); ++i)
				{
					if (rows[i].order > control.order) { pos = i; break; }
				}

				Row r;
				r.text = control.event;
				r.order = control.order;
				a_movie->CreateObject(&r.value);
				// Take the bookkeeping from the row this one is going next to, before the identity members are set.
				if (!rows.empty()) { CloneRowMembers(rows[pos > 0 ? pos - 1 : 0].value, r.value, control.event.c_str()); }
				r.value.SetMember("text", RE::GFxValue(control.event.c_str()));
				r.value.SetMember("buttonName", RE::GFxValue(""));
				r.value.SetMember("buttonID", RE::GFxValue(static_cast<double>(kUnmappedID)));
				r.value.SetMember("_uvcAdded", RE::GFxValue(true));
				const bool hasPrev = pos > 0 && rows[pos - 1].hasSort;
				const bool hasNext = pos < rows.size() && rows[pos].hasSort;
				if (hasPrev || hasNext)
				{
					r.sortIndex = hasPrev && hasNext ? (rows[pos - 1].sortIndex + rows[pos].sortIndex) / 2.0 :
					              hasPrev            ? rows[pos - 1].sortIndex + 0.5 :
					                                   rows[pos].sortIndex - 0.5;
					r.hasSort = true;
					r.value.SetMember("sortIndex", RE::GFxValue(r.sortIndex));
				}
				logger::debug("controls list: \"{}\" put back as a row with no key at position {} of {} (order {}, sortIndex {})",
							  control.event, pos, rows.size() + 1, control.order, r.hasSort ? r.sortIndex : -1.0);
				rows.insert(rows.begin() + static_cast<std::ptrdiff_t>(pos), std::move(r));
				++added;
			}
			if (!added) { return false; }

			a_entries.SetArraySize(static_cast<std::uint32_t>(rows.size()));
			for (std::uint32_t i = 0; i < rows.size(); ++i) { a_entries.SetElement(i, rows[i].value); }
			g_rowsAdded.fetch_add(added);
			logger::info("controls list: {} unbound control(s) put back into the {} list with no key", added, a_gamepad ? "gamepad" : "keyboard");
			return true;
		}

		// The extra rows ([Functions]) go at the END of the list, after every vanilla control. They are not user events,
		// so controlmap.txt gives them no place in its order, and appending is the one position that cannot push a
		// vanilla row out of the order the player already knows. Their key text is written on every pass rather than
		// once: the row's binding changes while the page is open (the player just bound it), and the game never
		// rewrites an entry it did not create.
		bool AddFunctionRows(RE::GFxMovieView* a_movie, RE::GFxValue& a_entries, bool a_gamepad)
		{
			const auto list = functions::GetFunctions();

			const std::uint32_t count = a_entries.GetArraySize();
			std::vector<std::string> present;
			present.reserve(count);
			for (std::uint32_t i = 0; i < count; ++i)
			{
				RE::GFxValue value;
				if (a_entries.GetElement(i, &value) && value.IsObject()) { present.push_back(RowKey(value)); }
				else { present.emplace_back(); }
			}

			int added = 0;

			// The designated Modifier row (the owner, 2026-09-16: "There should be a modifier key row, which would be
			// set to left trigger"). It is not a user event either, so it is drawn the same way an extra row is - but
			// its value lives in this mod's [Modifier] list and it is what every "Modifier" combination resolves to.
			{
				const int modDevice = a_gamepad ? 2 : 0;
				const std::uint16_t mod = unbinder::ModifierFor(modDevice);
				const std::string modText = RowButtonName(mod, modDevice);
				const auto at = std::find(present.begin(), present.end(), std::string(unbinder::kModifierRowName));
				if (at != present.end())
				{
					RE::GFxValue entry;
					const auto index = static_cast<std::uint32_t>(std::distance(present.begin(), at));
					if (a_entries.GetElement(index, &entry) && entry.IsObject() && StringMember(entry, "buttonName") != modText)
					{
						entry.SetMember("buttonName", RE::GFxValue(modText.c_str()));
						entry.SetMember("_uvcBaseName", RE::GFxValue(modText.c_str()));
						++added;
					}
				}
				else
				{
					{
						std::vector<std::string> keys;
						for (std::uint32_t i = 0; i < a_entries.GetArraySize(); ++i)
						{
							RE::GFxValue v;
							keys.push_back(a_entries.GetElement(i, &v) && v.IsObject() ? RowKey(v) : std::string());
						}
						if (AppendTitle(a_movie, a_entries, keys, kExtraTitle)) { ++added; }
					}
					RE::GFxValue value;
					a_movie->CreateObject(&value);
					RE::GFxValue last;
					double sortIndex = 0.0;
					bool hasSort = false;
					if (a_entries.GetArraySize() > 0 && a_entries.GetElement(a_entries.GetArraySize() - 1, &last) && last.IsObject())
					{
						CloneRowMembers(last, value, unbinder::kModifierRowName);
						hasSort = NumberMember(last, "sortIndex", sortIndex);
					}
					value.SetMember("text", RE::GFxValue(unbinder::kModifierRowName));
					value.SetMember("buttonName", RE::GFxValue(modText.c_str()));
					value.SetMember("buttonID", RE::GFxValue(static_cast<double>(mod)));
					value.SetMember("_uvcAdded", RE::GFxValue(true));
					value.SetMember("_uvcFunction", RE::GFxValue(true));
					value.SetMember("_uvcBaseName", RE::GFxValue(modText.c_str()));
					if (hasSort) { value.SetMember("sortIndex", RE::GFxValue(sortIndex + 1.0)); }
					const auto size = a_entries.GetArraySize();
					a_entries.SetArraySize(size + 1);
					a_entries.SetElement(size, value);
					present.push_back(unbinder::kModifierRowName);
					++added;
					logger::info("controls list: the Modifier row added to the {} list (button \"{}\")",
								 a_gamepad ? "gamepad" : "keyboard", modText.empty() ? "none" : modText);
				}
			}

			for (std::size_t f = 0; f < list.size(); ++f)
			{
				// The mod this row points at is not installed: no row, and nothing written on its behalf.
				if (!functions::IsPresentAt(f)) { continue; }
				const auto shown = functions::ShownBinding(f, a_gamepad);
				const int fnDevice = a_gamepad ? 2 : 0;
				// The tile shows the BUTTON; a modifier is spelled in front of it, the way a bound control's row is.
				std::string keyText = RowButtonName(shown.key, fnDevice);
				if (shown.key != kUnmappedID && shown.modifier != 0)
				{
					const std::string modPart = RowButtonName(shown.modifier, fnDevice);
					if (!modPart.empty()) { keyText = modPart + " + " + keyText; }
				}
				const auto at = std::find(present.begin(), present.end(), list[f].name);
				if (at != present.end())
				{
					// Already a row: only its key text can have changed.
					RE::GFxValue entry;
					const auto index = static_cast<std::uint32_t>(std::distance(present.begin(), at));
					if (a_entries.GetElement(index, &entry) && entry.IsObject() && StringMember(entry, "buttonName") != keyText)
					{
						entry.SetMember("buttonName", RE::GFxValue(keyText.c_str()));
						entry.SetMember("_uvcBaseName", RE::GFxValue(keyText.c_str()));
						++added;  // the list is redrawn so the new key shows
					}
					continue;
				}
				RE::GFxValue value;
				a_movie->CreateObject(&value);
				// The last real row is the template: an extra row goes after every vanilla control, so that is the
				// row it sits next to, and it carries whatever this journal's list keeps on a row.
				RE::GFxValue last;
				double sortIndex = 0.0;
				bool hasSort = false;
				if (a_entries.GetArraySize() > 0 && a_entries.GetElement(a_entries.GetArraySize() - 1, &last) && last.IsObject())
				{
					CloneRowMembers(last, value, list[f].name.c_str());
					hasSort = NumberMember(last, "sortIndex", sortIndex);
				}
				value.SetMember("text", RE::GFxValue(list[f].name.c_str()));
				value.SetMember("buttonName", RE::GFxValue(keyText.c_str()));
				value.SetMember("buttonID", RE::GFxValue(static_cast<double>(kUnmappedID)));
				// Appended, so it sorts after everything already there.
				if (hasSort) { value.SetMember("sortIndex", RE::GFxValue(sortIndex + 1.0)); }
				value.SetMember("_uvcAdded", RE::GFxValue(true));
				value.SetMember("_uvcFunction", RE::GFxValue(true));
				// ListShowsGamepad reads _uvcBaseName when it is there; an extra row must never be the row that
				// decides the device family, because its name is one this mod composed, not one the game sent
				// (the 1.0.8 finding that a modifier prefix turned "360_Y" into a name no family test matches).
				value.SetMember("_uvcBaseName", RE::GFxValue(keyText.c_str()));
				const auto size = a_entries.GetArraySize();
				a_entries.SetArraySize(size + 1);
				a_entries.SetElement(size, value);
				present.push_back(list[f].name);
				++added;
				logger::info("controls list: extra row \"{}\" added at the end of the {} list (key \"{}\")", list[f].name,
							 a_gamepad ? "gamepad" : "keyboard", keyText.empty() ? "none" : keyText);
			}
			return added > 0;
		}

		// The menu actions this mod turned from links into separate mappings (Unbinder, 1.1.1), one row each, after the
		// extra rows: "Inventory: Charge Item", "Menus: Cancel". A row is listed on the family its links were on - the
		// keyboard page shows the keyboard key (the mouse button when the keyboard has none), the gamepad page the button.
		// Drawn the way an extra row is: the game has no row for a menu action, so the key tile is this mod's.
		std::string DelinkedKeyText(const unbinder::DelinkedAction& a_d, bool a_gamepad)
		{
			for (const int device : a_gamepad ? std::vector<int>{ 2 } : std::vector<int>{ 0, 1 })
			{
				for (const std::uint16_t k : unbinder::LiveKeysIn(a_d.context, a_d.event, device))
				{
					if (k != kUnmappedID) { return RowButtonName(k, device); }
				}
			}
			return {};
		}

		bool AddDelinkedRows(RE::GFxMovieView* a_movie, RE::GFxValue& a_entries, bool a_gamepad)
		{
			auto list = unbinder::GetDelinked();
			if (list.empty()) { return false; }
			// One block per column, in the game's context order; inside a block, controlmap.txt's order. A row may be listed in
			// another menu's column (Charge Item under ITEMS), and hidden rows are left off (2026-09-28).
			std::erase_if(list, [](const unbinder::DelinkedAction& a_d) { return a_d.hidden; });
			if (list.empty()) { return false; }
			std::stable_sort(list.begin(), list.end(), [](const unbinder::DelinkedAction& a_l, const unbinder::DelinkedAction& a_r) { return a_l.sheetContext < a_r.sheetContext; });
			std::vector<std::string> present;
			for (std::uint32_t i = 0; i < a_entries.GetArraySize(); ++i)
			{
				RE::GFxValue value;
				present.push_back(a_entries.GetElement(i, &value) && value.IsObject() ? RowKey(value) : std::string());
			}
			int added = 0;
			for (const auto& d : list)
			{
				if (a_gamepad ? !d.gamepad : !d.keyboardFamily) { continue; }
				if (AppendTitle(a_movie, a_entries, present, d.title)) { ++added; }
				const std::string keyText = DelinkedKeyText(d, a_gamepad);
				const auto at = std::find(present.begin(), present.end(), d.row);
				if (at != present.end())
				{
					RE::GFxValue entry;
					const auto index = static_cast<std::uint32_t>(std::distance(present.begin(), at));
					if (a_entries.GetElement(index, &entry) && entry.IsObject() && StringMember(entry, "buttonName") != keyText)
					{
						entry.SetMember("buttonName", RE::GFxValue(keyText.c_str()));
						entry.SetMember("_uvcBaseName", RE::GFxValue(keyText.c_str()));
						++added;
					}
					continue;
				}
				RE::GFxValue value;
				a_movie->CreateObject(&value);
				RE::GFxValue last;
				double sortIndex = 0.0;
				bool hasSort = false;
				if (a_entries.GetArraySize() > 0 && a_entries.GetElement(a_entries.GetArraySize() - 1, &last) && last.IsObject())
				{
					CloneRowMembers(last, value, d.row.c_str());
					hasSort = NumberMember(last, "sortIndex", sortIndex);
				}
				value.SetMember("text", RE::GFxValue(d.action.c_str()));
				value.SetMember("_uvcRowKey", RE::GFxValue(d.row.c_str()));
				value.SetMember("buttonName", RE::GFxValue(keyText.c_str()));
				value.SetMember("buttonID", RE::GFxValue(static_cast<double>(kUnmappedID)));
				if (hasSort) { value.SetMember("sortIndex", RE::GFxValue(sortIndex + 1.0)); }
				value.SetMember("_uvcAdded", RE::GFxValue(true));
				value.SetMember("_uvcFunction", RE::GFxValue(true));
				value.SetMember("_uvcBaseName", RE::GFxValue(keyText.c_str()));
				const auto size = a_entries.GetArraySize();
				a_entries.SetArraySize(size + 1);
				a_entries.SetElement(size, value);
				present.push_back(d.row);
				++added;
				logger::debug("controls list: menu row \"{}\" added to the {} list (key \"{}\")", d.row, a_gamepad ? "gamepad" : "keyboard", keyText.empty() ? "none" : keyText);
			}
			return added > 0;
		}

		// Records the first key pressed while a remap is on. Runs on the main thread when the game dispatches input,
		// before the menus handle it.
		class InputSink : public RE::BSTEventSink<RE::InputEvent*>
		{
		public:
			static InputSink* GetSingleton()
			{
				static InputSink s;
				return &s;
			}

			RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_event, RE::BSTEventSource<RE::InputEvent*>*) override
			{
				if (!a_event) { return RE::BSEventNotifyControl::kContinue; }
				const bool listening = SteadyNowMs() < g_listenUntilMs.load();
				const bool journalOpen = g_journalOpen.load();
				for (auto* e = *a_event; e; e = e->next)
				{
					if (e->GetEventType() != RE::INPUT_EVENT_TYPE::kButton) { continue; }
					auto* button = e->AsButtonEvent();
					if (!button) { continue; }
					if (!journalOpen && button->IsDown() && e->GetDevice() == RE::INPUT_DEVICE::kGamepad)
					{
						const auto* userEvents = RE::UserEvents::GetSingleton();
						if (userEvents && button->QUserEvent() == userEvents->pause) { g_pauseDownMs.store(SteadyNowMs()); }
					}
					if (!g_remapArmed.load() && !listening) { continue; }
					if (listening && (button->IsDown() || button->IsUp()))
					{
						const auto& user = button->QUserEvent();
						logger::info("listen: device {} ({}) code 0x{:X} userEvent \"{}\" value {} held {:.2f}s{}", static_cast<int>(e->GetDevice()),
									 unbinder::DeviceName(static_cast<int>(e->GetDevice())), button->GetIDCode(), user.c_str() ? user.c_str() : "", button->Value(),
									 button->HeldDuration(), button->IsDown() ? " DOWN" : " UP");
					}
					if (!g_remapArmed.load()) { continue; }
					std::scoped_lock l(g_pressLock);
					const PressedKey now{ static_cast<int>(e->GetDevice()), button->GetIDCode() };
					if (button->IsDown())
					{
						if (!g_hasPress)
						{
							g_pressed = now;
							g_hasPress = true;
						}
						else if (!g_hasPress2 && !g_firstReleased && (now.device != g_pressed.device || now.code != g_pressed.code))
						{
							g_pressed2 = now;
							g_hasPress2 = true;
						}
					}
					else if (button->IsUp() && g_hasPress && now.device == g_pressed.device && now.code == g_pressed.code) { g_firstReleased = true; }
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		// The remap ended: decide what the press meant.
		void EvaluateRemap()
		{
			const std::string event = g_remapEvent;
			PressedKey pressed;
			bool hasPress = false;
			{
				std::scoped_lock l(g_pressLock);
				pressed = g_pressed;
				hasPress = g_hasPress;
			}
			if (event.empty())
			{
				SetRemapResult("a remap ended, but its row was not known");
				logger::debug("controls list: remap ended for an unknown row; nothing done");
				return;
			}

			const std::string pressedText = hasPress ? std::format("{} 0x{:02x}", unbinder::DeviceName(pressed.device), pressed.code) : std::string("nothing recorded");

			if (event.rfind("title:", 0) == 0)
			{
				SetRemapResult(std::format("\"{}\" is a block title; nothing to set", event.substr(6)));
				return;
			}

			// The Modifier row first - it is this mod's own, not a user event and not a [Functions] row.
			if (IEqualsView(event, unbinder::kModifierRowName))
			{
				if (!hasPress || pressed.device < 0 || pressed.device > 2)
				{
					SetRemapResult(std::format("\"{}\": the remap ended with no button recorded; the row is unchanged", event));
					return;
				}
				const std::uint16_t had = unbinder::ModifierFor(pressed.device);
				const bool same = (had == pressed.code);
				unbinder::SetModifier(pressed.device, same ? 0xFF : static_cast<std::uint16_t>(pressed.code));
				settings::Save();
				// Every combination that refers to the row moves with it, so the live map is rebuilt now.
				unbinder::ApplyAll("modifier row changed");
				const std::string text = same ? std::format("\"{}\": its own button pressed again ({}); the designated modifier is cleared", event, pressedText) :
												std::format("\"{}\": the designated modifier is now {}; every \"Modifier\" combination follows it", event, pressedText);
				SetRemapResult(text);
				logger::info("controls list: {}", text);
				return;
			}

			// A menu row (a linked action made separate, 1.1.1): the press is the whole answer, as for an extra row. Its own key
			// again unbinds it in that menu ([Unbound]); any other key becomes its [Bound] line in that menu's context, and
			// is refused when another action of that menu already holds it. The Favourite button's row never changes.
			unbinder::DelinkedAction menuRow;
			if (unbinder::FindDelinkedRow(event, menuRow))
			{
				if (!hasPress || pressed.device < 0 || pressed.device > 2)
				{
					SetRemapResult(std::format("\"{}\": the remap ended with no key recorded; the row is unchanged", event));
					return;
				}
				if (menuRow.fixed)
				{
					const std::string text = std::format("\"{}\" is fixed to F on the keyboard and Y on the controller; {} ignored", event, pressedText);
					SetRemapResult(text);
					logger::info("controls list: {}", text);
					return;
				}
				const auto keys = unbinder::LiveKeysIn(menuRow.context, menuRow.event, pressed.device);
				const bool own = std::any_of(keys.begin(), keys.end(), [&](std::uint16_t a_k) { return a_k == pressed.code; });
				std::string text;
				if (own)
				{
					std::string why;
					unbinder::RemoveBind(menuRow.context, menuRow.event, pressed.device);
					const bool ok = unbinder::Unbind(menuRow.context, menuRow.event, pressed.device, why);
					text = ok ? std::format("\"{}\": its own key pressed again ({}); unbound on the {}", event, pressedText, unbinder::DeviceName(pressed.device)) :
					            std::format("\"{}\": its own key pressed again ({}), but unbinding failed: {}", event, pressedText, why);
				}
				else
				{
					unbinder::Forget(menuRow.context, menuRow.event, pressed.device);
					unbinder::SetBind(menuRow.context, menuRow.event, pressed.device, static_cast<std::uint16_t>(pressed.code));
					unbinder::ApplyAll("menu row set");
					const auto now = unbinder::LiveKeysIn(menuRow.context, menuRow.event, pressed.device);
					if (std::find(now.begin(), now.end(), static_cast<std::uint16_t>(pressed.code)) == now.end())
					{
						unbinder::RemoveBind(menuRow.context, menuRow.event, pressed.device);
						unbinder::ApplyAll("menu row refused");
						text = std::format("\"{}\": {} refused - another action in that menu holds it (see the log)", event, pressedText);
					}
					else { text = std::format("\"{}\": bound to {}", event, pressedText); }
				}
				settings::Save();
				SetRemapResult(text);
				logger::info("controls list: {}", text);
				return;
			}

			// An extra row ([Functions]) next: it is not a user event, so the game changed nothing on the live map for
			// it and there is nothing to compare. The press IS the whole answer - its own key again unbinds it, any
			// other key binds it - and the new value goes straight to the mod that owns the function.
			std::size_t functionIndex = 0;
			if (functions::IsFunctionRow(event, &functionIndex))
			{
				if (!hasPress || pressed.device < 0 || pressed.device > 2)
				{
					SetRemapResult(std::format("\"{}\": the remap ended with no key recorded; the row is unchanged", event));
					logger::debug("controls list: extra row \"{}\" remap ended with no key recorded", event);
					return;
				}
				std::string text;
				if (functions::HoldsKey(functionIndex, pressed.device, pressed.code))
				{
					functions::Unbind(functionIndex, pressed.device);
					text = std::format("\"{}\": its own key pressed again ({}); the extra row is unbound", event, pressedText);
				}
				else
				{
					std::string why;
					if (!functions::Bind(functionIndex, pressed.device, pressed.code, why))
					{
						SetRemapResult(std::format("\"{}\": {} refused: {}", event, pressedText, why));
						logger::warn("controls list: extra row \"{}\": {} refused: {}", event, pressedText, why);
						return;
					}
					text = std::format("\"{}\": bound to {}", event, pressedText);
				}
				settings::Save();
				const int files = functions::Deliver("Controls menu");
				const std::string full = std::format("{}; {} target file(s) written", text, files);
				SetRemapResult(full);
				logger::info("controls list: {}", full);
				return;
			}

			std::array<std::vector<std::uint16_t>, 3> after;
			for (int d = 0; d < 3; ++d) { after[d] = unbinder::LiveKeys(event, d); }

			// 1. A control the INI list unbinds was given a key: the player bound it, so it leaves the list. A control a [Bound]
			// line gives a key was given ANOTHER key: the line follows the player's choice, or closing the journal would put
			// the old key back (the owner, 2026-09-14, remapped System Tab to Back and it returned to Start).
			int forgotten = 0;
			int rebound = 0;
			std::string reboundText;
			for (int d = 0; d < 3; ++d)
			{
				const auto newKey = std::find_if(after[d].begin(), after[d].end(), [](std::uint16_t a_k) { return a_k != kUnmappedID; });
				const bool gotKey = newKey != after[d].end();
				if (after[d] != g_before[d] && gotKey && unbinder::IsListed(event, d) && unbinder::Forget(0, event, d)) { ++forgotten; }
				if (after[d] != g_before[d] && gotKey && unbinder::UpdateBind(0, event, d, *newKey))
				{
					++rebound;
					reboundText = std::format("{} 0x{:02x}", unbinder::DeviceName(d), *newKey);
				}
			}
			if (rebound && !forgotten)
			{
				settings::Save();
				const std::string text = std::format("\"{}\" was given another key ({}): its [Bound] line follows it", event, reboundText);
				SetRemapResult(text);
				logger::info("controls list: {}", text);
				return;
			}
			if (forgotten)
			{
				settings::Save();
				const std::string text = std::format("\"{}\" was given a key ({} pressed): removed from the INI list for {} device(s)", event, pressedText, forgotten);
				SetRemapResult(text);
				logger::info("controls list: {}", text);
				return;
			}

			// 2. The key pressed is one the control already had, and the game changed nothing: unbind it on that device.
			if (hasPress && pressed.device >= 0 && pressed.device <= 2 && after[pressed.device] == g_before[pressed.device])
			{
				const auto& keys = g_before[pressed.device];
				const bool same = std::any_of(keys.begin(), keys.end(), [&](std::uint16_t a_k) { return a_k == pressed.code; });
				if (same)
				{
					std::string why;
					unbinder::RemoveBind(0, event, pressed.device);  // a bound control unbound here keeps no bind, or the next apply would give the key back
					const bool ok = unbinder::Unbind(0, event, pressed.device, why);
					if (ok) { settings::Save(); }
					const std::string text = ok ? std::format("\"{}\": its own key pressed again ({}); unbound on the {} and added to the INI list", event, pressedText, unbinder::DeviceName(pressed.device)) :
					                              std::format("\"{}\": its own key pressed again ({}), but unbinding failed: {}", event, pressedText, why);
					SetRemapResult(text);
					if (ok) { logger::info("controls list: {}", text); }
					else { logger::warn("controls list: {}", text); }
					return;
				}
			}

			const std::string text = std::format("\"{}\": remap ended ({} pressed; keyboard {} -> {}, mouse {} -> {}, gamepad {} -> {}); the game's result is kept", event, pressedText,
												 KeysText(g_before[0]), KeysText(after[0]), KeysText(g_before[1]), KeysText(after[1]), KeysText(g_before[2]), KeysText(after[2]));
			SetRemapResult(text);
			logger::debug("controls list: {}", text);
		}

		// bRemapMode is the System page's own flag: set when a row is pressed, cleared 200 ms after the game reports the
		// remap finished.
		void WatchRemap(RE::GFxMovieView* a_movie, const RE::GFxValue& a_list, const RE::GFxValue& a_entries)
		{
			const std::string page = PagePathCopy();
			RE::GFxValue flag;
			const bool remap = !page.empty() && a_movie->GetVariable(&flag, (page + ".bRemapMode").c_str()) && flag.IsBool() && flag.GetBool() && !g_sheet.capture &&
			                   g_sheet.releaseAtMs == 0;
			if (remap && !g_remapActive.load())
			{
				double selected = -1.0;
				std::string event;
				RE::GFxValue entry;
				if (NumberMember(a_list, "iSelectedIndex", selected) && selected >= 0 && a_entries.GetElement(static_cast<std::uint32_t>(selected), &entry) && entry.IsObject())
				{
					event = RowKey(entry);
				}
				g_remapEvent = event;
				for (int d = 0; d < 3; ++d) { g_before[d] = unbinder::LiveKeys(event, d); }
				g_beforeAll = unbinder::SnapshotGameplay();  // a remap can also take a key from another control
				{
					std::scoped_lock l(g_pressLock);
					g_hasPress = false;
					g_hasPress2 = false;
					g_firstReleased = false;
				}
				g_remapActive.store(true);
				g_remapArmed.store(true);
				logger::debug("controls list: remap started for \"{}\" (keyboard {}, mouse {}, gamepad {})", event, KeysText(g_before[0]), KeysText(g_before[1]), KeysText(g_before[2]));
			}
			else if (!remap && g_remapActive.load())
			{
				g_remapArmed.store(false);
				g_remapActive.store(false);
				EvaluateRemap();
				// 1.0.7: whatever the remap changed - the control itself and any control the game took the key from - is written
				// into the INI lists, so the controls live in this mod's own file; the game's ControlMap_Custom.txt is removed
				// when the journal closes (Unbinder).
				if (settings::general::keepRemapsInIni && settings::general::enabled)
				{
					if (const int n = unbinder::RecordChanges(g_beforeAll, "Controls menu remap"); n > 0)
					{
						settings::Save();
						logger::info("controls list: {} INI line(s) now hold this remap", n);
					}
				}
				g_beforeAll = {};
			}
		}

		// After the movie advanced: the remap watch, missing rows put back, then every visible row clip, matched to its
		// entry through itemIndex, shows its key or none.
		// The journal was opened by a controller "Pause" press: show the System tab, the way keyboard Esc opens it. The journal
		// movie picks its first tab in RestoreSavedSettings(aiSavedTab, abTabsDisabled), which the engine calls with its saved
		// tab; calling that method again (a method of the menu object - the safe call shape) with the last tab switches to
		// System in vanilla, SkyUI and Quest Journal Overhaul journals alike. Retried for a few frames in case the engine's own
		// call lands after the first frame. Returns true while the switch is still being confirmed.
		bool ShowSystemTab(RE::GFxMovieView* a_movie)
		{
			constexpr const char* kMenuCandidates[] = { "_root.QuestJournalFader.Menu_mc", "_root.Menu_mc", "_level0.QuestJournalFader.Menu_mc" };
			for (const char* path : kMenuCandidates)
			{
				RE::GFxValue tab;
				if (!a_movie->GetVariable(&tab, (std::string(path) + ".iCurrentTab").c_str()) || !tab.IsNumber()) { continue; }
				RE::GFxValue count;
				const int tabs = a_movie->GetVariable(&count, (std::string(path) + ".TabButtonGroup.length").c_str()) && count.IsNumber() ? static_cast<int>(count.GetNumber()) : 3;
				const int system = tabs > 0 ? tabs - 1 : 2;
				const int current = static_cast<int>(tab.GetNumber());
				if (current == system)
				{
					if (--g_systemTabFrames <= 0)
					{
						logger::debug("system tab: the journal shows the System tab ({} of {}), opened by a controller Pause press", system, tabs);
						return false;
					}
					return true;
				}
				RE::GFxValue disabled;
				const bool tabsDisabled = a_movie->GetVariable(&disabled, (std::string(path) + ".bTabsDisabled").c_str()) && disabled.IsBool() && disabled.GetBool();
				RE::GFxValue args[2];
				args[0].SetNumber(static_cast<double>(system));
				args[1].SetBoolean(tabsDisabled);
				a_movie->Invoke((std::string(path) + ".RestoreSavedSettings").c_str(), nullptr, args, 2);
				RE::GFxValue after;
				const bool read = a_movie->GetVariable(&after, (std::string(path) + ".iCurrentTab").c_str()) && after.IsNumber();
				logger::info("system tab: the journal opened on tab {} from a controller Pause press; switched to the System tab ({}): now tab {}", current, system,
							 read ? std::to_string(static_cast<int>(after.GetNumber())) : std::string("unknown"));
				g_systemTabFrames = 3;
				return true;
			}
			if (--g_systemTabFrames <= -10)
			{
				logger::warn("system tab: no journal menu object found (tried QuestJournalFader.Menu_mc and Menu_mc); the journal keeps its saved tab");
				return false;
			}
			return true;
		}

		void FixRows(RE::GFxMovieView* a_movie)
		{
			if (g_systemTabPending.load() && !ShowSystemTab(a_movie)) { g_systemTabPending.store(false); }

			RE::GFxValue list;
			if (!FindList(a_movie, list))
			{
				if (!g_searchFailedLogged)
				{
					g_searchFailedLogged = true;
					logger::debug("controls list: not found in the journal movie yet (tried SystemFader.Page_mc.MappingList and .InputMappingPanel.List_mc); still looking each frame");
				}
				return;
			}

			RE::GFxValue entries;
			if (!list.GetMember("EntriesA", &entries) || !entries.IsArray()) { return; }
			if (entries.GetArraySize() == 0) { return; }  // the list fills when CONTROLS is pressed

			WatchRemap(a_movie, list, entries);

			const bool gamepad = ListShowsGamepad(entries);
			if (g_showsGamepad.exchange(gamepad) != gamepad) { logger::debug("controls list: now showing the {}", gamepad ? "gamepad" : "keyboard and mouse"); }

			bool changed = false;
			if (!g_remapActive.load())
			{
				changed = AddMissingRows(a_movie, entries, gamepad);
				// Both are asked, and the order matters: the vanilla rows this mod puts back are placed by
				// controlmap.txt order, and the extra rows go after all of them.
				changed = AddFunctionRows(a_movie, entries, gamepad) || changed;
				changed = AddDelinkedRows(a_movie, entries, gamepad) || changed;
				changed = AddGameplayTitle(a_movie, entries) || changed;
			}
			if (changed)
			{
				// InvalidateData is a method of the list object, the call shape that is safe (logic library).
				const std::string path = ListPathCopy();
				a_movie->Invoke((path + ".InvalidateData").c_str(), nullptr, nullptr, 0);
			}

			const std::uint32_t count = entries.GetArraySize();
			RE::GFxValue shownValue;
			int shown = kMaxRowClips;
			if (list.GetMember("iMaxItemsShown", &shownValue) && shownValue.IsNumber())
			{
				shown = std::clamp(static_cast<int>(shownValue.GetNumber()), 0, kMaxRowClips);
			}

			int blankNow = 0;
			for (int i = 0; i < shown; ++i)
			{
				RE::GFxValue clip;
				if (!list.GetMember(std::format("Entry{}", i).c_str(), &clip) || !clip.IsDisplayObject()) { break; }
				if (!IsVisible(clip)) { continue; }

				double idx = -1.0;
				if (!NumberMember(clip, "itemIndex", idx) || idx < 0 || idx >= count) { continue; }
				RE::GFxValue entry;
				if (!entries.GetElement(static_cast<std::uint32_t>(idx), &entry) || !entry.IsObject()) { continue; }

				const RowFacts row = ReadRow(entry, gamepad);
				{
					RE::GFxValue titleMark;
					LayOutRowClip(clip, entry.GetMember("_uvcTitle", &titleMark) && titleMark.IsBool() && titleMark.GetBool());
				}
				RE::GFxValue mark;
				const bool wasBlank = clip.GetMember("_uvcBlank", &mark) && mark.IsBool() && mark.GetBool();
				if (row.blank)
				{
					const int art = SetArtVisible(clip, false);
					if (!wasBlank) { clip.SetMember("_uvcBlank", RE::GFxValue(true)); }
					++blankNow;
					if (g_loggedRows.insert(std::format("{}|{}", gamepad ? "gamepad" : "keyboard", row.text)).second)
					{
						if (art == 0)
						{
							logger::warn("controls list: \"{}\" has no key but its row has no ButtonArt/buttonArt member; this journal draws the key some other way", row.text);
						}
						else
						{
							logger::debug("controls list: \"{}\" drawn with no key ({}; the game sent buttonName \"{}\", buttonID {}{})", row.text, row.why, row.buttonName,
										  row.buttonID, row.added ? "; row put back by this mod" : "");
						}
					}
				}
				else if (wasBlank)
				{
					// The clip now shows a control that has a key (a scroll reused it, or the control was just bound).
					SetArtVisible(clip, true);
					clip.SetMember("_uvcBlank", RE::GFxValue(false));
				}

				// A row this mod added whose label still starts with '$' is one the game could not translate.
				//
				// The list composes "$" + the row's name and puts it through the translation table - that is why the
				// GAME's own rows carry a bare name ("Left Attack/Block") and still read correctly. Vanilla has no
				// token for the quick-item hotkeys, because it never lists them here, and none for a row this mod
				// invents, so those came out as "$Hotkey1" and "$Modifier" (the owner, 2026-09-16).
				//
				// Stripping the dollar after the frame is drawn beats shipping tokens for them: it needs no table, it
				// works in any journal, and it is language-safe BECAUSE it only ever fires where translation already
				// failed - a row that resolved never starts with '$'. This runs after AdvanceMovie, so it is the last
				// word on the frame, the same standing this mod's key-art changes already have.
				if (row.added || row.function)
				{
					RE::GFxValue field;
					if (clip.GetMember("textField", &field) && field.IsObject())
					{
						RE::GFxValue shown;
						if (field.GetMember("text", &shown) && shown.IsString() && shown.GetString())
						{
							const std::string label = shown.GetString();
							if (label.size() > 1 && label.front() == '$')
							{
								const std::string plain = label.substr(1);
								field.SetMember("text", RE::GFxValue(plain.c_str()));
								if (g_loggedRows.insert("label|" + row.text).second)
								{
									logger::debug("controls list: \"{}\" had no translation ({}); shown as \"{}\"", row.text, label, plain);
								}
							}
						}
					}
				}

				// A bind that requires a modifier shows it in front of the key the game named: "Left Shift + Q".
				// Written onto the ENTRY, not the clip: the vanilla row function redraws each row from its entry
				// every frame, and the clip only carries itemIndex. The key's own name is left to the game, which
				// names it correctly for this journal and this gamepad type - only the prefix is ours.
				//
				// The guard is a PREFIX test, not "is the composed string different from what is there". This runs
				// every frame, and after the first write the row already reads "Left Shift + Q", so composing again
				// would give "Left Shift + Left Shift + Q" and grow without bound.
				if (!row.blank && !row.function && !row.text.empty() && !row.buttonName.empty())
				{
					const int device = gamepad ? 2 : 0;
					std::uint16_t modifier = 0;
					if (unbinder::BindModifier(row.text, device, modifier))
					{
						const char* modName = unbinder::ButtonName(modifier, device);
						const std::string prefix = (modName[0] ? std::string(modName) : std::format("0x{:02x}", modifier)) + " + ";
						if (row.buttonName.rfind(prefix, 0) != 0)
						{
							const std::string shown = prefix + row.buttonName;
							// Keep the game's own string: ListShowsGamepad reads this instead, so the device
							// family is never decided from a name this mod composed.
							entry.SetMember("_uvcBaseName", RE::GFxValue(row.buttonName.c_str()));
							entry.SetMember("buttonName", RE::GFxValue(shown.c_str()));
							if (g_loggedRows.insert(std::format("modifier|{}|{}", gamepad ? "gamepad" : "keyboard", row.text)).second)
							{
								logger::debug("controls list: \"{}\" shown as \"{}\" (its bind requires a modifier)", row.text, shown);
							}
						}
					}
				}
			}
			if (blankNow) { g_blankFrames.fetch_add(1); }
			g_rowsBlankNow.store(blankNow);
		}

		// THE CONTROLS SHEET (the owner, 2026-09-28, in turn: "break up all these different control rows so that they're not
		// just vertical, but are also separated horizontally ... each column would be for a different context"; "There's no
		// need to have the bumpers navigate anything or to have tabs. This is just one continuous sheet of D-pad selectable
		// options"; "the gameplay section of the controls should be two columns wide. And the gameplay header title should
		// be centered over these two columns ... all the game controls are in one area and don't need to scroll").
		//
		// The game's list stays the source of every row - its entries are what the game remaps, and this mod already adds
		// its own rows and block titles to them - but it is no longer drawn. The sheet draws the same entries in columns:
		// GAMEPLAY across two columns under one centred title, then EXTRA CONTROLS, then one column per menu. The D-pad moves
		// freely over it (left from the first column goes back to the System list, as the game's own list does) and the
		// sheet slides sideways to keep the selection on screen.
		//
		// Accept on a game control starts the game's own remap for it (the page's onInputMappingPress, with the row selected
		// in the hidden list, so the remap watch and the INI recording work exactly as before). Accept on a row of this
		// mod's - an extra row or a menu action - waits for the key itself: those rows are named after actions ("Run",
		// "Zoom In") that are also Gameplay controls, and the game's remap would move the Gameplay control of that name.
		//
		// A cell is a copy of the list's Entry0 drawn by the list's own SetEntry, so its key tile is this journal's.
		constexpr double kColWidth = 225.0;  // four columns and their key tiles inside the screen
		constexpr double kCellNameWidth = 160.0;
		constexpr double kCellArtX = 168.0;
		constexpr double kSlotPitch = 29.0;
		constexpr std::uint32_t kDimColor = 0x6E6E6E;  // this journal lifts greys: 0x808080 still reads white
		constexpr int kInputMappingState = 6;  // SystemPage.INPUT_MAPPING_STATE

		bool IsTitleEntry(const RE::GFxValue& a_entry)
		{
			RE::GFxValue v;
			return a_entry.GetMember("_uvcTitle", &v) && v.IsBool() && v.GetBool();
		}

		// A row this mod draws and owns (an extra row, the Modifier row, a menu action): its key is captured by this mod.
		bool IsOwnRow(const RE::GFxValue& a_entry)
		{
			RE::GFxValue v;
			if (a_entry.GetMember("_uvcRowKey", &v) && v.IsString()) { return true; }
			return a_entry.GetMember("_uvcFunction", &v) && v.IsBool() && v.GetBool();
		}

		std::vector<SheetColumn> BuildSheet(const RE::GFxValue& a_entries)
		{
			struct Block
			{
				std::string title;
				std::vector<std::string> rows;
			};
			std::vector<Block> blocks;
			for (std::uint32_t i = 0; i < a_entries.GetArraySize(); ++i)
			{
				RE::GFxValue entry;
				if (!a_entries.GetElement(i, &entry) || !entry.IsObject()) { continue; }
				if (IsTitleEntry(entry)) { blocks.push_back({ StringMember(entry, "text"), {} }); continue; }
				if (blocks.empty()) { blocks.push_back({ "", {} }); }
				blocks.back().rows.push_back(RowKey(entry));
			}
			std::vector<SheetColumn> out;
			const std::size_t perColumn = kSheetSlots - 1;
			for (const auto& b : blocks)
			{
				if (b.rows.empty()) { continue; }
				const bool gameplay = b.title == kGameplayTitle;
				// GAMEPLAY is always two columns, split evenly; any other block runs on into more columns only if it is long.
				const std::size_t split = gameplay ? (b.rows.size() + 1) / 2 : perColumn;
				for (std::size_t at = 0; at < b.rows.size(); at += split)
				{
					SheetColumn c;
					c.title = at == 0 ? b.title : std::string();
					c.titleSpans = gameplay && at == 0;
					c.rows.assign(b.rows.begin() + static_cast<std::ptrdiff_t>(at), b.rows.begin() + static_cast<std::ptrdiff_t>(std::min(b.rows.size(), at + split)));
					out.push_back(std::move(c));
				}
			}
			return out;
		}

		bool PageInControls(RE::GFxMovieView* a_movie, RE::GFxValue& a_page)
		{
			const std::string path = PagePathCopy();
			if (path.empty() || !a_movie->GetVariable(&a_page, path.c_str()) || !a_page.IsObject()) { return false; }
			double state = -1.0;
			return NumberMember(a_page, "iCurrentState", state) && static_cast<int>(state) == kInputMappingState;
		}

		void SheetMessage(RE::GFxValue& a_page, const char* a_text)
		{
			RE::GFxValue error;
			if (!a_page.GetMember("ErrorText", &error) || !error.IsDisplayObject()) { return; }
			if (a_text && a_text[0])
			{
				RE::GFxValue arg(a_text);
				error.Invoke("SetText", nullptr, &arg, 1);
			}
			else { a_page.Invoke("HideErrorText", nullptr, nullptr, 0); }
		}

		void KeepSelectionOnScreen()
		{
			const int count = static_cast<int>(g_sheet.columns.size());
			g_sheet.col = std::clamp(g_sheet.col, 0, std::max(0, count - 1));
			if (count > 0) { g_sheet.row = std::clamp(g_sheet.row, 0, std::max(0, static_cast<int>(g_sheet.columns[g_sheet.col].rows.size()) - 1)); }
			// GAMEPLAY's two columns come on screen together.
			int wantFirst = g_sheet.firstCol;
			const int start = (g_sheet.col > 0 && g_sheet.columns[g_sheet.col].title.empty() && g_sheet.columns[g_sheet.col - 1].titleSpans) ? g_sheet.col - 1 : g_sheet.col;
			if (start < wantFirst) { wantFirst = start; }
			if (g_sheet.col >= wantFirst + kSheetCols) { wantFirst = g_sheet.col - kSheetCols + 1; }
			g_sheet.firstCol = std::clamp(wantFirst, 0, std::max(0, count - kSheetCols));
		}

		// Accept on the selected cell.
		void ActivateCell(RE::GFxMovieView* a_movie, RE::GFxValue& a_page)
		{
			if (g_sheet.columns.empty()) { return; }
			const auto& column = g_sheet.columns[g_sheet.col];
			if (g_sheet.row >= static_cast<int>(column.rows.size())) { return; }
			const std::string key = column.rows[g_sheet.row];
			RE::GFxValue list;
			if (!FindList(a_movie, list)) { return; }
			RE::GFxValue entries;
			if (!list.GetMember("EntriesA", &entries) || !entries.IsArray()) { return; }
			for (std::uint32_t i = 0; i < entries.GetArraySize(); ++i)
			{
				RE::GFxValue entry;
				if (!entries.GetElement(i, &entry) || !entry.IsObject() || RowKey(entry) != key) { continue; }
				{
					unbinder::DelinkedAction menuRow;
					g_sheet.capture = true;
					a_page.SetMember("bRemapMode", RE::GFxValue(true));  // the page keeps every input to itself meanwhile
					SheetMessage(a_page, "$Press a button to map to this action.");
					g_remapEvent = key;
					for (auto& b : g_before) { b.clear(); }
					g_beforeAll = unbinder::SnapshotGameplay();
					{
						std::scoped_lock l(g_pressLock);
						g_hasPress = false;
						g_hasPress2 = false;
						g_firstReleased = false;
					}
					g_remapArmed.store(true);
					// Which rule set the key goes through: a game control (context 0), a menu action (its own context), or an
					// extra row / the Modifier row (EvaluateRemap, which writes into the owning mod's file).
					g_sheet.captureContext = -1;
					g_sheet.captureEvent.clear();
					if (unbinder::FindDelinkedRow(key, menuRow))
					{
						g_sheet.captureContext = menuRow.context;
						g_sheet.captureEvent = menuRow.event;
					}
					else if (!IsOwnRow(entry))
					{
						g_sheet.captureContext = 0;
						g_sheet.captureEvent = StringMember(entry, "text");
					}
					logger::debug("controls list: sheet cell \"{}\" waits for a key", key);
					return;
				}
			}
		}

		// The page's handleInput, wrapped: the sheet takes the D-pad and Accept while the Controls panel is open.
		class SheetInput : public RE::GFxFunctionHandler
		{
		public:
			void Call(Params& a_params) override
			{
				bool handled = false;
				if (a_params.thisPtr && a_params.argCount >= 1 && a_params.args && a_params.args[0].IsObject())
				{
					handled = Handle(a_params.movie, *a_params.thisPtr, a_params.args[0]);
				}
				if (handled)
				{
					if (a_params.retVal) { a_params.retVal->SetBoolean(true); }
					return;
				}
				RE::GFxValue original;
				if (!a_params.thisPtr || !a_params.thisPtr->GetMember("_uvcHandleInput", &original) || original.IsUndefined()) { return; }
				std::vector<RE::GFxValue> args;
				args.push_back(*a_params.thisPtr);
				for (std::uint32_t i = 0; i < a_params.argCount; ++i) { args.push_back(a_params.args[i]); }
				RE::GFxValue result;
				original.Invoke("call", &result, args.data(), args.size());
				if (a_params.retVal) { *a_params.retVal = result; }
			}

		private:
			static bool Handle(RE::GFxMovie* a_movie, RE::GFxValue& a_page, const RE::GFxValue& a_details)
			{
				double state = -1.0;
				if (!NumberMember(a_page, "iCurrentState", state) || static_cast<int>(state) != kInputMappingState) { return false; }
				RE::GFxValue remap;
				if (a_page.GetMember("bRemapMode", &remap) && remap.IsBool() && remap.GetBool()) { return g_sheet.capture; }
				if (g_sheet.columns.empty()) { return false; }
				const std::string value = StringMember(a_details, "value");
				const std::string nav = StringMember(a_details, "navEquivalent");
				{
					double code = -1.0;
					NumberMember(a_details, "code", code);
					logger::trace("controls sheet: input value \"{}\" nav \"{}\" code {} (column {}, row {})", value, nav, code, g_sheet.col, g_sheet.row);
				}
				if (value != "keyDown" && value != "keyHold") { return false; }
				if (nav == "up") { --g_sheet.row; }
				else if (nav == "down") { ++g_sheet.row; }
				else if (nav == "left")
				{
					if (g_sheet.col == 0) { return false; }  // the page turns it into Tab: back to the System list
					--g_sheet.col;
				}
				else if (nav == "right") { ++g_sheet.col; }
				else if (nav == "enter-gamepad_A")
				{
					if (value != "keyDown") { return true; }
					auto* movie = static_cast<RE::GFxMovieView*>(a_movie);
					if (movie) { ActivateCell(movie, a_page); }
					return true;
				}
				else { return false; }
				KeepSelectionOnScreen();
				return true;
			}
		};

		// The mouse on the sheet (the owner, 2026-09-28: "the mouse couldnt select a control to change and it was locked to
		// one entry"). A cell is a duplicate of the list's Entry0, and duplicateMovieClip does not carry the rollover and
		// press handlers the list gave its own rows - which are hidden - so nothing answered the mouse. Each cell gets its
		// own: rolling over selects it, a press selects it and starts the remap exactly as Accept does.
		class SheetMouse : public RE::GFxFunctionHandler
		{
		public:
			explicit SheetMouse(bool a_press) :
				press(a_press) {}

			void Call(Params& a_params) override
			{
				if (!a_params.thisPtr || !a_params.movie || g_sheet.capture || g_sheet.columns.empty()) { return; }
				double col = -1.0, slot = -1.0;
				if (!NumberMember(*a_params.thisPtr, "_uvcCol", col) || !NumberMember(*a_params.thisPtr, "_uvcSlot", slot) || slot < 1.0) { return; }
				const int column = g_sheet.firstCol + static_cast<int>(col);
				const int row = static_cast<int>(slot) - 1;
				if (column < 0 || column >= static_cast<int>(g_sheet.columns.size()) || row >= static_cast<int>(g_sheet.columns[column].rows.size())) { return; }
				auto* movie = static_cast<RE::GFxMovieView*>(a_params.movie);
				RE::GFxValue page;
				if (!PageInControls(movie, page)) { return; }
				RE::GFxValue remap;
				if (page.GetMember("bRemapMode", &remap) && remap.IsBool() && remap.GetBool()) { return; }
				if (g_sheet.col != column || g_sheet.row != row) { logger::trace("controls sheet: mouse {} column {}, row {}", press ? "press" : "over", column, row); }
				g_sheet.col = column;
				g_sheet.row = row;
				if (press) { ActivateCell(movie, page); }
			}

		private:
			bool press;
		};

		void GiveCellTheMouse(RE::GFxMovieView* a_movie, RE::GFxValue& a_cell, int a_col, int a_slot)
		{
			static SheetMouse* over = new SheetMouse(false);
			static SheetMouse* press = new SheetMouse(true);
			a_cell.SetMember("_uvcCol", RE::GFxValue(static_cast<double>(a_col)));
			a_cell.SetMember("_uvcSlot", RE::GFxValue(static_cast<double>(a_slot)));
			RE::GFxValue fnOver, fnPress;
			a_movie->CreateFunction(&fnOver, over);
			a_movie->CreateFunction(&fnPress, press);
			a_cell.SetMember("onRollOver", fnOver);
			a_cell.SetMember("onPress", fnPress);
		}

		void InstallSheetInput(RE::GFxMovieView* a_movie, RE::GFxValue& a_page)
		{
			RE::GFxValue installed;
			if (a_page.GetMember("_uvcHandleInput", &installed) && !installed.IsUndefined()) { return; }
			RE::GFxValue original;
			if (!a_page.GetMember("handleInput", &original) || original.IsUndefined()) { logger::warn("controls list: the System page has no handleInput; the controls sheet cannot take the D-pad"); return; }
			static SheetInput* handler = new SheetInput();
			RE::GFxValue fn;
			a_movie->CreateFunction(&fn, handler);
			a_page.SetMember("_uvcHandleInput", original);
			a_page.SetMember("handleInput", fn);
			logger::debug("controls list: the System page's input now reaches the controls sheet first");
		}

		void HideSheet(RE::GFxValue& a_list)
		{
			for (int c = 0; c < kSheetCols; ++c)
			{
				for (int s = 0; s < kSheetSlots; ++s)
				{
					RE::GFxValue cell;
					if (a_list.GetMember(std::format("uvcCell{}_{}", c, s).c_str(), &cell) && cell.IsDisplayObject() && IsVisible(cell)) { SetVisible(cell, false); }
				}
			}
			g_sheet.drawn.clear();
		}

		// The list is the data; the sheet is what is seen. Its own rows, arrows and scrollbar are hidden every frame
		// (the list shows them again whenever it redraws).
		void HideListChrome(RE::GFxValue& a_list)
		{
			for (int i = 0; i < kMaxRowClips; ++i)
			{
				RE::GFxValue clip;
				if (!a_list.GetMember(std::format("Entry{}", i).c_str(), &clip) || !clip.IsDisplayObject()) { break; }
				if (IsVisible(clip)) { SetVisible(clip, false); }
			}
			for (const char* name : { "ScrollUp", "ScrollDown", "scrollbar" })
			{
				RE::GFxValue part;
				if (a_list.GetMember(name, &part) && part.IsDisplayObject() && IsVisible(part)) { SetVisible(part, false); }
			}
		}

		// A key combination ("PS3_LT + PS3_RT", "L-Shift + Q") drawn as its tiles: InputMappingArt draws several tiles when
		// its name map gives an array, so the combination is added to that map, from the codes the map already has for
		// each part. Without it the art falls back to the text.
		void TeachCombo(RE::GFxMovieView* a_movie, RE::GFxValue& a_cell, const std::string& a_key)
		{
			if (a_key.find(" + ") == std::string::npos) { return; }
			RE::GFxValue art;
			RE::GFxValue map;
			if (!a_cell.GetMember("buttonArt", &art) || !art.IsDisplayObject() || !art.GetMember("_buttonNameMap", &map) || !map.IsObject()) { return; }
			auto lower = [](std::string a_s) {
				for (auto& ch : a_s) { ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch))); }
				return a_s;
			};
			const std::string whole = lower(a_key);
			RE::GFxValue known;
			if (map.GetMember(whole.c_str(), &known) && known.IsArray()) { return; }
			RE::GFxValue codes;
			a_movie->CreateArray(&codes);
			std::size_t pos = 0;
			while (pos <= whole.size())
			{
				const std::size_t cut = whole.find(" + ", pos);
				const std::string part = whole.substr(pos, cut == std::string::npos ? std::string::npos : cut - pos);
				RE::GFxValue code;
				if (!map.GetMember(part.c_str(), &code) || !code.IsNumber()) { return; }
				codes.PushBack(code);
				if (cut == std::string::npos) { break; }
				pos = cut + 3;
			}
			map.SetMember(whole.c_str(), codes);
		}

		// The FONT colour in a field's html, replaced: the way a colour reaches a duplicated field in this journal (its
		// textColor does not redraw).
		void SetHtmlColor(RE::GFxValue& a_field, std::uint32_t a_rgb)
		{
			RE::GFxValue html;
			if (!a_field.GetMember("htmlText", &html) || !html.IsString() || !html.GetString()) { return; }
			std::string text = html.GetString();
			const std::string want = std::format("COLOR=\"#{:06X}\"", a_rgb);
			bool changed = false;
			for (std::size_t at = text.find("COLOR=\"#"); at != std::string::npos; at = text.find("COLOR=\"#", at + want.size()))
			{
				if (text.compare(at, want.size(), want) != 0)
				{
					text.replace(at, want.size(), want);
					changed = true;
				}
			}
			if (changed) { a_field.SetMember("htmlText", RE::GFxValue(text.c_str())); }
		}

		// A key captured on the sheet for a game control or a menu action, applied as a [Bound] line in that control's context:
		// its own key again (no Modifier) unbinds it; a key another control of that context holds is SWAPPED with this one's
		// old key when both are Gameplay controls the game lets the player remap - the game's own rule - and refused
		// otherwise ("That button is reserved.").
		std::string ApplySheetBind(int a_context, const std::string& a_event, int a_device, std::uint16_t a_key, bool a_combo, bool& a_refused)
		{
			a_refused = false;
			std::uint16_t oldKey = kUnmappedID, oldMod = 0;
			unbinder::LiveBinding(a_context, a_event, a_device, oldKey, oldMod);
			const std::uint16_t wantMod = a_combo ? unbinder::ModifierFor(a_device) : 0;
			const std::string keyText = std::format("{} 0x{:02x}{}", unbinder::DeviceName(a_device), a_key, a_combo ? " + Modifier" : "");
			if (!a_combo && a_key == oldKey && oldMod == 0)
			{
				std::string why;
				unbinder::RemoveBind(a_context, a_event, a_device);
				const bool ok = unbinder::Unbind(a_context, a_event, a_device, why);
				return ok ? std::format("\"{}\": its own key pressed again ({}); unbound", a_event, keyText) : std::format("\"{}\": unbinding failed: {}", a_event, why);
			}
			unbinder::Holder holder;
			std::string swapped;
			if (unbinder::HolderOf(a_context, a_device, a_key, wantMod, holder) && !IEqualsView(holder.event, a_event))
			{
				if (a_context != 0 || !holder.remappable || oldKey == kUnmappedID)
				{
					a_refused = true;
					return std::format("\"{}\": {} refused - \"{}\" holds it", a_event, keyText, holder.event);
				}
				unbinder::Forget(a_context, holder.event, a_device);
				unbinder::SetBind(a_context, holder.event, a_device, oldKey, oldMod != 0);
				swapped = holder.event;
			}
			unbinder::Forget(a_context, a_event, a_device);
			unbinder::SetBind(a_context, a_event, a_device, a_key, a_combo);
			unbinder::ApplyAll("controls sheet");
			std::uint16_t nowKey = kUnmappedID, nowMod = 0;
			unbinder::LiveBinding(a_context, a_event, a_device, nowKey, nowMod);
			if (nowKey != a_key || nowMod != wantMod)
			{
				unbinder::RemoveBind(a_context, a_event, a_device);
				if (!swapped.empty()) { unbinder::RemoveBind(a_context, swapped, a_device); }
				unbinder::ApplyAll("controls sheet refused");
				a_refused = true;
				return std::format("\"{}\": {} refused (see the log)", a_event, keyText);
			}
			return swapped.empty() ? std::format("\"{}\": bound to {}", a_event, keyText) :
			                         std::format("\"{}\": bound to {}; \"{}\" took its old key", a_event, keyText, swapped);
		}

		void UpdateSheet(RE::GFxMovieView* a_movie)
		{
			RE::GFxValue list;
			if (!FindList(a_movie, list)) { return; }
			RE::GFxValue page;
			if (!PageInControls(a_movie, page))
			{
				HideSheet(list);
				return;
			}
			InstallSheetInput(a_movie, page);
			HideListChrome(list);

			// A capture of this mod's: the key the sink recorded is the answer, handled the way the rows' remap always was.
			if (g_sheet.capture)
			{
				bool pressed = false;
				{
					std::scoped_lock l(g_pressLock);
					pressed = g_hasPress;
				}
				PressedKey first, second;
				bool hasSecond = false, released = false;
				{
					std::scoped_lock l(g_pressLock);
					first = g_pressed;
					second = g_pressed2;
					hasSecond = g_hasPress2;
					released = g_firstReleased;
				}
				// The designated Modifier pressed first: wait for the button pressed while it is held (or its release, which
				// makes the Modifier button itself the key).
				const bool firstIsModifier = pressed && first.device >= 0 && first.device <= 2 && unbinder::ModifierFor(first.device) == first.code;
				if (pressed && firstIsModifier && !hasSecond && !released) { pressed = false; }
				if (pressed)
				{
					g_remapArmed.store(false);
					g_sheet.capture = false;
					const bool combo = firstIsModifier && hasSecond && second.device == first.device;
					const PressedKey key = combo ? second : first;
					bool refused = false;
					std::string result;
					if (g_sheet.captureContext >= 0 && key.device >= 0 && key.device <= 2)
					{
						result = ApplySheetBind(g_sheet.captureContext, g_sheet.captureEvent, key.device, static_cast<std::uint16_t>(key.code), combo, refused);
						settings::Save();
						SetRemapResult(result);
						logger::info("controls list: {}", result);
					}
					else
					{
						{
							std::scoped_lock l(g_pressLock);
							g_pressed = key;
						}
						EvaluateRemap();
						result = g_lastRemap;
						refused = result.find("refused") != std::string::npos;
					}
					g_beforeAll = {};
					SheetMessage(page, refused ? "$That button is reserved." : "");
					g_sheet.releaseAtMs = SteadyNowMs() + 200;  // the key's own release must not reach the page
					g_sheet.drawn.clear();
				}
			}
			if (!g_sheet.capture && g_sheet.releaseAtMs && SteadyNowMs() >= g_sheet.releaseAtMs)
			{
				g_sheet.releaseAtMs = 0;
				page.SetMember("bRemapMode", RE::GFxValue(false));
			}

			RE::GFxValue entries;
			if (!list.GetMember("EntriesA", &entries) || !entries.IsArray() || entries.GetArraySize() == 0) { HideSheet(list); return; }
			const bool gamepad = ListShowsGamepad(entries);
			g_sheet.columns = BuildSheet(entries);
			if (g_sheet.columns.empty()) { HideSheet(list); return; }
			KeepSelectionOnScreen();
			std::map<std::string, std::uint32_t> index;
			for (std::uint32_t i = 0; i < entries.GetArraySize(); ++i)
			{
				RE::GFxValue entry;
				if (entries.GetElement(i, &entry) && entry.IsObject()) { index.emplace(RowKey(entry), i); }
			}

			RE::GFxValue first;
			if (!list.GetMember("Entry0", &first) || !first.IsDisplayObject()) { return; }
			double top = 0.0;
			NumberMember(first, "_y", top);

			g_sheet.drawn.resize(static_cast<std::size_t>(kSheetCols * kSheetSlots));
			for (int c = 0; c < kSheetCols; ++c)
			{
				const int columnIndex = g_sheet.firstCol + c;
				const SheetColumn* column = columnIndex < static_cast<int>(g_sheet.columns.size()) ? &g_sheet.columns[columnIndex] : nullptr;
				for (int s = 0; s < kSheetSlots; ++s)
				{
					const std::size_t slot = static_cast<std::size_t>(c * kSheetSlots + s);
					const std::string name = std::format("uvcCell{}_{}", c, s);
					RE::GFxValue cell;
					if (!list.GetMember(name.c_str(), &cell) || !cell.IsDisplayObject())
					{
						RE::GFxValue args[2] = { RE::GFxValue(name.c_str()), RE::GFxValue(static_cast<double>(20000 + slot)) };
						first.Invoke("duplicateMovieClip", nullptr, args, 2);
						if (!list.GetMember(name.c_str(), &cell) || !cell.IsDisplayObject())
						{
							if (g_loggedRows.insert("sheet|nocell").second) { logger::warn("controls list: a sheet cell could not be made from Entry0; the controls sheet is not drawn"); }
							return;
						}
						cell.SetMember("_x", RE::GFxValue(c * kColWidth));
						cell.SetMember("_y", RE::GFxValue(top + s * kSlotPitch));
						GiveCellTheMouse(a_movie, cell, c, s);
					}
					const bool isTitle = s == 0;
					RE::GFxValue entry;
					bool haveEntry = false;
					if (column && !isTitle && s - 1 < static_cast<int>(column->rows.size()))
					{
						const auto it = index.find(column->rows[s - 1]);
						haveEntry = it != index.end() && entries.GetElement(it->second, &entry) && entry.IsObject();
					}
					if (!column || (isTitle ? column->title.empty() : !haveEntry))
					{
						if (IsVisible(cell)) { SetVisible(cell, false); }
						g_sheet.drawn[slot].clear();
						continue;
					}
					const bool selected = !isTitle && columnIndex == g_sheet.col && s - 1 == g_sheet.row;
					const bool capturing = selected && g_sheet.capture;
					std::string text = isTitle ? column->title : StringMember(entry, "text");
					// The two favourite rows named apart (the owner, 2026-09-28: "favorites has two rows, one for the menu and
					// one for the action of favoriting an item"): the Gameplay control opens the Favorites menu; ITEMS holds
					// "Favorite Item".
					if (!isTitle && !IsOwnRow(entry) && IEqualsView(text, "Favorites")) { text = "Favorites Menu"; }
					std::string key;
					if (!isTitle && !capturing)
					{
						const RowFacts facts = ReadRow(entry, gamepad);
						key = facts.blank ? std::string() : facts.buttonName;
						// A game control is read from the live map: the game names a row's key only when it builds the list,
						// and never learns of a bind this sheet made ("Shout" on LB + Modifier still read R2).
						if (!IsOwnRow(entry))
						{
							key.clear();
							for (const int device : gamepad ? std::vector<int>{ 2 } : std::vector<int>{ 0, 1 })
							{
								std::uint16_t k = kUnmappedID, mod = 0;
								if (!unbinder::LiveBinding(0, facts.text, device, k, mod) || k == kUnmappedID) { continue; }
								key = RowButtonName(k, device);
								if (mod != 0 && mod != kUnmappedID) { key = RowButtonName(mod, device) + " + " + key; }
								break;
							}
						}
					}
					// a title has no key tile, so it takes the whole column ("IN FAVORITES MENU" is not shrunk)
					const double nameWidth = isTitle ? (column->titleSpans ? 2 * kColWidth - 60.0 : kColWidth - 12.0) : kCellNameWidth;
					const std::string signature = std::format("{}|{}|{}|{}|{}", text, key, capturing ? 1 : 0, nameWidth, selected ? 1 : 0);
					if (!IsVisible(cell)) { SetVisible(cell, true); }
					if (g_sheet.drawn[slot] == signature) { continue; }
					TeachCombo(a_movie, cell, key);

					RE::GFxValue shown;
					a_movie->CreateObject(&shown);
					shown.SetMember("text", RE::GFxValue(text.c_str()));
					shown.SetMember("buttonName", RE::GFxValue(key.c_str()));
					RE::GFxValue args[2] = { cell, shown };
					list.Invoke("SetEntry", nullptr, args, 2);

					bool artReady = true;
					RE::GFxValue art;
					if (cell.GetMember("buttonArt", &art) && art.IsDisplayObject())
					{
						art.SetMember("_x", RE::GFxValue(kCellArtX));
						art.SetMember("_alpha", RE::GFxValue(100.0));
						SetVisible(art, !key.empty());
						// InputMappingArt builds its tile list in onLoad, which a duplicated clip runs on the NEXT frame.
						RE::GFxValue tiles;
						artReady = key.empty() || (art.GetMember("buttonArt", &tiles) && tiles.IsArray() && tiles.GetArraySize() > 0);
					}
					RE::GFxValue field;
					if (cell.GetMember("textField", &field) && field.IsDisplayObject())
					{
						field.SetMember("_alpha", RE::GFxValue(100.0));
						field.SetMember("textAutoSize", RE::GFxValue("none"));
						field.SetMember("_width", RE::GFxValue(nameWidth));
						RE::GFxValue label;
						if (field.GetMember("text", &label) && label.IsString() && label.GetString() && label.GetString()[0] == '$')
						{
							field.SetMember("text", RE::GFxValue(label.GetString() + 1));
						}
						RE::GFxValue format;
						if (field.Invoke("getTextFormat", &format, nullptr, 0) && format.IsObject())
						{
							format.SetMember("align", RE::GFxValue(isTitle && column->titleSpans ? "center" : "left"));
							field.Invoke("setTextFormat", nullptr, &format, 1);
						}
						// Titles gold; the selected row's name white and every other name grey - the list's own 70/100
						// alpha does not read in this journal, and the tiles stay at full strength so the sheet stays legible.
						SetHtmlColor(field, isTitle ? 0xC8A96Eu : (selected ? 0xFFFFFFu : kDimColor));
					}
					g_sheet.drawn[slot] = artReady ? signature : std::string();
				}
			}
		}

		struct AdvanceMovie
		{
			static void thunk(RE::JournalMenu* a_this, float a_interval, std::uint32_t a_currentTime)
			{
				func(a_this, a_interval, a_currentTime);
				if (!a_this || !a_this->uiMovie || !settings::general::enabled) { return; }
				systemmenu::OnFrame(a_this->uiMovie.get());  // [SystemMenu] rows (SystemMenu.h)
				FixRows(a_this->uiMovie.get());
				UpdateSheet(a_this->uiMovie.get());
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		// Opens the System page's Controls panel from code, for the DevBench tool.
		//
		// NOT by driving keys. Splicing a keypress into the Controls menu is how a control gets REMAPPED by accident:
		// the selection moves invisibly, the press is taken as the start of a remap, and the result is written out as
		// if the player had done it (logic library 4680). Driving a test that way risks rewriting the owner's own
		// bindings. The page already has the transition as a property - currentState, with INPUT_MAPPING_STATE as one
		// of its values - so it is set directly, which runs the page's own state change and touches no input at all.
		bool OpenControlsPanelImpl(std::string& a_why)
		{
			auto* ui = RE::UI::GetSingleton();
			if (!ui) { a_why = "no UI singleton"; return false; }
			const auto menu = ui->GetMenu(RE::JournalMenu::MENU_NAME);
			if (!menu || !menu->uiMovie) { a_why = "the journal is not open"; return false; }
			const std::string page = PagePathCopy();
			if (page.empty()) { a_why = "the System page has not been found yet - open the journal's System tab first"; return false; }
			auto* movie = menu->uiMovie.get();
			// INPUT_MAPPING_STATE is a constant of the page's CLASS, not of the instance, so where it can be read
			// from depends on how the journal's author declared it. Each of these is a plain variable read - never a
			// call through a function object, which is the shape that crashed the game in 1.0.2.
			const std::string candidates[] = {
				page + ".INPUT_MAPPING_STATE",
				page + ".__proto__.INPUT_MAPPING_STATE",
				"_global.SystemPage.INPUT_MAPPING_STATE",
				"_global.InputMappingList.INPUT_MAPPING_STATE",
			};
			RE::GFxValue state;
			bool found = false;
			for (const auto& path : candidates)
			{
				if (movie->GetVariable(&state, path.c_str()) && state.IsNumber())
				{
					found = true;
					logger::debug("controls list: INPUT_MAPPING_STATE read from {} ({})", path, state.GetNumber());
					break;
				}
			}
			if (!found)
			{
				// Say what IS there, so the next attempt is informed rather than another guess.
				RE::GFxValue pageValue;
				std::string members;
				if (movie->GetVariable(&pageValue, page.c_str()) && pageValue.IsObject())
				{
					pageValue.VisitMembers([&](const char* a_name, const RE::GFxValue&) {
						if (a_name) { members += (members.empty() ? "" : ", ") + std::string(a_name); }
					});
				}
				logger::warn("controls list: INPUT_MAPPING_STATE not found on this journal. The page's members are: {}",
							 members.empty() ? "(none readable)" : members);
				a_why = "this journal's System page has no readable INPUT_MAPPING_STATE - its members are in the log";
				return false;
			}
			if (!movie->SetVariable((page + ".currentState").c_str(), state)) { a_why = "setting currentState failed"; return false; }
			logger::info("controls list: Controls panel opened by setting {}.currentState to INPUT_MAPPING_STATE ({})", page, state.GetNumber());
			return true;
		}

		void ResetRemapWatch()
		{
			g_remapArmed.store(false);
			g_remapActive.store(false);
			g_remapEvent.clear();
		}
	}

	void Install()
	{
		if (g_hooked.load()) { return; }
		REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_JournalMenu[0] };
		AdvanceMovie::func = vtbl.write_vfunc(0x5, AdvanceMovie::thunk);
		g_hooked.store(true);
		logger::info("hook: JournalMenu::AdvanceMovie (vtable {:X}, slot 5) wrapped; the Controls list is checked after each journal frame", vtbl.address());
	}

	void InstallInputSink()
	{
		if (g_sinkInstalled.load()) { return; }
		auto* devices = RE::BSInputDeviceManager::GetSingleton();
		if (!devices)
		{
			logger::warn("controls list: the input device manager is null at kDataLoaded; pressing a control's own key in the Controls menu will not unbind it this session");
			return;
		}
		devices->AddEventSink(InputSink::GetSingleton());
		g_sinkInstalled.store(true);
		logger::info("sink registered: input events (records the key pressed during a Controls-menu remap)");
	}

	void Listen(int a_seconds)
	{
		const int seconds = a_seconds < 1 ? 1 : (a_seconds > 120 ? 120 : a_seconds);
		g_listenUntilMs.store(SteadyNowMs() + seconds * 1000LL);
		logger::info("listen: logging every button event for {} s (sink installed: {})", seconds, g_sinkInstalled.load());
	}

	void OnJournalOpen()
	{
		g_journalOpen.store(true);
		{
			const long long pressed = g_pauseDownMs.exchange(0);
			const long long since = pressed ? SteadyNowMs() - pressed : -1;
			const bool fromPause = since >= 0 && since < 1000;
			g_systemTabPending.store(fromPause);
			g_systemTabFrames = 0;
			if (fromPause) { logger::debug("controls list: journal opened by a controller Pause press {} ms ago; the System tab will be shown", since); }
		}
		g_loggedRows.clear();
		g_searchFailedLogged = false;
		g_sheet = {};
		g_blankFrames.store(0);
		g_rowsBlankNow.store(0);
		g_rowsAdded.store(0);
		ResetRemapWatch();
		{
			std::scoped_lock l(g_lock);
			g_listPath.clear();
			g_pagePath.clear();
		}
		logger::debug("controls list: journal opened");
	}

	void OnJournalClose()
	{
		g_journalOpen.store(false);
		g_systemTabPending.store(false);
		if (g_remapActive.load()) { logger::debug("controls list: the journal closed during a remap of \"{}\"; nothing decided", g_remapEvent); }
		ResetRemapWatch();
		{
			std::scoped_lock l(g_lock);
			g_listPath.clear();
			g_pagePath.clear();
		}
		logger::debug("controls list: journal closed ({} row(s) put back, {} frame(s) drew a row with no key this open)", g_rowsAdded.load(), g_blankFrames.load());
	}

	bool OpenControlsPanel(std::string& a_why)
	{
		return OpenControlsPanelImpl(a_why);
	}

	std::string RowsJson()
	{
		auto* movie = JournalMovie();
		if (!movie) { return R"({"ok":false,"op":"rows","error":"the journal is not open"})"; }
		RE::GFxValue list;
		if (!FindList(movie, list)) { return R"({"ok":false,"op":"rows","error":"no Controls list in this journal"})"; }
		RE::GFxValue entries;
		if (!list.GetMember("EntriesA", &entries) || !entries.IsArray()) { return R"({"ok":false,"op":"rows","error":"the list has no EntriesA array"})"; }
		const bool gamepad = ListShowsGamepad(entries);
		std::string out = std::format(R"({{"ok":true,"op":"rows","listPath":"{}","showing":"{}","count":{},"rows":[)", EscapeJson(ListPathCopy()),
									  gamepad ? "gamepad" : "keyboard", entries.GetArraySize());
		bool first = true;
		for (std::uint32_t i = 0; i < entries.GetArraySize(); ++i)
		{
			RE::GFxValue entry;
			if (!entries.GetElement(i, &entry) || !entry.IsObject()) { continue; }
			const RowFacts row = ReadRow(entry, gamepad);
			if (!first) { out += ","; }
			first = false;
			out += std::format(R"({{"text":"{}","buttonName":"{}","buttonID":{},"sortIndex":{},"added":{},"blank":{},"why":"{}"}})",
							   EscapeJson(row.text), EscapeJson(row.buttonName), row.buttonID, row.sortIndex, row.added ? "true" : "false",
							   row.blank ? "true" : "false", row.why);
		}
		out += "]}";
		return out;
	}

	// DevBench op=rowclips (journal open, Controls shown): the row clips on stage and their text field - position, size,
	// alignment, font size and auto-size modes - so a layout change is made against what this journal actually draws.
	std::string RowClipsJson()
	{
		auto* movie = JournalMovie();
		if (!movie) { return R"({"ok":false,"op":"rowclips","error":"the journal is not open"})"; }
		RE::GFxValue list;
		if (!FindList(movie, list)) { return R"({"ok":false,"op":"rowclips","error":"no Controls list in this journal"})"; }
		auto num = [](const RE::GFxValue& a_obj, const char* a_name) {
			RE::GFxValue v;
			return a_obj.GetMember(a_name, &v) && v.IsNumber() ? std::format("{:.1f}", v.GetNumber()) : std::string("null");
		};
		auto str = [](const RE::GFxValue& a_obj, const char* a_name) {
			RE::GFxValue v;
			if (!a_obj.GetMember(a_name, &v)) { return std::string("null"); }
			if (v.IsString() && v.GetString()) { return "\"" + EscapeJson(v.GetString()) + "\""; }
			if (v.IsBool()) { return std::string(v.GetBool() ? "true" : "false"); }
			if (v.IsNumber()) { return std::format("{:.1f}", v.GetNumber()); }
			return std::string("\"?\"");
		};
		std::string out = std::format(R"({{"ok":true,"op":"rowclips","list":{{"x":{},"y":{},"width":{},"iMaxItemsShown":{}}},"clips":[)", num(list, "_x"), num(list, "_y"),
									  num(list, "_width"), num(list, "iMaxItemsShown"));
		bool first = true;
		std::vector<std::string> names;
		for (int i = 0; i < kMaxRowClips; ++i) { names.push_back(std::format("Entry{}", i)); }
		for (int c = 0; c < kSheetCols; ++c)
		{
			for (int s = 0; s < kSheetSlots; ++s) { names.push_back(std::format("uvcCell{}_{}", c, s)); }
		}
		for (std::size_t i = 0; i < names.size(); ++i)
		{
			RE::GFxValue clip;
			if (!list.GetMember(names[i].c_str(), &clip) || !clip.IsDisplayObject()) { continue; }
			std::string members;
			clip.VisitMembers([&](const char* a_name, const RE::GFxValue&) { members += (members.empty() ? "" : ",") + std::string("\"") + EscapeJson(a_name) + "\""; });
			std::string field = "null";
			RE::GFxValue tf;
			if (clip.GetMember("textField", &tf) && tf.IsDisplayObject())
			{
				std::string fmt = "null";
				RE::GFxValue f;
				if (tf.Invoke("getTextFormat", &f, nullptr, 0) && f.IsObject())
				{
					fmt = std::format(R"({{"align":{},"size":{},"font":{},"color":{},"leftMargin":{},"indent":{}}})", str(f, "align"), str(f, "size"), str(f, "font"), str(f, "color"), str(f, "leftMargin"), str(f, "indent"));
				}
				field = std::format(R"({{"x":{},"y":{},"width":{},"height":{},"autoSize":{},"textAutoSize":{},"textColor":{},"alpha":{},"html":{},"text":{},"format":{}}})", num(tf, "_x"), num(tf, "_y"),
									num(tf, "_width"), num(tf, "_height"), str(tf, "autoSize"), str(tf, "textAutoSize"), str(tf, "textColor"), num(tf, "_alpha"), str(tf, "html"), str(tf, "text"), fmt);
			}
			std::string art = "null";
			for (const char* name : { "ButtonArt", "buttonArt" })
			{
				RE::GFxValue a;
				if (clip.GetMember(name, &a) && a.IsDisplayObject()) { art = std::format(R"({{"name":"{}","x":{},"y":{},"width":{}}})", name, num(a, "_x"), num(a, "_y"), num(a, "_width")); break; }
			}
			if (!first) { out += ","; }
			first = false;
			out += std::format(R"({{"clip":"{}","x":{},"y":{},"visible":{},"itemIndex":{},"members":[{}],"textField":{},"art":{}}})", names[i], num(clip, "_x"), num(clip, "_y"), str(clip, "_visible"),
							   num(clip, "itemIndex"), members, field, art);
		}
		out += "]}";
		return out;
	}

	// DevBench op=gfxget / op=gfxset (test only): read or write one member of an object in the journal movie, by its path
	// relative to the Controls list ("uvcCell0_1.textField") - for trying a look live before it is written into the mod.
	std::string GfxMember(std::string_view a_path, std::string_view a_member, std::string_view a_value, bool a_set)
	{
		auto* movie = JournalMovie();
		if (!movie) { return R"({"ok":false,"error":"the journal is not open"})"; }
		RE::GFxValue obj;
		if (!FindList(movie, obj)) { return R"({"ok":false,"error":"no Controls list"})"; }
		std::size_t pos = 0;
		const std::string path(a_path);
		while (pos < path.size())
		{
			const std::size_t dot = path.find('.', pos);
			const std::string part = path.substr(pos, dot == std::string::npos ? std::string::npos : dot - pos);
			RE::GFxValue next;
			if (part.empty() || !obj.GetMember(part.c_str(), &next) || next.IsUndefined()) { return std::format(R"({{"ok":false,"error":"no member {}"}})", EscapeJson(part)); }
			obj = next;
			if (dot == std::string::npos) { break; }
			pos = dot + 1;
		}
		const std::string member(a_member);
		if (a_set)
		{
			const std::string value(a_value);
			char* end = nullptr;
			const double number = std::strtod(value.c_str(), &end);
			if (value == "true" || value == "false") { obj.SetMember(member.c_str(), RE::GFxValue(value == "true")); }
			else if (!value.empty() && end && *end == '\0') { obj.SetMember(member.c_str(), RE::GFxValue(number)); }
			else { obj.SetMember(member.c_str(), RE::GFxValue(value.c_str())); }
		}
		RE::GFxValue v;
		std::string shown = "undefined";
		if (obj.GetMember(member.c_str(), &v))
		{
			if (v.IsString() && v.GetString()) { shown = "\"" + EscapeJson(v.GetString()) + "\""; }
			else if (v.IsNumber()) { shown = std::format("{}", v.GetNumber()); }
			else if (v.IsBool()) { shown = v.GetBool() ? "true" : "false"; }
			else if (v.IsObject()) { shown = "\"(object)\""; }
		}
		return std::format(R"({{"ok":true,"path":"{}","member":"{}","value":{}}})", EscapeJson(path), EscapeJson(member), shown);
	}

	std::string StateJson()
	{
		std::scoped_lock l(g_lock);
		return std::format(R"("controlsList":{{"hooked":{},"inputSink":{},"journalOpen":{},"listPath":"{}","showing":"{}","rowsAddedThisOpen":{},"rowsBlankInLatestFrame":{},"blankFramesThisOpen":{},"remapActive":{},"lastRemap":"{}","lastResult":"{}"}})",
						   g_hooked.load() ? "true" : "false", g_sinkInstalled.load() ? "true" : "false", g_journalOpen.load() ? "true" : "false", EscapeJson(g_listPath),
						   g_showsGamepad.load() ? "gamepad" : "keyboard", g_rowsAdded.load(), g_rowsBlankNow.load(), g_blankFrames.load(),
						   g_remapActive.load() ? "true" : "false", EscapeJson(g_lastRemap), EscapeJson(g_lastResult));
	}
}
