#include "PCH.h"

#include "ControlsList.h"

#include "Settings.h"
#include "Unbinder.h"
#include "utils/Logger.h"

#include <array>
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

		constexpr int kMaxAttempts = 120;  // UI tasks, one per frame: a movie not built after two seconds is not coming

		constexpr std::uint32_t kUnmappedID = 0xFF;  // the engine's own "no key"

		std::mutex g_lock;       // the strings below; the DevBench thread reads them
		std::string g_listPath;  // this open's list, empty when not attached
		std::string g_lastResult = "the journal has not been opened yet";

		std::atomic<bool> g_attached{ false };
		std::atomic<int> g_attaches{ 0 };      // journal opens where the row function was replaced
		std::atomic<int> g_blankedCalls{ 0 };  // row draws that hid a key, this open
		std::atomic<int> g_forwardFailures{ 0 };
		int g_openSerial = 0;                  // main thread
		std::set<std::string> g_loggedRows;    // main thread; rows already logged as blank this open

		void SetResult(std::string a_text)
		{
			std::scoped_lock l(g_lock);
			g_lastResult = std::move(a_text);
		}

		std::string ListPath()
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

		// Returns how many art members were found. Hiding covers both journal families; showing again never touches
		// `buttonPlacing`, because the vanilla row function sets that one itself on every draw.
		int SetArtVisible(RE::GFxValue& a_clip, bool a_visible)
		{
			int found = 0;
			for (const char* name : { "ButtonArt", "buttonArt" })
			{
				RE::GFxValue art;
				if (!a_clip.GetMember(name, &art) || !art.IsDisplayObject()) { continue; }
				RE::GFxValue::DisplayInfo info;
				if (!art.GetDisplayInfo(&info)) { continue; }
				info.SetVisible(a_visible);
				art.SetDisplayInfo(info);
				++found;
			}
			if (!a_visible)
			{
				RE::GFxValue placing;
				if (a_clip.GetMember("buttonPlacing", &placing) && placing.IsDisplayObject())
				{
					RE::GFxValue::DisplayInfo info;
					if (placing.GetDisplayInfo(&info))
					{
						info.SetVisible(false);
						placing.SetDisplayInfo(info);
					}
				}
			}
			return found;
		}

		class SetEntryHandler : public RE::GFxFunctionHandler
		{
		public:
			void Call(Params& a_params) override
			{
				if (!a_params.movie || !a_params.thisPtr) { return; }
				const std::string path = ListPath();
				if (path.empty()) { return; }

				// 1. The row as the journal draws it.
				std::array<RE::GFxValue, 3> args;
				args[0] = *a_params.thisPtr;
				if (a_params.argCount > 0) { args[1] = a_params.args[0]; }
				if (a_params.argCount > 1) { args[2] = a_params.args[1]; }
				if (!a_params.movie->Invoke((path + ".__proto__.SetEntry.call").c_str(), nullptr, args.data(), 3))
				{
					// Never leave the list unable to draw: give the instance its class function back and draw this row
					// through it.
					g_forwardFailures.fetch_add(1);
					a_params.thisPtr->DeleteMember("SetEntry");
					a_params.movie->Invoke((path + ".SetEntry").c_str(), nullptr, a_params.args, a_params.argCount);
					{
						std::scoped_lock l(g_lock);
						g_listPath.clear();
					}
					g_attached.store(false);
					SetResult("the forward to the list's own row function failed; the list is back to the game's drawing");
					logger::error("controls list: forwarding a row draw to \"{}.__proto__.SetEntry\" failed; the wrap is removed and the list draws as the game does", path);
					return;
				}

				// 2. Blank the key of an unbound control.
				if (!settings::general::enabled || a_params.argCount < 2) { return; }
				RE::GFxValue clip = a_params.args[0];
				const RE::GFxValue& entry = a_params.args[1];
				if (!clip.IsDisplayObject() || !entry.IsObject()) { return; }

				const RowFacts row = ReadRow(entry);
				RE::GFxValue mark;
				const bool wasBlank = clip.GetMember("_uvcBlank", &mark) && mark.IsBool() && mark.GetBool();
				if (row.unbound)
				{
					const int art = SetArtVisible(clip, false);
					clip.SetMember("_uvcBlank", RE::GFxValue(true));
					g_blankedCalls.fetch_add(1);
					if (g_loggedRows.insert(row.text).second)
					{
						if (art == 0)
						{
							logger::warn("controls list: \"{}\" is unbound but its row has no ButtonArt/buttonArt member; this journal draws the key some other way", row.text);
						}
						else
						{
							logger::debug("controls list: \"{}\" drawn blank ({}; game sent buttonName \"{}\", buttonID {})", row.text, row.why, row.buttonName, row.buttonID);
						}
					}
				}
				else if (wasBlank)
				{
					// A row clip is reused for another entry when the list scrolls.
					SetArtVisible(clip, true);
					clip.SetMember("_uvcBlank", RE::GFxValue(false));
				}
			}
		};

		RE::GFxFunctionHandler* Handler()
		{
			// One handler for the whole session. The movie holds its own reference; this one is never released, so the
			// object is never freed from under a movie that still points at it.
			static RE::GFxFunctionHandler* s_handler = new SetEntryHandler();
			return s_handler;
		}

		// true = finished (attached, or decided not to); false = the movie is not built yet, try again next frame.
		bool TryAttach()
		{
			auto* movie = JournalMovie();
			if (!movie) { return false; }

			std::string path;
			RE::GFxValue list;
			for (const char* page : kPageCandidates)
			{
				for (const char* member : kListMembers)
				{
					const std::string candidate = std::string(page) + member;
					if (movie->GetVariable(&list, candidate.c_str()) && list.IsDisplayObject())
					{
						path = candidate;
						break;
					}
				}
				if (!path.empty()) { break; }
			}
			if (path.empty()) { return false; }

			RE::GFxValue mark;
			if (list.GetMember("_uvcWrapped", &mark) && mark.IsBool() && mark.GetBool())
			{
				logger::debug("controls list: \"{}\" is already wrapped in this movie", path);
				return true;
			}

			// Probe: the forward must reach the class's own row function before the instance's is replaced. With no
			// clip and no entry the journal's SetEntry touches nothing (every member access on undefined is a no-op).
			std::array<RE::GFxValue, 3> probe;
			probe[0] = list;
			const std::string forward = path + ".__proto__.SetEntry.call";
			if (!movie->Invoke(forward.c_str(), nullptr, probe.data(), 3))
			{
				SetResult(std::format("\"{}\" is not reachable in this journal; unbound rows draw as the game draws them", forward));
				logger::warn("controls list: \"{}\" could not be called - this journal's Controls list is left as the game draws it (unbound rows keep their key art)", forward);
				return true;
			}

			RE::GFxValue fn;
			movie->CreateFunction(&fn, Handler());
			if (!list.SetMember("SetEntry", fn))
			{
				SetResult("setting SetEntry on the list failed; rows draw as the game draws them");
				logger::warn("controls list: SetMember(\"SetEntry\") failed on \"{}\"; rows draw as the game draws them", path);
				return true;
			}
			list.SetMember("_uvcWrapped", RE::GFxValue(true));
			{
				std::scoped_lock l(g_lock);
				g_listPath = path;
			}
			g_attached.store(true);
			const int n = g_attaches.fetch_add(1) + 1;
			SetResult(std::format("row drawing wrapped at \"{}\"", path));
			logger::info("controls list: row drawing wrapped at \"{}\" (journal open #{}); unbound controls draw with no key", path, n);

			// Rows already on screen (normally none - the list fills when CONTROLS is pressed) are drawn again.
			RE::GFxValue entries;
			if (movie->GetVariable(&entries, (path + ".entryList").c_str()) && entries.IsArray() && entries.GetArraySize() > 0)
			{
				movie->Invoke((path + ".UpdateList").c_str(), nullptr, nullptr, 0);
				logger::debug("controls list: {} row(s) were already listed; redrawn", entries.GetArraySize());
			}
			return true;
		}

		void Attempt(int a_serial, int a_attempt)
		{
			if (a_serial != g_openSerial) { return; }  // the journal closed (or reopened) since this was queued
			if (TryAttach()) { return; }
			if (a_attempt + 1 >= kMaxAttempts)
			{
				SetResult("the journal's Controls list was not found; rows draw as the game draws them");
				logger::warn("controls list: no Controls list found in the journal after {} frames (tried SystemFader.Page_mc.MappingList and .InputMappingPanel.List_mc); rows draw as the game draws them", kMaxAttempts);
				return;
			}
			if (a_attempt == 0) { logger::debug("controls list: the journal movie is not ready yet; retrying each frame"); }
			if (auto* tasks = SKSE::GetTaskInterface())
			{
				tasks->AddUITask([a_serial, a_attempt]() { Attempt(a_serial, a_attempt + 1); });
			}
		}
	}

	void OnJournalOpen()
	{
		++g_openSerial;
		g_loggedRows.clear();
		g_blankedCalls.store(0);
		{
			std::scoped_lock l(g_lock);
			g_listPath.clear();
		}
		g_attached.store(false);
		Attempt(g_openSerial, 0);
	}

	void OnJournalClose()
	{
		++g_openSerial;  // cancels a pending retry
		{
			std::scoped_lock l(g_lock);
			g_listPath.clear();
		}
		g_attached.store(false);
		logger::debug("controls list: journal closed ({} blank row draw(s) this open)", g_blankedCalls.load());
	}

	std::string RowsJson()
	{
		auto* movie = JournalMovie();
		const std::string path = ListPath();
		if (!movie) { return R"({"ok":false,"op":"rows","error":"the journal is not open"})"; }
		if (path.empty()) { return R"j({"ok":false,"op":"rows","error":"the Controls list is not wrapped in this journal (see state.controlsList.lastResult)"})j"; }
		RE::GFxValue entries;
		if (!movie->GetVariable(&entries, (path + ".entryList").c_str()) || !entries.IsArray())
		{
			return std::format(R"({{"ok":false,"op":"rows","error":"{}.entryList is not an array"}})", EscapeJson(path));
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
		return std::format(R"("controlsList":{{"attached":{},"listPath":"{}","attaches":{},"blankRowDrawsThisOpen":{},"forwardFailures":{},"lastResult":"{}"}})",
						   g_attached.load() ? "true" : "false", EscapeJson(g_listPath), g_attaches.load(), g_blankedCalls.load(), g_forwardFailures.load(), EscapeJson(g_lastResult));
	}
}
