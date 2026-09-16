#include "PCH.h"

#include "ControlsList.h"

#include "Functions.h"
#include "Settings.h"
#include "SystemMenu.h"
#include "Unbinder.h"
#include "utils/Logger.h"

#include <algorithm>
#include <array>
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
		std::string g_remapEvent;                       // main thread
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
			static const std::set<std::string> kOwn = { "text", "buttonName", "buttonID", "sortIndex", "_uvcAdded", "_uvcFunction", "_uvcBaseName" };
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
			// An extra row ([Functions]) is not a user event, so the live map knows nothing about it: its key comes
			// from the row's own binding, and the row is blank until the player gives it one.
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

		// The game leaves a control with no key out of the list altogether. Every control the INI list unbinds on the
		// device family being shown is put back as a row with no key, where controlmap.txt orders it, with a sortIndex
		// between its neighbours' so the list's own re-sorts (after a remap) keep it there. Returns true when rows were
		// added (the caller redraws the list).
		bool AddMissingRows(RE::GFxMovieView* a_movie, RE::GFxValue& a_entries, bool a_gamepad)
		{
			const auto missing = unbinder::ListedKeylessOnFamily(a_gamepad);
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
					r.text = StringMember(r.value, "text");
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
			if (list.empty()) { return false; }

			const std::uint32_t count = a_entries.GetArraySize();
			std::vector<std::string> present;
			present.reserve(count);
			for (std::uint32_t i = 0; i < count; ++i)
			{
				RE::GFxValue value;
				if (a_entries.GetElement(i, &value) && value.IsObject()) { present.push_back(StringMember(value, "text")); }
				else { present.emplace_back(); }
			}

			int added = 0;
			for (std::size_t f = 0; f < list.size(); ++f)
			{
				const std::string keyText = functions::ShownText(f, a_gamepad);
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
					if (!g_remapArmed.load() || !button->IsDown()) { continue; }
					std::scoped_lock l(g_pressLock);
					if (!g_hasPress)
					{
						g_pressed = { static_cast<int>(e->GetDevice()), button->GetIDCode() };
						g_hasPress = true;
					}
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

			// An extra row ([Functions]) first: it is not a user event, so the game changed nothing on the live map for
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
			const bool remap = !page.empty() && a_movie->GetVariable(&flag, (page + ".bRemapMode").c_str()) && flag.IsBool() && flag.GetBool();
			if (remap && !g_remapActive.load())
			{
				double selected = -1.0;
				std::string event;
				RE::GFxValue entry;
				if (NumberMember(a_list, "iSelectedIndex", selected) && selected >= 0 && a_entries.GetElement(static_cast<std::uint32_t>(selected), &entry) && entry.IsObject())
				{
					event = StringMember(entry, "text");
				}
				g_remapEvent = event;
				for (int d = 0; d < 3; ++d) { g_before[d] = unbinder::LiveKeys(event, d); }
				g_beforeAll = unbinder::SnapshotGameplay();  // a remap can also take a key from another control
				{
					std::scoped_lock l(g_pressLock);
					g_hasPress = false;
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

				// A bind that requires a modifier shows it in front of the key the game named: "Left Shift + Q".
				// Written onto the ENTRY, not the clip: the vanilla row function redraws each row from its entry
				// every frame, and the clip only carries itemIndex. The key's own name is left to the game, which
				// names it correctly for this journal and this gamepad type - only the prefix is ours.
				//
				// The guard is a PREFIX test, not "is the composed string different from what is there". This runs
				// every frame, and after the first write the row already reads "Left Shift + Q", so composing again
				// would give "Left Shift + Left Shift + Q" and grow without bound.
				if (!row.blank && !row.text.empty() && !row.buttonName.empty())
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

		struct AdvanceMovie
		{
			static void thunk(RE::JournalMenu* a_this, float a_interval, std::uint32_t a_currentTime)
			{
				func(a_this, a_interval, a_currentTime);
				if (!a_this || !a_this->uiMovie || !settings::general::enabled) { return; }
				systemmenu::OnFrame(a_this->uiMovie.get());  // [SystemMenu] rows (SystemMenu.h)
				FixRows(a_this->uiMovie.get());
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

	std::string StateJson()
	{
		std::scoped_lock l(g_lock);
		return std::format(R"("controlsList":{{"hooked":{},"inputSink":{},"journalOpen":{},"listPath":"{}","showing":"{}","rowsAddedThisOpen":{},"rowsBlankInLatestFrame":{},"blankFramesThisOpen":{},"remapActive":{},"lastRemap":"{}","lastResult":"{}"}})",
						   g_hooked.load() ? "true" : "false", g_sinkInstalled.load() ? "true" : "false", g_journalOpen.load() ? "true" : "false", EscapeJson(g_listPath),
						   g_showsGamepad.load() ? "gamepad" : "keyboard", g_rowsAdded.load(), g_rowsBlankNow.load(), g_blankFrames.load(),
						   g_remapActive.load() ? "true" : "false", EscapeJson(g_lastRemap), EscapeJson(g_lastResult));
	}
}
