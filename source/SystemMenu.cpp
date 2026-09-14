#include "PCH.h"

#include "SystemMenu.h"

#include "utils/Logger.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <mutex>
#include <string>
#include <vector>

namespace systemmenu
{
	namespace
	{
		const std::string kPage = "_root.QuestJournalFader.Menu_mc.SystemFader.Page_mc";

		struct Row
		{
			const char* name;
			const char* token;
			const char* alias;  // a second token for the same row, or nullptr
		};
		// Skyrim SE 1.5.97 translate_english.txt: $CREATIONS and $MOD MANAGER both read CREATIONS.
		constexpr Row kRows[] = {
			{ "Quicksave", "$QUICKSAVE", nullptr }, { "Save", "$SAVE", nullptr }, { "Load", "$LOAD", nullptr },
			{ "Installed Content", "$INSTALLED CONTENT", nullptr }, { "Creations", "$CREATIONS", "$MOD MANAGER" },
			{ "Settings", "$SETTINGS", nullptr }, { "Mod Configuration", "$MOD CONFIGURATION", nullptr },
			{ "Controls", "$CONTROLS", nullptr }, { "Help", "$HELP", nullptr }, { "Quit", "$QUIT", nullptr },
		};

		std::mutex g_lock;
		std::vector<std::string> g_hidden;
		std::string g_page = "the journal has not been opened yet";
		int g_filteredNow = 0;  // rows carrying filterFlag 0 after the latest frame

		// Main thread.
		bool g_decided = false;
		bool g_loggedHidden = false;

		std::string Lower(std::string a_s)
		{
			for (char& c : a_s) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
			return a_s;
		}

		std::string EscapeJson(std::string_view a_in)
		{
			std::string out;
			for (const char c : a_in)
			{
				if (c == '"' || c == '\\') { out += '\\'; }
				if (static_cast<unsigned char>(c) < 0x20) { out += ' '; continue; }
				out += c;
			}
			return out;
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

		void SetPageText(std::string a_text)
		{
			std::scoped_lock l(g_lock);
			g_page = std::move(a_text);
		}

		std::vector<std::string> HiddenTokens()
		{
			std::vector<std::string> out;
			for (const auto& name : GetHidden())
			{
				const std::string lower = Lower(name);
				for (const auto& row : kRows)
				{
					if (lower == Lower(row.name) || lower == Lower(row.token) || (row.alias && lower == Lower(row.alias)))
					{
						out.push_back(row.token);
						if (row.alias) { out.push_back(row.alias); }
					}
				}
			}
			return out;
		}

		bool GetPage(RE::GFxMovie* a_movie, RE::GFxValue& a_page)
		{
			return a_movie && a_movie->GetVariable(&a_page, kPage.c_str()) && !a_page.IsUndefined() && !a_page.IsNull();
		}

		bool GetList(const RE::GFxValue& a_page, RE::GFxValue& a_list, RE::GFxValue& a_entries)
		{
			if (!a_page.GetMember("CategoryList", &a_list) || !a_list.IsDisplayObject()) { return false; }
			return a_list.GetMember("EntriesA", &a_entries) && a_entries.IsArray();
		}

		// The list class filters: Shared.CenteredScrollingList owns a Shared.ListFilterer, whose EntryMatchesFilter is
		// `filterFlag == undefined || (filterFlag & itemFilter) != 0`.
		bool ListFilters(const RE::GFxValue& a_list)
		{
			RE::GFxValue filterer;
			return a_list.GetMember("_filterer", &filterer) && filterer.IsObject();
		}
	}

	std::vector<std::string> DefaultHidden() { return { "Quicksave", "Installed Content", "Creations", "Help" }; }

	std::vector<std::string> GetHidden()
	{
		std::scoped_lock l(g_lock);
		return g_hidden;
	}

	void SetHidden(std::vector<std::string> a_rows)
	{
		std::scoped_lock l(g_lock);
		g_hidden = std::move(a_rows);
	}

	std::string TokenFor(std::string_view a_name)
	{
		const std::string name = Lower(std::string(a_name));
		for (const auto& row : kRows)
		{
			if (name == Lower(row.name) || name == Lower(row.token) || (row.alias && name == Lower(row.alias))) { return row.token; }
		}
		return {};
	}

	void OnJournalOpen()
	{
		g_decided = false;
		g_loggedHidden = false;
		std::scoped_lock l(g_lock);
		g_filteredNow = 0;
	}

	void OnFrame(RE::GFxMovieView* a_movie)
	{
		RE::GFxValue page;
		if (!GetPage(a_movie, page)) { return; }  // the System page is not loaded yet

		RE::GFxValue canonical;
		if (page.GetMember("__qjuiSystemCanonical", &canonical) && canonical.IsArray())
		{
			if (!g_decided)
			{
				g_decided = true;
				SetPageText("Quest Journal Overhaul - Entire Journal Redesigned: left alone (hide its rows in its MCM settings)");
				logger::info("system menu: this journal (Quest Journal Overhaul - Entire Journal Redesigned) hides System rows from its own MCM settings; [SystemMenu] leaves it alone");
			}
			return;
		}

		RE::GFxValue list, entries;
		if (!GetList(page, list, entries) || entries.GetArraySize() == 0) { return; }  // onLoad has not filled the list yet
		if (!ListFilters(list))
		{
			if (!g_decided)
			{
				g_decided = true;
				SetPageText("System list without a filterer: [SystemMenu] not applied");
				logger::info("system menu: this journal's System list has no filterer (_filterer), so rows cannot be hidden without changing what the rows below them do; [SystemMenu] is not applied");
			}
			return;
		}
		if (!g_decided)
		{
			g_decided = true;
			SetPageText("filtered System list: hidden rows carry filterFlag 0");
		}

		// Rows the game adds later (SetShowMod, installed content) arrive as new entry objects, so every frame looks.
		const auto tokens = HiddenTokens();
		bool changed = false;
		int filtered = 0;
		std::string names;
		for (std::uint32_t i = 0; i < entries.GetArraySize(); ++i)
		{
			RE::GFxValue entry;
			if (!entries.GetElement(i, &entry) || !entry.IsObject()) { continue; }
			const std::string text = StringMember(entry, "text");
			const bool hide = std::find(tokens.begin(), tokens.end(), text) != tokens.end();
			RE::GFxValue mark;
			const bool ours = entry.GetMember("_uvcFiltered", &mark) && mark.IsBool() && mark.GetBool();
			double flag = -1.0;
			const bool hasFlag = NumberMember(entry, "filterFlag", flag);
			if (hide)
			{
				if (!hasFlag || flag != 0.0)
				{
					entry.SetMember("filterFlag", RE::GFxValue(0.0));
					entry.SetMember("_uvcFiltered", RE::GFxValue(true));
					changed = true;
				}
				++filtered;
				names += (names.empty() ? "" : ", ") + text;
			}
			else if (ours)
			{
				// No longer listed (the INI was reloaded): the row is shown again.
				entry.DeleteMember("filterFlag");
				entry.SetMember("_uvcFiltered", RE::GFxValue(false));
				changed = true;
			}
		}
		{
			std::scoped_lock l(g_lock);
			g_filteredNow = filtered;
		}
		if (!changed) { return; }

		// InvalidateData hands EntriesA to the filterer again and redraws; a method call on the list object.
		a_movie->Invoke((kPage + ".CategoryList.InvalidateData").c_str(), nullptr, nullptr, 0);
		if (!g_loggedHidden && filtered > 0)
		{
			g_loggedHidden = true;
			logger::info("system menu: System rows hidden: {}", names);
		}
		else
		{
			logger::debug("system menu: rows hidden now: {}", names.empty() ? "none" : names);
		}
	}

	std::string RowsJson()
	{
		auto* ui = RE::UI::GetSingleton();
		auto menu = ui ? ui->GetMenu(RE::JournalMenu::MENU_NAME) : nullptr;
		RE::GFxMovieView* movie = (menu && menu->uiMovie) ? menu->uiMovie.get() : nullptr;
		if (!movie) { return R"({"ok":false,"op":"systemrows","error":"the journal is not open"})"; }
		RE::GFxValue page;
		if (!GetPage(movie, page)) { return std::format(R"({{"ok":false,"op":"systemrows","error":"no System page at {}"}})", kPage); }
		RE::GFxValue canonical;
		const bool qjo = page.GetMember("__qjuiSystemCanonical", &canonical) && canonical.IsArray();
		RE::GFxValue list, entries;
		if (!GetList(page, list, entries)) { return R"({"ok":false,"op":"systemrows","error":"the System page has no CategoryList.EntriesA"})"; }
		const auto tokens = HiddenTokens();

		std::string rows = "[";
		for (std::uint32_t i = 0; i < entries.GetArraySize(); ++i)
		{
			RE::GFxValue entry;
			if (!entries.GetElement(i, &entry) || !entry.IsObject()) { continue; }
			const std::string text = StringMember(entry, "text");
			double flag = -1.0;
			const bool hasFlag = NumberMember(entry, "filterFlag", flag);
			double clipIndex = -1.0;
			const bool drawn = NumberMember(entry, "clipIndex", clipIndex);
			if (rows.size() > 1) { rows += ","; }
			rows += std::format(R"({{"index":{},"text":"{}","listed":{},"filterFlag":{},"clipIndex":{}}})", i, EscapeJson(text),
								std::find(tokens.begin(), tokens.end(), text) != tokens.end() ? "true" : "false", hasFlag ? std::format("{}", flag) : std::string("null"),
								drawn ? std::format("{}", static_cast<int>(clipIndex)) : std::string("null"));
		}
		rows += "]";
		double sel = -1.0;
		NumberMember(list, "iSelectedIndex", sel);
		return std::format(R"({{"ok":true,"op":"systemrows","questJournalOverhaulRedesigned":{},"filterer":{},"selected":{},"rows":{},{}}})", qjo ? "true" : "false",
						   ListFilters(list) ? "true" : "false", static_cast<int>(sel), rows, StateJson());
	}

	std::string StateJson()
	{
		std::scoped_lock l(g_lock);
		std::string hidden = "[";
		for (const auto& name : g_hidden)
		{
			if (hidden.size() > 1) { hidden += ","; }
			hidden += "\"" + EscapeJson(name) + "\"";
		}
		hidden += "]";
		return std::format(R"("systemMenu":{{"hidden":{},"page":"{}","rowsFilteredNow":{}}})", hidden, EscapeJson(g_page), g_filteredNow);
	}
}
