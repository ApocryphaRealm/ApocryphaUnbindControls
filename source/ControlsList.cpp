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
		std::atomic<int> g_blankFrames{ 0 };  // frames that hid at least one key, this open
		std::atomic<int> g_rowsBlankNow{ 0 }; // rows drawn blank in the latest frame
		bool g_searchFailedLogged = false;    // main thread
		std::set<std::string> g_loggedRows;   // main thread; rows already logged as blank this open

		void SetResult(std::string a_text)
		{
			std::scoped_lock l(g_lock);
			g_lastResult = std::move(a_text);
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

		struct RowFacts
		{
			std::string text;        // the user event, e.g. "Quick Inventory"
			std::string buttonName;  // what the game sent for the key's art
			double buttonID = -1.0;  // the key code the game sent, -1 when absent
			bool unbound = false;
			const char* why = "bound";
		};

		RowFacts ReadRow(const RE::GFxValue& a_entry)
		{
			RowFacts f;
			RE::GFxValue v;
			if (a_entry.GetMember("text", &v) && v.IsString() && v.GetString()) { f.text = v.GetString(); }
			if (a_entry.GetMember("buttonName", &v) && v.IsString() && v.GetString()) { f.buttonName = v.GetString(); }
			if (a_entry.GetMember("buttonID", &v) && v.IsNumber()) { f.buttonID = v.GetNumber(); }
			if (f.buttonID == static_cast<double>(kUnmappedID))
			{
				f.unbound = true;
				f.why = "the game sent key 0xff";
			}
			else if (!f.text.empty() && unbinder::IsUnboundNow(f.text))
			{
				f.unbound = true;
				f.why = "in the INI list and keyless on the live map";
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
						logger::info("controls list: found at \"{}\"; unbound controls draw with no key", candidate);
						return true;
					}
				}
			}
			return false;
		}

		// After the movie advanced: every visible row clip, matched to its entry through itemIndex.
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
			const std::uint32_t count = entries.GetArraySize();
			if (count == 0) { return; }  // the list fills when CONTROLS is pressed

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

				RE::GFxValue itemIndex;
				if (!clip.GetMember("itemIndex", &itemIndex) || !itemIndex.IsNumber()) { continue; }
				const double idx = itemIndex.GetNumber();
				if (idx < 0 || idx >= count) { continue; }
				RE::GFxValue entry;
				if (!entries.GetElement(static_cast<std::uint32_t>(idx), &entry) || !entry.IsObject()) { continue; }

				const RowFacts row = ReadRow(entry);
				RE::GFxValue mark;
				const bool wasBlank = clip.GetMember("_uvcBlank", &mark) && mark.IsBool() && mark.GetBool();
				if (row.unbound)
				{
					const int art = SetArtVisible(clip, false);
					if (!wasBlank) { clip.SetMember("_uvcBlank", RE::GFxValue(true)); }
					++blankNow;
					if (g_loggedRows.insert(row.text).second)
					{
						if (art == 0)
						{
							logger::warn("controls list: \"{}\" is unbound but its row has no ButtonArt/buttonArt member; this journal draws the key some other way", row.text);
						}
						else
						{
							logger::debug("controls list: \"{}\" drawn blank ({}; the game sent buttonName \"{}\", buttonID {})", row.text, row.why, row.buttonName, row.buttonID);
						}
					}
				}
				else if (wasBlank)
				{
					// The clip now shows another control after a scroll: its key comes back.
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
		logger::debug("controls list: journal closed ({} frame(s) drew a blank key this open)", g_blankFrames.load());
	}

	std::string RowsJson()
	{
		auto* movie = JournalMovie();
		if (!movie) { return R"({"ok":false,"op":"rows","error":"the journal is not open"})"; }
		RE::GFxValue list;
		if (!FindList(movie, list)) { return R"({"ok":false,"op":"rows","error":"no Controls list in this journal"})"; }
		RE::GFxValue entries;
		if (!list.GetMember("EntriesA", &entries) || !entries.IsArray()) { return R"({"ok":false,"op":"rows","error":"the list has no EntriesA array"})"; }
		std::string path;
		{
			std::scoped_lock l(g_lock);
			path = g_listPath;
		}
		std::string out = std::format(R"({{"ok":true,"op":"rows","listPath":"{}","count":{},"rows":[)", EscapeJson(path), entries.GetArraySize());
		bool first = true;
		for (std::uint32_t i = 0; i < entries.GetArraySize(); ++i)
		{
			RE::GFxValue entry;
			if (!entries.GetElement(i, &entry) || !entry.IsObject()) { continue; }
			const RowFacts row = ReadRow(entry);
			if (!first) { out += ","; }
			first = false;
			out += std::format(R"({{"text":"{}","buttonName":"{}","buttonID":{},"blank":{},"why":"{}"}})",
							   EscapeJson(row.text), EscapeJson(row.buttonName), row.buttonID, row.unbound ? "true" : "false", row.why);
		}
		out += "]}";
		return out;
	}

	std::string StateJson()
	{
		std::scoped_lock l(g_lock);
		return std::format(R"("controlsList":{{"hooked":{},"journalOpen":{},"listPath":"{}","rowsBlankInLatestFrame":{},"blankFramesThisOpen":{},"lastResult":"{}"}})",
						   g_hooked.load() ? "true" : "false", g_journalOpen.load() ? "true" : "false", EscapeJson(g_listPath), g_rowsBlankNow.load(),
						   g_blankFrames.load(), EscapeJson(g_lastResult));
	}
}
