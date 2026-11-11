// Unbind Vanilla Controls 1.6.0 - the Controls page's section stays put until the player switches it. See DevicePin.h.
#include "PCH.h"

#include "DevicePin.h"

#include "utils/Logger.h"

#include <atomic>
#include <format>
#include <string>

namespace devicepin
{
	namespace
	{
		using Sink = RE::BSTEventSink<RE::InputEvent*>;

		Sink* g_ais = nullptr;              // Auto Input Switch's input sink (its InputEventHandler), found by its vtable's module
		std::uintptr_t g_aisBegin = 0;
		std::uintptr_t g_aisEnd = 0;
		bool g_aisLooked = false;
		std::atomic<bool> g_onPage{ false };
		std::atomic<bool> g_standing{ false };  // Auto Input Switch's sink is off the input source while the page is open
		std::atomic<int> g_toggles{ 0 };
		std::string g_status = "not looked for yet (the Controls page has not opened)";
		std::uintptr_t g_check = 0;          // the game's using-controller check (BSInputDeviceManager), called, never patched

		// One event per device, made once with the game's own vtable (Auto Input Switch 1.1.x runs skyrim_cast on it) and kept.
		RE::ButtonEvent* g_keyboardEvent = nullptr;
		RE::ButtonEvent* g_gamepadEvent = nullptr;

		bool AisModule()
		{
			const auto module = reinterpret_cast<std::uintptr_t>(REX::W32::GetModuleHandleA("AutoInputSwitch.dll"));
			if (!module) { return false; }
			// PE32+: e_lfanew at 0x3C; SizeOfImage at NT headers + 0x50
			const auto nt = module + *reinterpret_cast<const std::uint32_t*>(module + 0x3C);
			const auto size = *reinterpret_cast<const std::uint32_t*>(nt + 0x50);
			g_aisBegin = module;
			g_aisEnd = module + size;
			return size != 0;
		}

		// The sink of Auto Input Switch's on the game's input source: the one whose vtable lives in AutoInputSwitch.dll.
		Sink* FindAisSink(RE::BSInputDeviceManager* a_manager)
		{
			RE::BSSpinLockGuard locker(a_manager->lock);
			for (auto* sink : a_manager->sinks)
			{
				if (!sink) { continue; }
				const auto vtbl = *reinterpret_cast<const std::uintptr_t*>(sink);
				if (vtbl >= g_aisBegin && vtbl < g_aisEnd) { return sink; }
			}
			return nullptr;
		}

		bool Look(RE::BSInputDeviceManager* a_manager)
		{
			if (g_ais) { return true; }
			if (!g_aisLooked)
			{
				g_aisLooked = true;
				if (!AisModule())
				{
					g_status = "Auto Input Switch is not loaded - the page follows the game's device, as the game itself never switches it on a key";
					logger::info("controls page: {}", g_status);
					return false;
				}
			}
			if (!g_aisEnd) { return false; }
			g_ais = FindAisSink(a_manager);
			if (!g_ais)
			{
				g_status = "Auto Input Switch is loaded but its input sink is not on the game's input source - nothing is changed";
				logger::warn("controls page: {}", g_status);
				return false;
			}
			g_status = std::format("Auto Input Switch's input sink found (AutoInputSwitch.dll+0x{:X})",
								   reinterpret_cast<std::uintptr_t>(g_ais) - g_aisBegin);
			logger::info("controls page: {}", g_status);
			return true;
		}

		bool AskGame()
		{
			auto* manager = RE::BSInputDeviceManager::GetSingleton();
			if (!manager || !g_check) { return false; }
			return reinterpret_cast<bool (*)(RE::BSInputDeviceManager*)>(g_check)(manager);
		}
	}

	void Install()
	{
		// Nothing is written into the game: only the address of the using-controller check, to ask which section is shown.
		std::uint64_t id = 0;
		if (!REL::Module::IsAE()) { id = 67320; }
		else if (REL::Module::get().version().compare(SKSE::RUNTIME_SSE_1_6_1130) == std::strong_ordering::less) { id = 68622; }
		else { id = 443396; }
		g_check = REL::ID(id).address();
		g_keyboardEvent = RE::ButtonEvent::Create(RE::INPUT_DEVICE::kKeyboard, RE::BSFixedString{}, 0, 0.0F, 0.0F);
		g_gamepadEvent = RE::ButtonEvent::Create(RE::INPUT_DEVICE::kGamepad, RE::BSFixedString{}, 0, 0.0F, 0.0F);
		logger::info("controls page: section switch ready (no engine hook); Auto Input Switch {} - it stands down only while UVC's Controls page is open",
					 REX::W32::GetModuleHandleA("AutoInputSwitch.dll") ? "is loaded" : "is not loaded");
	}

	void OnPage(bool a_open)
	{
		if (a_open == g_onPage.load()) { return; }
		g_onPage = a_open;
		auto* manager = RE::BSInputDeviceManager::GetSingleton();
		if (!manager) { return; }
		if (a_open)
		{
			if (!Look(manager)) { return; }
			manager->RemoveEventSink(g_ais);
			g_standing = true;
			logger::info("controls page: opened on the {} section; Auto Input Switch stands down until the page closes - Square or the page's switch changes the section",
						 AskGame() ? "controller" : "keyboard and mouse");
		}
		else if (g_standing.exchange(false))
		{
			manager->AddEventSink(g_ais);
			logger::info("controls page: closed; Auto Input Switch switches as normal again ({} now)", AskGame() ? "controller" : "keyboard and mouse");
		}
	}

	bool Toggle(const char* a_why)
	{
		if (!g_standing.load() || !g_ais) { return false; }
		auto* manager = RE::BSInputDeviceManager::GetSingleton();
		if (!manager) { return false; }
		const bool wasPad = AskGame();
		RE::ButtonEvent* event = wasPad ? g_keyboardEvent : g_gamepadEvent;
		if (!event) { return false; }
		// Auto Input Switch is handed one event of the other device - only it, never the game - so it switches and refreshes the menus.
		RE::InputEvent* list = event;
		g_ais->ProcessEvent(&list, manager);
		const bool nowPad = AskGame();
		if (nowPad == wasPad)
		{
			logger::info("controls page: {} - Auto Input Switch kept the {} section (its platform setting forces one device)", a_why, wasPad ? "controller" : "keyboard and mouse");
			return false;
		}
		g_toggles.fetch_add(1);
		logger::info("controls page: the {} section ({})", nowPad ? "controller" : "keyboard and mouse", a_why);
		return true;
	}

	bool Available() { return g_ais != nullptr; }
	bool Pinned() { return g_standing.load(); }

	std::string StateJson()
	{
		return std::format("\"devicePin\":{{\"engineHook\":false,\"autoInputSwitch\":{},\"status\":\"{}\",\"onPage\":{},\"standingDown\":{},\"section\":\"{}\",\"toggles\":{}}}",
						   g_ais ? "true" : "false", g_status, g_onPage.load() ? "true" : "false", g_standing.load() ? "true" : "false",
						   AskGame() ? "gamepad" : "keyboard", g_toggles.load());
	}
}
