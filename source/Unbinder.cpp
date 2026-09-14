#include "PCH.h"

#include "Unbinder.h"

#include "ControlsList.h"
#include "Settings.h"
#include "utils/Logger.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <map>
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
		std::string g_lastApply = "never";
		int g_applyCount = 0;
		int g_lastTouched = 0;
		bool g_sinkInstalled = false;

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

		// The engine's button -> event lookup is a binary search on inputKey (CommonLibSSE's
		// ControlMap::GetUserEventName mirrors it with std::equal_range), so the array must be sorted by key
		// again after any edit.
		void SortByKey(Mappings& a_mappings)
		{
			if (a_mappings.size() < 2) { return; }
			std::stable_sort(a_mappings.begin(), a_mappings.end(),
							 [](const RE::ControlMap::UserEventMapping& a_l, const RE::ControlMap::UserEventMapping& a_r) { return a_l.inputKey < a_r.inputKey; });
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

		int g_lastLinked = 0;  // menu actions given a key at the last apply

		// A menu action controlmap.txt links to a gameplay control (the inventory's "ChargeItem !0,Wait") takes that
		// control's key whenever the game resolves its links, so unbinding the control empties the action too - the
		// owner chose to keep such actions working. Every action linked to a control in the list gets the control's
		// default key back on that device. Caller holds g_lock.
		int KeepLinkedActions(RE::ControlMap* a_map, const char* a_reason)
		{
			auto& links = a_map->GetRuntimeData().linkedMappings;
			static bool s_loggedCount = false;
			if (!s_loggedCount)
			{
				s_loggedCount = true;
				logger::info("links: the control map holds {} linked menu action(s)", links.size());
			}
			int kept = 0;
			for (const auto& e : g_entries)
			{
				const int ctx = ContextIndex(e.context);
				if (ctx != 0 || e.device < 0 || e.device > 2) { continue; }  // controlmap.txt defaults are read for Gameplay only
				const auto& defaults = GameplayDefaults();
				const auto it = defaults.find(Lower(e.event));
				if (it == defaults.end()) { continue; }
				const std::uint16_t key = it->second.key[e.device];
				if (key == kUnmapped) { continue; }
				for (const auto& link : links)
				{
					if (static_cast<int>(link.linkFromContext) != ctx || static_cast<int>(link.device) != e.device) { continue; }
					if (!link.linkFromName.c_str() || !IEquals(link.linkFromName.c_str(), e.event)) { continue; }
					auto* target = MappingsFor(a_map, static_cast<int>(link.linkedMappingContext), e.device);
					if (!target || !link.linkedMappingName.c_str()) { continue; }
					bool changed = false;
					for (auto* m : Find(*target, link.linkedMappingName.c_str()))
					{
						if (m->inputKey != key)
						{
							m->inputKey = key;
							changed = true;
							++kept;
						}
					}
					if (changed)
					{
						SortByKey(*target);
						logger::debug("apply ({}): {}|{}|{} is linked to {}, which is unbound - given {}", a_reason, ContextName(static_cast<int>(link.linkedMappingContext)),
									  link.linkedMappingName.c_str(), DeviceName(e.device), e.event, Hex(key));
					}
				}
			}
			return kept;
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
					return RE::BSEventNotifyControl::kContinue;
				}
				controlslist::OnJournalClose();
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
		logger::debug("unbinder: list set, {} entries", g_entries.size());
	}

	std::vector<Entry> DefaultEntries()
	{
		// The Tween Menu set (the owner, 2026-09-14: "only unbind the tween menu overhaul coverd buttons and keys and
		// leave the rest alone"): the shortcuts for screens Tween Menu Overhaul and its Wait add-on already open. On the
		// controller only Back (Wait) is freed; Start stays on Journal, the controller's route to the system menu.
		// Device: 0 keyboard, 2 gamepad.
		constexpr std::pair<const char*, int> kDefaults[] = {
			{ "Journal", 0 },
			{ "Quick Inventory", 0 },
			{ "Quick Magic", 0 },
			{ "Quick Map", 0 },
			{ "Quick Stats", 0 },
			{ "Wait", 0 },
			{ "Wait", 2 },
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
		g_lastLinked = KeepLinkedActions(map, a_reason);
		if (g_lastLinked) { logger::info("apply ({}): {} menu action(s) linked to an unbound control given their key", a_reason, g_lastLinked); }
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
		std::string out = std::format(R"("unbinder":{{"contextCount":{},"lastApply":"{}","applyCount":{},"lastTouched":{},"linkedKept":{},"sink":{},"entries":[)",
									  ContextCount(), EscapeJson(g_lastApply), g_applyCount, g_lastTouched, g_lastLinked, g_sinkInstalled ? "true" : "false");
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
		out += "]}";
		return out;
	}
}
