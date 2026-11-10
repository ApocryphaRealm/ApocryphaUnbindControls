// Unbind Vanilla Controls 1.6.0 - controller buttons that press keyboard keys ([PadToKey]). See PadToKey.h.
#include "PCH.h"

#include "PadToKey.h"

#include "Unbinder.h"
#include "utils/Logger.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <format>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace padtokey
{
	namespace
	{
		constexpr std::uint16_t kNone = 0xFF;

		struct PadMap
		{
			std::uint16_t button = kNone;
			std::uint16_t modifier = kNone;  // kNone: no modifier
			bool designatedModifier = false;  // "Modifier": the [Modifier] row's controller button, asked at each press
			std::uint16_t key = kNone;        // DirectInput scan code
			std::string text;                 // the INI line, for the log and the DevBench state
			bool active = false;              // main thread only: the key is down because of this line
		};

		// Read at load (and by the DevBench reload, which runs on the main thread like the hook).
		std::vector<PadMap> g_maps;

		std::atomic<int> g_synthSent{ 0 };
		std::atomic<bool> g_dispatchHook{ false };

		// Main thread only (the dispatch hook): controller buttons down right now (as the device reports).
		std::set<std::uint32_t> g_padHeld;

		std::string Lower(std::string a_s)
		{
			for (char& c : a_s) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
			return a_s;
		}

		std::string Trim(std::string_view a_s)
		{
			const auto b = a_s.find_first_not_of(" \t\r\n");
			if (b == std::string_view::npos) { return {}; }
			const auto e = a_s.find_last_not_of(" \t\r\n");
			return std::string(a_s.substr(b, e - b + 1));
		}

		std::vector<std::string> Split(std::string_view a_s, char a_sep)
		{
			std::vector<std::string> out;
			std::size_t from = 0;
			while (true)
			{
				const auto at = a_s.find(a_sep, from);
				out.push_back(Trim(a_s.substr(from, at == std::string_view::npos ? std::string_view::npos : at - from)));
				if (at == std::string_view::npos) { break; }
				from = at + 1;
			}
			return out;
		}

		std::string KeyText(std::uint16_t a_code)
		{
			const char* n = unbinder::ButtonName(a_code, 0);
			return n[0] ? std::string(n) : std::format("0x{:02x}", a_code);
		}

		// ---- controller -> key -------------------------------------------------------------------------------------------
		struct Synth
		{
			std::uint16_t key;
			float value;
			float held;
		};

		std::uint16_t ModifierOf(const PadMap& a_map)
		{
			return a_map.designatedModifier ? unbinder::ModifierFor(2) : a_map.modifier;
		}

		// True when the event belongs to a [PadToKey] line (it is then taken out of the list; the key is sent instead).
		bool HandlePadToKey(const RE::ButtonEvent& a_event, std::vector<Synth>& a_out)
		{
			const std::uint32_t code = a_event.GetIDCode();
			const bool pressed = a_event.IsPressed();
			const float held = a_event.HeldDuration();
			for (auto& m : g_maps)
			{
				if (!m.active || m.button != code) { continue; }
				if (pressed) { a_out.push_back({ m.key, 1.0F, held }); }
				else
				{
					a_out.push_back({ m.key, 0.0F, held > 0.0F ? held : 0.016F });
					m.active = false;
				}
				return true;
			}
			if (!a_event.IsDown()) { return false; }
			PadMap* plain = nullptr;
			PadMap* withModifier = nullptr;
			for (auto& m : g_maps)
			{
				if (m.button != code) { continue; }
				const std::uint16_t mod = ModifierOf(m);
				if (mod == kNone) { if (!plain) { plain = &m; } }
				else if (!withModifier && g_padHeld.contains(mod)) { withModifier = &m; }
			}
			PadMap* chosen = withModifier ? withModifier : plain;
			if (!chosen) { return false; }
			chosen->active = true;
			a_out.push_back({ chosen->key, 1.0F, 0.0F });
			logger::debug("pad to key: {} -> {}", chosen->text, KeyText(chosen->key));
			return true;
		}

		RE::InputEvent* MakeKey(const Synth& a_synth)
		{
			std::string_view name;
			if (auto* map = RE::ControlMap::GetSingleton())
			{
				auto& stack = map->GetRuntimeData().contextPriorityStack;
				const auto context = stack.empty() ? RE::UserEvents::INPUT_CONTEXT_ID::kGameplay : stack.back();
				name = map->GetUserEventName(a_synth.key, RE::INPUT_DEVICE::kKeyboard, context);
			}
			const RE::BSFixedString userEvent(std::string(name).c_str());
			return RE::ButtonEvent::Create(RE::INPUT_DEVICE::kKeyboard, userEvent, a_synth.key, a_synth.value, a_synth.held);
		}

		// ---- the dispatch hook -------------------------------------------------------------------------------------------
		void Process(RE::InputEvent** a_events)
		{
			if (g_maps.empty()) { return; }

			std::vector<Synth> synth;
			RE::InputEvent* kept = nullptr;
			RE::InputEvent** tail = &kept;
			for (RE::InputEvent* e = *a_events; e;)
			{
				RE::InputEvent* const next = e->next;
				bool drop = false;
				if (auto* button = e->AsButtonEvent(); button && e->GetDevice() == RE::INPUT_DEVICE::kGamepad)
				{
					const std::uint32_t code = button->GetIDCode();
					if (button->IsPressed()) { g_padHeld.insert(code); }
					else { g_padHeld.erase(code); }
					drop = HandlePadToKey(*button, synth);
				}
				if (!drop)
				{
					*tail = e;
					tail = &e->next;
				}
				e = next;
			}
			*tail = nullptr;

			// The keys, in order, in front of what the device sent this frame.
			RE::InputEvent* head = kept;
			for (auto it = synth.rbegin(); it != synth.rend(); ++it)
			{
				if (auto* ev = MakeKey(*it))
				{
					ev->next = head;
					head = ev;
					g_synthSent.fetch_add(1, std::memory_order_relaxed);
				}
			}
			*a_events = head;  // written back even when empty: a consumed node must not stay the head (past mistakes 5062)
		}

		struct DispatchHook
		{
			static inline REL::Relocation<void(RE::BSTEventSource<RE::InputEvent*>*, RE::InputEvent**)> func;

			static void thunk(RE::BSTEventSource<RE::InputEvent*>* a_dispatcher, RE::InputEvent** a_events)
			{
				if (a_events) { Process(a_events); }
				func(a_dispatcher, a_events);
			}
		};
	}

	void Load(const std::string& a_iniPath)
	{
		std::vector<PadMap> maps;
		std::ifstream in(a_iniPath);
		std::string line, section;
		while (in && std::getline(in, line))
		{
			const std::string t = Trim(line);
			if (t.empty() || t[0] == ';' || t[0] == '#') { continue; }
			if (t.size() >= 2 && t.front() == '[' && t.back() == ']')
			{
				section = Lower(t.substr(1, t.size() - 2));
				continue;
			}
			if (section != "padtokey") { continue; }
			// gamepad|Button[|Modifier] -> keyboard|Key
			const auto arrow = t.find("->");
			const auto left = arrow == std::string::npos ? std::vector<std::string>{} : Split(std::string_view(t).substr(0, arrow), '|');
			const auto right = arrow == std::string::npos ? std::vector<std::string>{} : Split(std::string_view(t).substr(arrow + 2), '|');
			if (left.size() < 2 || left.size() > 3 || right.size() != 2 || Lower(left[0]) != "gamepad" || Lower(right[0]) != "keyboard")
			{
				logger::warn("[PadToKey] line \"{}\" is not gamepad|Button[|Modifier] -> keyboard|Key; ignored", t);
				continue;
			}
			PadMap m;
			m.text = t;
			m.button = unbinder::ParseButton(left[1], 2);
			if (left.size() == 3 && !left[2].empty())
			{
				if (Lower(left[2]) == Lower(unbinder::kModifierRowName)) { m.designatedModifier = true; }
				else { m.modifier = unbinder::ParseButton(left[2], 2); if (m.modifier == kNone) { logger::warn("[PadToKey] line \"{}\": \"{}\" is not a controller button; ignored", t, left[2]); continue; } }
			}
			m.key = unbinder::ParseButton(right[1], 0);
			if (m.button == kNone) { logger::warn("[PadToKey] line \"{}\": \"{}\" is not a controller button; ignored", t, left[1]); continue; }
			if (m.key == kNone || m.key == 0) { logger::warn("[PadToKey] line \"{}\": \"{}\" is not a keyboard key; ignored", t, right[1]); continue; }
			const bool dup = std::any_of(maps.begin(), maps.end(), [&](const PadMap& o) { return o.button == m.button && o.modifier == m.modifier && o.designatedModifier == m.designatedModifier; });
			if (dup) { logger::warn("[PadToKey] line \"{}\" uses the same button as an earlier line; ignored", t); continue; }
			maps.push_back(std::move(m));
		}
		g_maps = std::move(maps);
		logger::info("pad to key: {} [PadToKey] line(s)", g_maps.size());
		for (const auto& m : g_maps) { logger::info("pad to key: {}", m.text); }
	}

	void Install()
	{
		// BSInputDeviceManager::PollInputDevices, the call that hands the frame's input to every sink (SE 67315 / AE 68617
		// + 0x7B; the site the Apocrypha framework and InputBench hook, guarded the same way).
		constexpr REL::RelocationID kPollID{ 67315, 68617 };
		const std::uintptr_t site = kPollID.address() + 0x7B;
		if (!REL::make_pattern<"E8">().match(site))
		{
			logger::error("pad to key: the input dispatch at 0x{:X} is not a call; [PadToKey] is off", site - REL::Module::get().base());
			return;
		}
		DispatchHook::func = SKSE::GetTrampoline().write_call<5>(site, DispatchHook::thunk);
		g_dispatchHook = true;
		logger::info("pad to key: input dispatch hooked (0x{:X})", site - REL::Module::get().base());
	}

	std::string StateJson()
	{
		std::string maps;
		for (const auto& m : g_maps)
		{
			std::string text = m.text;
			for (auto& c : text) { if (c == '"' || c == '\\') { c = '\''; } }
			maps += std::format("{}{{\"line\":\"{}\",\"key\":{},\"active\":{}}}", maps.empty() ? "" : ",", text, m.key, m.active ? "true" : "false");
		}
		return std::format("\"padToKey\":{{\"dispatchHook\":{},\"keysSent\":{},\"lines\":[{}]}}", g_dispatchHook.load() ? "true" : "false", g_synthSent.load(), maps);
	}
}
