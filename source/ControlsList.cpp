#include "PCH.h"

#include "ControlsList.h"

#include "Settings.h"
#include "Unbinder.h"
#include "utils/Logger.h"

#include <algorithm>
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
		std::string g_lastResult = "the journal has not been opened yet";

		std::atomic<bool> g_hooked{ false };
		std::atomic<bool> g_journalOpen{ false };
		std::atomic<bool> g_showsGamepad{ false };  // what the list showed in the latest frame
		std::atomic<int> g_blankFrames{ 0 };        // frames that hid at least one key, this open
		std::atomic<int> g_rowsBlankNow{ 0 };       // rows drawn blank in the latest frame
		std::atomic<int> g_rowsAdded{ 0 };          // rows put back into the list, this open
		bool g_searchFailedLogged = false;          // main thread
		std::set<std::string> g_loggedRows;         // main thread; rows already logged this open

		void SetResult(std::string a_text)
		{
			std::scoped_lock l(g_lock);
			g_lastResult = std::move(a_text);
		}

		std::string ListPathCopy()
		{
			std::scoped_lock l(g_lock);
			return g_listPath;
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
		// gamepad flag does not: 1.0.2's first AdvanceMovie build trusted it and blanked Start and Back while the list
		// showed the controller.)
		bool ListShowsGamepad(const RE::GFxValue& a_entries)
		{
			for (std::uint32_t i = 0; i < a_entries.GetArraySize(); ++i)
			{
				RE::GFxValue entry;
				if (!a_entries.GetElement(i, &entry) || !entry.IsObject()) { continue; }
				const std::string name = StringMember(entry, "buttonName");
				if (name.empty()) { continue; }
				return IsGamepadButtonName(name);
			}
			return false;
		}

		struct RowFacts
		{
			std::string text;        // the user event, e.g. "Quick Inventory"
			std::string buttonName;  // what the game sent for the key's art
			double buttonID = -1.0;  // the key code the game sent, -1 when absent
			double sortIndex = -1.0;
			bool added = false;      // put back by this mod
			bool blank = false;
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

		// After the movie advanced: put back missing rows, then every visible row clip, matched to its entry through
		// itemIndex, shows its key or none.
		void FixRows(RE::GFxMovieView* a_movie)
		{
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

			const bool gamepad = ListShowsGamepad(entries);
			if (g_showsGamepad.exchange(gamepad) != gamepad) { logger::debug("controls list: now showing the {}", gamepad ? "gamepad" : "keyboard and mouse"); }

			if (AddMissingRows(a_movie, entries, gamepad))
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
				FixRows(a_this->uiMovie.get());
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};
	}

	void Install()
	{
		if (g_hooked.load()) { return; }
		REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_JournalMenu[0] };
		AdvanceMovie::func = vtbl.write_vfunc(0x5, AdvanceMovie::thunk);
		g_hooked.store(true);
		logger::info("hook: JournalMenu::AdvanceMovie (vtable {:X}, slot 5) wrapped; the Controls list is checked after each journal frame", vtbl.address());
	}

	void OnJournalOpen()
	{
		g_journalOpen.store(true);
		g_loggedRows.clear();
		g_searchFailedLogged = false;
		g_blankFrames.store(0);
		g_rowsBlankNow.store(0);
		g_rowsAdded.store(0);
		{
			std::scoped_lock l(g_lock);
			g_listPath.clear();
		}
		logger::debug("controls list: journal opened");
	}

	void OnJournalClose()
	{
		g_journalOpen.store(false);
		{
			std::scoped_lock l(g_lock);
			g_listPath.clear();
		}
		logger::debug("controls list: journal closed ({} row(s) put back, {} frame(s) drew a row with no key this open)", g_rowsAdded.load(), g_blankFrames.load());
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
		return std::format(R"("controlsList":{{"hooked":{},"journalOpen":{},"listPath":"{}","showing":"{}","rowsAddedThisOpen":{},"rowsBlankInLatestFrame":{},"blankFramesThisOpen":{},"lastResult":"{}"}})",
						   g_hooked.load() ? "true" : "false", g_journalOpen.load() ? "true" : "false", EscapeJson(g_listPath),
						   g_showsGamepad.load() ? "gamepad" : "keyboard", g_rowsAdded.load(), g_rowsBlankNow.load(), g_blankFrames.load(), EscapeJson(g_lastResult));
	}
}
