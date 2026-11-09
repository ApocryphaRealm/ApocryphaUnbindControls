// Unbind Vanilla Controls 1.7.0 - the input-mode lock ([InputLock]) and controller-to-key lines ([PadToKey]). See InputLock.h.
#include "PCH.h"

#include "InputLock.h"

#include "Unbinder.h"
#include "layout/LayoutStrings.h"
#include "utils/Logger.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <format>
#include <fstream>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace inputlock
{
	namespace
	{
		constexpr std::uint16_t kNone = 0xFF;

		enum Mode : int { kModeGamepad = 0, kModeKeyboard = 1 };

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
		bool g_enabled = false;
		bool g_ignoreOther = false;
		bool g_notify = true;
		std::vector<std::uint16_t> g_padToggle;
		float g_padToggleSeconds = 1.0F;
		std::uint16_t g_keyToggle = kNone;
		std::vector<PadMap> g_maps;
		std::string g_iniPath;

		std::atomic<int> g_mode{ kModeGamepad };
		std::atomic<int> g_toggles{ 0 };
		std::atomic<int> g_synthSent{ 0 };
		std::atomic<int> g_dropped{ 0 };
		std::atomic<bool> g_dispatchHook{ false };
		std::atomic<bool> g_usingHook{ false };
		std::atomic<bool> g_cursorHook{ false };
		std::atomic<int> g_usingAsked{ 0 };

		// Main thread only (the dispatch hook).
		std::set<std::uint32_t> g_padHeld;                          // controller buttons down right now (as the device reports)
		std::set<std::pair<int, std::uint32_t>> g_passedHeld;       // buttons whose press reached the game and that are still down
		bool g_comboFired = false;
		bool g_comboTiming = false;
		std::chrono::steady_clock::time_point g_comboStart{};

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

		const char* ModeName(int a_mode) { return a_mode == kModeKeyboard ? "Keyboard" : "Gamepad"; }

		std::string PadText(std::uint16_t a_code)
		{
			const char* n = unbinder::ButtonName(a_code, 2);
			return n[0] ? std::string(n) : std::format("0x{:04x}", a_code);
		}

		std::string KeyText(std::uint16_t a_code)
		{
			const char* n = unbinder::ButtonName(a_code, 0);
			return n[0] ? std::string(n) : std::format("0x{:02x}", a_code);
		}

		// ---- the using-gamepad answer -------------------------------------------------------------------------------------
		// BSInputDeviceManager's "is the controller in use" check (SE 67320, AE 68622 / 443396 from 1.6.1130) is what the
		// button art, the cursor and every menu's RefreshPlatform ask; its sibling (SE 67321, AE 68624) decides whether the
		// controller drives the cursor. Vanilla answers "a controller is connected and enabled"; Auto Input Switch replaces the
		// inner call with "the device last touched". With the lock on, both are answered here, at the functions' entries, so
		// the answer is the chosen mode whatever else patched their insides.
		bool PadConnected()
		{
			auto* manager = RE::BSInputDeviceManager::GetSingleton();
			auto* handler = manager ? manager->GetGamepadHandler() : nullptr;
			return handler && handler->IsEnabled();
		}

		bool UsingGamepadAnswer()
		{
			g_usingAsked.fetch_add(1, std::memory_order_relaxed);
			if (g_enabled && g_mode.load(std::memory_order_relaxed) == kModeKeyboard) { return false; }
			return PadConnected();
		}

		struct EntrySite
		{
			std::uint64_t id = 0;
			std::size_t guardOffset = 0;  // where the vanilla body reads the device's IsEnabled: "48 8B 01 FF 50 38"
		};

		bool IsAtLeast(const REL::Version& a_v, const REL::Version& a_min) { return a_v.compare(a_min) != std::strong_ordering::less; }

		// Address Library IDs: the SE ones from Auto Input Switch's history (MIT, Parapets; SE 1.5.97 offsets 0xC15240 and
		// 0xC15280, checked against version-1-5-97-0.bin on 2026-11-09), the AE ones from its current source.
		void PickSites(EntrySite& a_using, EntrySite& a_cursor)
		{
			const auto version = REL::Module::get().version();
			if (!REL::Module::IsAE())
			{
				a_using = { 67320, 0xD };
				a_cursor = { 67321, 0xD };
				return;
			}
			if (!IsAtLeast(version, SKSE::RUNTIME_SSE_1_6_1130))
			{
				a_using = { 68622, 0xD };
				a_cursor = { 68624, 0xD };
				return;
			}
			a_using = { 443396, 0x20 };  // 1.6.1130 and later (Steam Deck check in front of the same call)
			if (version.minor() < 7) { a_cursor = { 68624, 0xD }; }
			// 1.7: no verified ID for the cursor check; the cursor keeps the game's own answer.
		}

		bool GuardMatches(std::uintptr_t a_at)
		{
			// The vanilla call, or the six-byte indirect call Auto Input Switch writes over it.
			return REL::make_pattern<"48 8B 01 FF 50 38">().match(a_at) || REL::make_pattern<"FF 15">().match(a_at);
		}

		bool HookEntry(const EntrySite& a_site, const char* a_what)
		{
			if (a_site.id == 0)
			{
				logger::info("input lock: {} check left to the game on this runtime (no verified address)", a_what);
				return false;
			}
			const std::uintptr_t entry = REL::ID(a_site.id).address();
			if (!GuardMatches(entry + a_site.guardOffset))
			{
				logger::error("input lock: {} check at ID {} (0x{:X}) does not look as expected (+0x{:X}); NOT hooked - the game keeps switching on its own",
							  a_what, a_site.id, entry - REL::Module::get().base(), a_site.guardOffset);
				return false;
			}
			const auto first = *reinterpret_cast<const std::uint8_t*>(entry);
			if (first == 0xE9 || first == 0xFF)
			{
				logger::warn("input lock: the {} check at ID {} already starts with a jump (another mod hooks its entry); answered here instead", a_what, a_site.id);
			}
			SKSE::GetTrampoline().write_branch<5>(entry, &UsingGamepadAnswer);
			logger::info("input lock: {} check answered by this mod (ID {}, 0x{:X})", a_what, a_site.id, entry - REL::Module::get().base());
			return true;
		}

		// ---- the toggle ---------------------------------------------------------------------------------------------------
		void RefreshMenus()
		{
			auto* ui = RE::UI::GetSingleton();
			if (!ui) { return; }
			for (auto& menu : ui->menuStack)
			{
				if (menu) { menu->RefreshPlatform(); }
			}
		}

		// The Mode line in [InputLock], rewritten in place (main thread). The new text is built before the file is opened for
		// writing, so a failure leaves the file as it was.
		void SaveMode(int a_mode)
		{
			if (g_iniPath.empty()) { return; }
			std::vector<std::string> lines;
			{
				std::ifstream in(g_iniPath);
				if (!in) { logger::warn("input lock: could not read {} to keep the mode", g_iniPath); return; }
				std::string line;
				while (std::getline(in, line)) { lines.push_back(line); }
			}
			const std::string value = std::string("Mode=") + ModeName(a_mode);
			std::size_t header = lines.size();
			bool written = false;
			std::string section;
			for (std::size_t i = 0; i < lines.size(); ++i)
			{
				const std::string t = Trim(lines[i]);
				if (t.size() >= 2 && t.front() == '[' && t.back() == ']')
				{
					section = Lower(t.substr(1, t.size() - 2));
					if (section == "inputlock") { header = i; }
					continue;
				}
				if (section != "inputlock" || t.empty() || t[0] == ';') { continue; }
				const auto eq = t.find('=');
				if (eq != std::string::npos && Lower(Trim(t.substr(0, eq))) == "mode")
				{
					lines[i] = value;
					written = true;
					break;
				}
			}
			if (!written)
			{
				if (header == lines.size())
				{
					lines.push_back("");
					lines.push_back("[InputLock]");
					lines.push_back(value);
				}
				else { lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(header + 1), value); }
			}
			std::string text;
			for (const auto& l : lines) { text += l; text += '\n'; }
			std::ofstream out(g_iniPath, std::ios::binary | std::ios::trunc);
			if (!out) { logger::warn("input lock: could not write {} to keep the mode", g_iniPath); return; }
			out << text;
		}

		void SetMode(int a_mode, const char* a_why)
		{
			g_mode.store(a_mode);
			g_toggles.fetch_add(1);
			logger::info("input lock: {} ({})", a_mode == kModeKeyboard ? "keyboard and mouse" : "controller", a_why);
			if (auto* tasks = SKSE::GetTaskInterface())
			{
				tasks->AddUITask([]() { RefreshMenus(); });
				tasks->AddTask([a_mode]() {
					if (g_notify)
					{
						const std::string text = a_mode == kModeKeyboard ? TR("UVC_InputLockKeyboard", "Controls: keyboard and mouse") :
																		   TR("UVC_InputLockGamepad", "Controls: controller");
#if RUNTIME_LINE == 17
						RE::SendHUDMessage::ShowHUDMessage(text.c_str());  // the 1.7 CommonLib has no RE::DebugNotification
#else
						RE::DebugNotification(text.c_str());
#endif
					}
					SaveMode(a_mode);
				});
			}
		}

		bool IsOtherDevice(RE::INPUT_DEVICE a_device)
		{
			const bool pad = a_device == RE::INPUT_DEVICE::kGamepad;
			const bool kbm = a_device == RE::INPUT_DEVICE::kKeyboard || a_device == RE::INPUT_DEVICE::kMouse;
			return g_mode.load(std::memory_order_relaxed) == kModeGamepad ? kbm : pad;
		}

		bool IsComboButton(std::uint32_t a_code)
		{
			for (const auto b : g_padToggle) { if (b == a_code) { return true; } }
			return false;
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
			if (g_enabled)
			{
				// The game stops reading the keyboard and mouse when a controller connects (ControlMap's ignoreKeyboardMouse);
				// with a mode chosen by the player, keyboard and mouse are read in either mode.
				if (auto* map = RE::ControlMap::GetSingleton()) { map->GetRuntimeData().ignoreKeyboardMouse = false; }
			}
			if (!g_enabled && g_maps.empty()) { return; }

			bool toggle = false;
			std::vector<Synth> synth;
			RE::InputEvent* kept = nullptr;
			RE::InputEvent** tail = &kept;
			for (RE::InputEvent* e = *a_events; e;)
			{
				RE::InputEvent* const next = e->next;
				const RE::INPUT_DEVICE device = e->GetDevice();
				bool drop = false;
				if (auto* button = e->AsButtonEvent())
				{
					const std::uint32_t code = button->GetIDCode();
					const bool pressed = button->IsPressed();
					if (device == RE::INPUT_DEVICE::kGamepad)
					{
						if (pressed) { g_padHeld.insert(code); }
						else { g_padHeld.erase(code); }
					}
					const auto id = std::make_pair(static_cast<int>(device), code);
					if (g_enabled && device == RE::INPUT_DEVICE::kKeyboard && code == g_keyToggle)
					{
						if (button->IsDown()) { toggle = true; }
						drop = !g_passedHeld.contains(id) || pressed;
					}
					else if (g_enabled && device == RE::INPUT_DEVICE::kGamepad && g_comboFired && IsComboButton(code))
					{
						drop = true;  // after the toggle: these buttons do nothing more until let go
					}
					else if (device == RE::INPUT_DEVICE::kGamepad && !g_maps.empty() && HandlePadToKey(*button, synth))
					{
						drop = true;
					}
					else if (g_enabled && g_ignoreOther && IsOtherDevice(device))
					{
						// A release whose press reached the game before the toggle still goes through (no held key left behind).
						drop = pressed || !g_passedHeld.contains(id);
					}
					if (!drop)
					{
						if (pressed) { g_passedHeld.insert(id); }
						else { g_passedHeld.erase(id); }
					}
				}
				else if (g_enabled && g_ignoreOther && IsOtherDevice(device))
				{
					drop = true;  // mouse movement, stick movement, typed characters
				}
				if (drop) { g_dropped.fetch_add(1, std::memory_order_relaxed); }
				else
				{
					*tail = e;
					tail = &e->next;
				}
				e = next;
			}
			*tail = nullptr;

			// The controller toggle: every button of the combination held for the set time.
			if (g_enabled && !g_padToggle.empty())
			{
				bool all = true;
				bool any = false;
				for (const auto b : g_padToggle)
				{
					const bool held = g_padHeld.contains(b);
					all = all && held;
					any = any || held;
				}
				const auto now = std::chrono::steady_clock::now();
				if (all && !g_comboFired)
				{
					if (!g_comboTiming) { g_comboTiming = true; g_comboStart = now; }
					else if (std::chrono::duration<float>(now - g_comboStart).count() >= g_padToggleSeconds)
					{
						g_comboFired = true;
						g_comboTiming = false;
						toggle = true;
					}
				}
				else if (!all) { g_comboTiming = false; }
				if (!any) { g_comboFired = false; }
			}
			if (toggle) { SetMode(g_mode.load() == kModeGamepad ? kModeKeyboard : kModeGamepad, "toggle pressed"); }

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

		bool InstallDispatch()
		{
			// BSInputDeviceManager::PollInputDevices, the call that hands the frame's input to every sink (SE 67315 / AE 68617
			// + 0x7B; the site the Apocrypha framework and InputBench hook, guarded the same way).
			constexpr REL::RelocationID kPollID{ 67315, 68617 };
			const std::uintptr_t site = kPollID.address() + 0x7B;
			if (!REL::make_pattern<"E8">().match(site))
			{
				logger::error("input lock: the input dispatch at 0x{:X} is not a call; the lock and [PadToKey] are off", site - REL::Module::get().base());
				return false;
			}
			DispatchHook::func = SKSE::GetTrampoline().write_call<5>(site, DispatchHook::thunk);
			logger::info("input lock: input dispatch hooked (0x{:X})", site - REL::Module::get().base());
			return true;
		}
	}

	void Load(const std::string& a_iniPath)
	{
		g_iniPath = a_iniPath;
		bool enabled = false, ignoreOther = false, notify = true, sawSection = false;
		int mode = kModeGamepad;
		std::vector<std::uint16_t> padToggle;
		float seconds = 1.0F;
		std::uint16_t keyToggle = kNone;
		std::vector<PadMap> maps;
		std::string padToggleText = "Back+Start", keyToggleText = "F7";

		std::ifstream in(a_iniPath);
		std::string line, section;
		while (in && std::getline(in, line))
		{
			const std::string t = Trim(line);
			if (t.empty() || t[0] == ';' || t[0] == '#') { continue; }
			if (t.size() >= 2 && t.front() == '[' && t.back() == ']')
			{
				section = Lower(t.substr(1, t.size() - 2));
				if (section == "inputlock") { sawSection = true; }
				continue;
			}
			if (section == "inputlock")
			{
				const auto eq = t.find('=');
				if (eq == std::string::npos) { continue; }
				const std::string key = Lower(Trim(t.substr(0, eq)));
				const std::string value = Trim(t.substr(eq + 1));
				if (key == "benabled") { enabled = value == "1" || Lower(value) == "true"; }
				else if (key == "mode") { mode = Lower(value) == "keyboard" ? kModeKeyboard : kModeGamepad; }
				else if (key == "otherdevice") { ignoreOther = Lower(value) == "ignore"; }
				else if (key == "gamepadtoggle") { padToggleText = value; }
				else if (key == "fgamepadtoggleseconds") { try { seconds = std::stof(value); } catch (...) {} }
				else if (key == "keyboardtoggle") { keyToggleText = value; }
				else if (key == "bnotify") { notify = !(value == "0" || Lower(value) == "false"); }
				continue;
			}
			if (section == "padtokey")
			{
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
		}
		if (!sawSection) { enabled = false; }  // an INI from before 1.7.0: nothing changes until the section is there

		for (const auto& part : Split(padToggleText, '+'))
		{
			if (part.empty()) { continue; }
			const std::uint16_t b = unbinder::ParseButton(part, 2);
			if (b == kNone) { logger::warn("[InputLock] GamepadToggle: \"{}\" is not a controller button; no controller toggle", part); padToggle.clear(); break; }
			padToggle.push_back(b);
		}
		if (!keyToggleText.empty())
		{
			keyToggle = unbinder::ParseButton(keyToggleText, 0);
			if (keyToggle == 0) { keyToggle = kNone; }
			if (keyToggle == kNone) { logger::warn("[InputLock] KeyboardToggle: \"{}\" is not a key; no keyboard toggle", keyToggleText); }
		}
		if (seconds < 0.0F) { seconds = 0.0F; }
		if (seconds > 10.0F) { seconds = 10.0F; }

		g_enabled = enabled;
		g_ignoreOther = ignoreOther;
		g_notify = notify;
		g_padToggle = std::move(padToggle);
		g_padToggleSeconds = seconds;
		g_keyToggle = keyToggle;
		g_maps = std::move(maps);
		g_mode.store(mode);

		std::string combo;
		for (const auto b : g_padToggle) { combo += (combo.empty() ? "" : "+") + PadText(b); }
		logger::info("input lock: {}{}, mode {}, other device {}, controller toggle {} held {:.2f}s, keyboard toggle {}; [PadToKey] {} line(s)",
					 g_enabled ? "on" : "off", sawSection ? "" : " (no [InputLock] section)", ModeName(mode), g_ignoreOther ? "ignored" : "passed",
					 combo.empty() ? "none" : combo, g_padToggleSeconds, g_keyToggle == kNone ? "none" : KeyText(g_keyToggle), g_maps.size());
		for (const auto& m : g_maps) { logger::info("pad to key: {}", m.text); }
	}

	void Install()
	{
		const bool dispatch = InstallDispatch();
		g_dispatchHook = dispatch;
		if (!g_enabled) { return; }
		if (!dispatch) { return; }
		EntrySite usingSite, cursorSite;
		PickSites(usingSite, cursorSite);
		g_usingHook = HookEntry(usingSite, "using-controller");
		g_cursorHook = HookEntry(cursorSite, "controller-cursor");
		if (REX::W32::GetModuleHandleA("AutoInputSwitch.dll"))
		{
			logger::info("input lock: Auto Input Switch is loaded; its switching is overruled while [InputLock] bEnabled=1");
		}
	}

	void Toggle(const char* a_why)
	{
		SetMode(g_mode.load() == kModeGamepad ? kModeKeyboard : kModeGamepad, a_why);
	}

	std::string StateJson()
	{
		std::string combo;
		for (const auto b : g_padToggle) { combo += (combo.empty() ? "" : "+") + PadText(b); }
		std::string maps;
		for (const auto& m : g_maps)
		{
			std::string text = m.text;
			for (auto& c : text) { if (c == '"' || c == '\\') { c = '\''; } }
			maps += std::format("{}{{\"line\":\"{}\",\"key\":{},\"active\":{}}}", maps.empty() ? "" : ",", text, m.key, m.active ? "true" : "false");
		}
		return std::format(
			"\"inputLock\":{{\"enabled\":{},\"mode\":\"{}\",\"otherDevice\":\"{}\",\"gamepadToggle\":\"{}\",\"seconds\":{:.2f},\"keyboardToggle\":{},"
			"\"dispatchHook\":{},\"usingHook\":{},\"cursorHook\":{},\"usingAsked\":{},\"padConnected\":{},\"toggles\":{},\"dropped\":{},\"keysSent\":{},\"padToKey\":[{}]}}",
			g_enabled ? "true" : "false", ModeName(g_mode.load()), g_ignoreOther ? "Ignore" : "Pass", combo, g_padToggleSeconds,
			g_keyToggle == kNone ? -1 : static_cast<int>(g_keyToggle), g_dispatchHook.load() ? "true" : "false", g_usingHook.load() ? "true" : "false",
			g_cursorHook.load() ? "true" : "false", g_usingAsked.load(), PadConnected() ? "true" : "false", g_toggles.load(), g_dropped.load(), g_synthSent.load(), maps);
	}
}
