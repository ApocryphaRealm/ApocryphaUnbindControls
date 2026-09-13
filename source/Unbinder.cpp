#include "PCH.h"

#include "Unbinder.h"

#include "Settings.h"
#include "utils/Logger.h"

#include "SKSE/Translation.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace unbinder
{
	namespace
	{
		constexpr std::uint16_t kUnmapped = 0xFF;  // ControlMap::kInvalid - the engine's own "no key"

		std::mutex g_lock;
		std::vector<Entry> g_entries;
		std::shared_ptr<const std::vector<Row>> g_rows = std::make_shared<const std::vector<Row>>();
		std::string g_lastApply = "never";
		int g_applyCount = 0;
		int g_lastTouched = 0;
		bool g_sinkInstalled = false;

		// The context names, in the engine's index order. SE and AE before 1.6.1130 have 17;
		// AE 1.6.1130+ inserts Marketplace at 16 and Favor becomes 17 (RE/U/UserEvents.h).
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

		// The engine's array of context pointers. AE 1.6.1130+ has one more than the header's
		// fixed 17, so index through the first element rather than the declared array.
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

		// Every mapping of one event on one device, in array order.
		std::vector<RE::ControlMap::UserEventMapping*> Find(Mappings& a_mappings, std::string_view a_event)
		{
			std::vector<RE::ControlMap::UserEventMapping*> out;
			for (auto& m : a_mappings)
			{
				if (m.eventID.c_str() && IEquals(m.eventID.c_str(), a_event)) { out.push_back(&m); }
			}
			return out;
		}

		// The engine's button -> event lookup is a binary search on inputKey (CommonLibSSE's
		// ControlMap::GetUserEventName mirrors it with std::equal_range), so the array must be
		// sorted by key again after any edit.
		void SortByKey(Mappings& a_mappings)
		{
			if (a_mappings.size() < 2) { return; }
			std::stable_sort(a_mappings.begin(), a_mappings.end(),
							 [](const RE::ControlMap::UserEventMapping& a_l, const RE::ControlMap::UserEventMapping& a_r) { return a_l.inputKey < a_r.inputKey; });
		}

		std::string Hex(std::uint16_t a_v) { return std::format("0x{:02x}", a_v); }

		std::string Translate(const std::string& a_key)
		{
			std::string out;
			if (!a_key.empty() && a_key[0] == '$' && SKSE::Translation::Translate(a_key, out) && !out.empty()) { return out; }
			return {};
		}

		std::string ButtonName(int a_device, std::uint16_t a_key)
		{
			if (a_key == kUnmapped) { return {}; }
			auto* idm = RE::BSInputDeviceManager::GetSingleton();
			RE::BSFixedString name;
			if (idm && idm->GetButtonNameFromID(static_cast<RE::INPUT_DEVICE>(a_device), static_cast<std::int32_t>(a_key), name) && name.c_str() && name.c_str()[0])
			{
				std::string s = name.c_str();
				if (s[0] == '$') { const std::string t = Translate(s); if (!t.empty()) { return t; } }
				return s;
			}
			return Hex(a_key);
		}

		std::string OriginalNames(const Entry& a_e)
		{
			std::string s;
			for (const auto& k : a_e.original)
			{
				if (!s.empty()) { s += ", "; }
				s += ButtonName(a_e.device, k.key);
			}
			return s.empty() ? std::string("-") : s;
		}

		std::vector<Entry>::iterator FindEntry(int a_context, std::string_view a_event, int a_device)
		{
			const char* name = ContextName(a_context);
			return std::find_if(g_entries.begin(), g_entries.end(), [&](const Entry& e) {
				return e.device == a_device && IEquals(e.context, name) && IEquals(e.event, a_event);
			});
		}

		// The journal is where the Controls menu lives; its Reset to defaults reloads the whole map
		// and a rebind rewrites entries, so every close re-applies the list.
		class MenuSink : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
		{
		public:
			static MenuSink* GetSingleton() { static MenuSink s; return &s; }

			RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
			{
				if (!a_event || a_event->opening) { return RE::BSEventNotifyControl::kContinue; }
				if (std::string_view(a_event->menuName.c_str()) != RE::JournalMenu::MENU_NAME) { return RE::BSEventNotifyControl::kContinue; }
				if (auto* tasks = SKSE::GetTaskInterface()) { tasks->AddTask([]() { ApplyAll("journal closed"); }); }
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
		logger::debug("unbinder: list set from the INI, {} entries", g_entries.size());
	}

	std::vector<Entry> DefaultEntries()
	{
		constexpr const char* kRedundantWithTween[] = { "Journal", "Quick Inventory", "Quick Magic", "Quick Map", "Quick Stats", "Wait" };
		std::vector<Entry> out;
		for (const char* ev : kRedundantWithTween)
		{
			Entry e;
			e.context = "Gameplay";
			e.event = ev;
			e.device = 0;  // keyboard
			out.push_back(std::move(e));
		}
		return out;
	}

	bool IsUnbound(int a_context, std::string_view a_event, int a_device)
	{
		std::scoped_lock l(g_lock);
		return FindEntry(a_context, a_event, a_device) != g_entries.end();
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
			if (FindEntry(a_context, a_event, a_device) != g_entries.end())
			{
				// Already ours - make sure the map agrees and leave the remembered originals alone.
				int touched = 0;
				for (auto* m : found) { if (m->inputKey != kUnmapped) { m->inputKey = kUnmapped; ++touched; } }
				if (touched) { SortByKey(*mappings); }
				logger::debug("unbind {}|{}|{}: already in the list; {} key(s) cleared again", ContextName(a_context), a_event, DeviceName(a_device), touched);
				return true;
			}
		}

		Entry e;
		e.context = ContextName(a_context);
		e.event = std::string(a_event);
		e.device = a_device;
		bool anyKey = false;
		for (auto* m : found)
		{
			e.original.push_back({ m->inputKey, m->modifier });
			if (m->inputKey != kUnmapped) { anyKey = true; }
		}
		if (!anyKey) { a_why = "the game already has no key there"; logger::info("unbind {}|{}|{}: {}", e.context, e.event, DeviceName(a_device), a_why); return false; }

		std::string before;
		for (auto* m : found) { if (!before.empty()) { before += ","; } before += Hex(m->inputKey) + "/" + Hex(m->modifier); m->inputKey = kUnmapped; }
		SortByKey(*mappings);
		{
			std::scoped_lock l(g_lock);
			g_entries.push_back(std::move(e));
		}
		logger::info("unbound {}|{}|{}: {} mapping(s), was {}", ContextName(a_context), a_event, DeviceName(a_device), found.size(), before);
		RebuildRows();
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
		if (!mappings)
		{
			a_why = "that context is not loaded; the control was dropped from the list without a key to give back";
			logger::warn("rebind {}|{}|{}: {}", e.context, e.event, DeviceName(a_device), a_why);
			RebuildRows();
			return false;
		}
		auto found = Find(*mappings, a_event);
		if (found.size() != e.original.size())
		{
			logger::warn("rebind {}|{}|{}: the map now has {} mapping(s) for it but {} original key(s) were remembered; restoring what matches",
						 e.context, e.event, DeviceName(a_device), found.size(), e.original.size());
		}
		const std::size_t n = std::min(found.size(), e.original.size());
		std::string after;
		for (std::size_t i = 0; i < n; ++i)
		{
			found[i]->inputKey = e.original[i].key;
			found[i]->modifier = e.original[i].modifier;
			if (!after.empty()) { after += ","; }
			after += Hex(found[i]->inputKey) + "/" + Hex(found[i]->modifier);
		}
		SortByKey(*mappings);
		logger::info("rebound {}|{}|{}: {} mapping(s) back to {}", e.context, e.event, DeviceName(a_device), n, after);
		RebuildRows();
		return true;
	}

	void ApplyAll(const char* a_reason)
	{
		std::vector<Entry> entries;
		{
			std::scoped_lock l(g_lock);
			entries = g_entries;
		}
		auto* map = RE::ControlMap::GetSingleton();
		if (!map)
		{
			logger::warn("apply ({}): the ControlMap singleton is null; nothing applied", a_reason);
			return;
		}
		if (!settings::general::enabled)
		{
			logger::info("apply ({}): disabled in the settings; {} entr{} left as the game has them", a_reason, entries.size(), entries.size() == 1 ? "y" : "ies");
			RebuildRows();
			return;
		}
		int touched = 0, missing = 0, captured = 0;
		std::vector<Mappings*> dirty;
		for (auto& e : entries)
		{
			const int ctx = ContextIndex(e.context);
			auto* mappings = ctx >= 0 ? MappingsFor(map, ctx, e.device) : nullptr;
			if (!mappings) { ++missing; logger::warn("apply ({}): {}|{}|{} - context unknown or not loaded", a_reason, e.context, e.event, DeviceName(e.device)); continue; }
			auto found = Find(*mappings, e.event);
			if (found.empty()) { ++missing; logger::warn("apply ({}): {}|{}|{} - the game has no mapping for it", a_reason, e.context, e.event, DeviceName(e.device)); continue; }
			if (e.original.empty())
			{
				// A hand-written INI line with no remembered key: remember what is there now.
				for (auto* m : found) { e.original.push_back({ m->inputKey, m->modifier }); }
				std::scoped_lock l(g_lock);
				auto it = FindEntry(ctx, e.event, e.device);
				if (it != g_entries.end()) { it->original = e.original; ++captured; }
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
		{
			std::scoped_lock l(g_lock);
			g_lastApply = a_reason;
			++g_applyCount;
			g_lastTouched = touched;
		}
		logger::info("apply ({}): {} entr{}, {} key(s) cleared in {} device list(s){}", a_reason, entries.size(), entries.size() == 1 ? "y" : "ies",
					 touched, dirty.size(), captured ? std::format(", {} original(s) captured", captured) : "");
		if (missing) { logger::warn("apply ({}): {} entr{} could not be applied (see above)", a_reason, missing, missing == 1 ? "y" : "ies"); }
		RebuildRows();
	}

	void RestoreAll(const char* a_reason)
	{
		std::vector<Entry> entries;
		{
			std::scoped_lock l(g_lock);
			entries = g_entries;
		}
		auto* map = RE::ControlMap::GetSingleton();
		if (!map) { logger::warn("restore ({}): the ControlMap singleton is null", a_reason); return; }
		int restored = 0;
		std::vector<Mappings*> dirty;
		for (const auto& e : entries)
		{
			const int ctx = ContextIndex(e.context);
			auto* mappings = ctx >= 0 ? MappingsFor(map, ctx, e.device) : nullptr;
			if (!mappings) { continue; }
			auto found = Find(*mappings, e.event);
			const std::size_t n = std::min(found.size(), e.original.size());
			for (std::size_t i = 0; i < n; ++i)
			{
				if (found[i]->inputKey != e.original[i].key || found[i]->modifier != e.original[i].modifier)
				{
					found[i]->inputKey = e.original[i].key;
					found[i]->modifier = e.original[i].modifier;
					++restored;
				}
			}
			if (n && std::find(dirty.begin(), dirty.end(), mappings) == dirty.end()) { dirty.push_back(mappings); }
		}
		for (auto* m : dirty) { SortByKey(*m); }
		logger::info("restore ({}): {} entr{}, {} key(s) given back", a_reason, entries.size(), entries.size() == 1 ? "y" : "ies", restored);
		RebuildRows();
	}

	void ClearAll()
	{
		RestoreAll("list cleared");
		{
			std::scoped_lock l(g_lock);
			g_entries.clear();
		}
		RebuildRows();
	}

	void RebuildRows()
	{
		auto rows = std::make_shared<std::vector<Row>>();
		auto* map = RE::ControlMap::GetSingleton();
		if (!map)
		{
			static bool s_said = false;
			if (!s_said) { s_said = true; logger::warn("rows: the ControlMap singleton is null; the page will be empty until it exists"); }
			std::scoped_lock l(g_lock);
			g_rows = rows;
			return;
		}
		std::vector<Entry> entries;
		{
			std::scoped_lock l(g_lock);
			entries = g_entries;
		}
		auto unboundEntry = [&](int a_ctx, std::string_view a_event, int a_dev) -> const Entry* {
			const char* name = ContextName(a_ctx);
			for (const auto& e : entries) { if (e.device == a_dev && IEquals(e.context, name) && IEquals(e.event, a_event)) { return &e; } }
			return nullptr;
		};
		for (int c = 0; c < ContextCount(); ++c)
		{
			auto* ctx = Context(map, c);
			if (!ctx) { continue; }
			struct Pending { Row row; int order; };
			std::vector<Pending> pending;
			for (int d = 0; d < 3; ++d)
			{
				for (auto& m : ctx->deviceMappings[d])
				{
					if (!m.eventID.c_str() || !m.eventID.c_str()[0]) { continue; }
					const std::string ev = m.eventID.c_str();
					auto it = std::find_if(pending.begin(), pending.end(), [&](const Pending& p) { return IEquals(p.row.event, ev); });
					if (it == pending.end())
					{
						Pending p;
						p.row.context = c;
						p.row.event = ev;
						const std::string t = Translate("$" + ev);
						p.row.label = t.empty() ? ev : t;
						p.row.warn = (c == 0) && (IEquals(ev, "Pause") || IEquals(ev, "Journal") || IEquals(ev, "Console"));
						p.order = m.indexInContext;
						pending.push_back(std::move(p));
						it = pending.end() - 1;
					}
					Cell& cell = it->row.cell[d];
					const Entry* e = unboundEntry(c, ev, d);
					if (e)
					{
						cell.exists = true;
						cell.unbound = true;
						cell.key = kUnmapped;
						cell.keyName = OriginalNames(*e);
					}
					else if (m.inputKey != kUnmapped)
					{
						// Several mappings of one event on one device (Hotkey1 = 2, 0x4f): list them all.
						const std::string n = ButtonName(d, m.inputKey);
						cell.exists = true;
						cell.key = cell.keyName.empty() ? m.inputKey : cell.key;
						cell.keyName = cell.keyName.empty() ? n : cell.keyName + ", " + n;
					}
				}
			}
			std::stable_sort(pending.begin(), pending.end(), [](const Pending& a, const Pending& b) { return a.order < b.order; });
			for (auto& p : pending) { rows->push_back(std::move(p.row)); }
		}
		{
			std::scoped_lock l(g_lock);
			g_rows = rows;
		}
		logger::debug("rows: {} controls across {} contexts", rows->size(), ContextCount());
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

	std::shared_ptr<const std::vector<Row>> GetRows()
	{
		std::scoped_lock l(g_lock);
		return g_rows;
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
						out += std::format(R"({{"event":"{}","key":"{}","modifier":"{}","index":{},"remappable":{},"linked":{},"flags":"0x{:x}"}})",
										   EscapeJson(m.eventID.c_str() ? m.eventID.c_str() : ""), Hex(m.inputKey), Hex(m.modifier), static_cast<int>(m.indexInContext),
										   m.remappable ? "true" : "false", m.linked ? "true" : "false", m.userEventGroupFlag.underlying());
					}
					out += "]";
				}
			}
			out += "}";
		}
		out += "]}";
		return out;
	}

	std::string RowsJson(int a_context)
	{
		auto rows = GetRows();
		std::string out = R"({"ok":true,"op":"rows","rows":[)";
		bool first = true;
		for (const auto& r : *rows)
		{
			if (a_context >= 0 && r.context != a_context) { continue; }
			if (!first) { out += ","; }
			first = false;
			out += std::format(R"({{"context":"{}","event":"{}","label":"{}","warn":{})", ContextName(r.context), EscapeJson(r.event), EscapeJson(r.label), r.warn ? "true" : "false");
			for (int d = 0; d < 3; ++d)
			{
				const Cell& c = r.cell[d];
				out += std::format(R"(,"{}":{{"exists":{},"unbound":{},"key":"{}","name":"{}"}})", kDevices[d], c.exists ? "true" : "false", c.unbound ? "true" : "false", Hex(c.key), EscapeJson(c.keyName));
			}
			out += "}";
		}
		out += "]}";
		return out;
	}

	std::string StateJson()
	{
		std::vector<Entry> entries;
		std::string lastApply;
		int applyCount = 0, lastTouched = 0;
		std::size_t rowCount = 0;
		{
			std::scoped_lock l(g_lock);
			entries = g_entries;
			lastApply = g_lastApply;
			applyCount = g_applyCount;
			lastTouched = g_lastTouched;
			rowCount = g_rows->size();
		}
		std::string out = std::format(R"("unbinder":{{"contextCount":{},"rows":{},"lastApply":"{}","applyCount":{},"lastTouched":{},"sink":{},"entries":[)",
									  ContextCount(), rowCount, EscapeJson(lastApply), applyCount, lastTouched, g_sinkInstalled ? "true" : "false");
		bool first = true;
		for (const auto& e : entries)
		{
			if (!first) { out += ","; }
			first = false;
			out += std::format(R"({{"context":"{}","event":"{}","device":"{}","original":[)", EscapeJson(e.context), EscapeJson(e.event), DeviceName(e.device));
			bool f2 = true;
			for (const auto& k : e.original) { if (!f2) { out += ","; } f2 = false; out += std::format(R"({{"key":"{}","modifier":"{}"}})", Hex(k.key), Hex(k.modifier)); }
			out += "]}";
		}
		out += "]}";
		return out;
	}
}
