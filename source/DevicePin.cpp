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
		using Inner = bool (*)(void*);

		Inner g_inner = nullptr;          // what was at the site: Auto Input Switch's function, or nullptr for the vanilla vfunc
		std::atomic<bool> g_hooked{ false };
		std::atomic<bool> g_onPage{ false };
		std::atomic<bool> g_pinActive{ false };
		std::atomic<bool> g_pinnedGamepad{ false };
		std::atomic<int> g_answered{ 0 };  // questions answered by the pin
		std::atomic<int> g_toggles{ 0 };
		std::string g_siteText = "not hooked";
		std::uintptr_t g_check = 0;        // the using-gamepad check's entry (a member of BSInputDeviceManager)

		// The question as it was asked before: Auto Input Switch's answer, or the vanilla call (the gamepad device's IsEnabled,
		// vtable slot 7: "mov rax,[rcx]; call [rax+38h]").
		bool Unpinned(void* a_device)
		{
			if (g_inner) { return g_inner(a_device); }
			if (!a_device) { return false; }
			auto** vtbl = *reinterpret_cast<void***>(a_device);
			return reinterpret_cast<Inner>(vtbl[7])(a_device);
		}

		bool Answer(void* a_device)
		{
			if (g_pinActive.load(std::memory_order_relaxed))
			{
				g_answered.fetch_add(1, std::memory_order_relaxed);
				return g_pinnedGamepad.load(std::memory_order_relaxed);
			}
			return Unpinned(a_device);
		}

		void RefreshMenus()
		{
			if (auto* tasks = SKSE::GetTaskInterface())
			{
				tasks->AddUITask([]() {
					auto* ui = RE::UI::GetSingleton();
					if (!ui) { return; }
					for (auto& menu : ui->menuStack)
					{
						if (menu) { menu->RefreshPlatform(); }
					}
				});
			}
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
		// The call inside BSInputDeviceManager's using-gamepad check; the same sites Auto Input Switch hooks (its source, MIT).
		std::uintptr_t site = 0;
		std::uint64_t id = 0;
		std::size_t offset = 0;
		if (!REL::Module::IsAE()) { id = 67320; offset = 0xD; }
		else if (REL::Module::get().version().compare(SKSE::RUNTIME_SSE_1_6_1130) == std::strong_ordering::less) { id = 68622; offset = 0xD; }
		else { id = 443396; offset = 0x20; }  // 1.6.1130 and later: a Steam Deck check in front of the same call
		g_check = REL::ID(id).address();
		site = g_check + offset;
		const auto rva = site - REL::Module::get().base();
		auto& trampoline = SKSE::GetTrampoline();
		if (REL::make_pattern<"FF 15">().match(site))
		{
			g_inner = reinterpret_cast<Inner>(trampoline.write_call<6>(site, &Answer));
			g_siteText = std::format("ID {} +0x{:X} (0x{:X}), after another mod's hook (Auto Input Switch's shape)", id, offset, rva);
		}
		else if (REL::make_pattern<"48 8B 01 FF 50 38">().match(site))
		{
			g_inner = nullptr;
			trampoline.write_call<6>(site, &Answer);
			g_siteText = std::format("ID {} +0x{:X} (0x{:X}), vanilla", id, offset, rva);
		}
		else
		{
			logger::error("controls page: the using-controller check at ID {} +0x{:X} (0x{:X}) does not look as expected; NOT hooked - the page follows the game's device, and Square does not switch it", id, offset, rva);
			return;
		}
		g_hooked = true;
		logger::info("controls page: using-controller check hooked at {}; it is answered by the page's own section only while the Controls page is open{}",
					 g_siteText, REX::W32::GetModuleHandleA("AutoInputSwitch.dll") ? " (Auto Input Switch is loaded and works as normal everywhere else)" : "");
	}

	void OnPage(bool a_open)
	{
		if (a_open == g_onPage.load()) { return; }
		g_onPage = a_open;
		if (!g_hooked.load()) { return; }
		if (a_open)
		{
			const bool pad = AskGame();  // the pin is not on yet: this is the game's (or Auto Input Switch's) own answer
			g_pinnedGamepad = pad;
			g_pinActive = true;
			logger::info("controls page: opened on the {} section; it stays there until Square or the page's switch", pad ? "controller" : "keyboard and mouse");
		}
		else
		{
			g_pinActive = false;
			logger::info("controls page: closed; the game picks the device again ({} now)", AskGame() ? "controller" : "keyboard and mouse");
			RefreshMenus();
		}
	}

	bool Toggle(const char* a_why)
	{
		if (!g_hooked.load() || !g_pinActive.load()) { return false; }
		const bool pad = !g_pinnedGamepad.load();
		g_pinnedGamepad = pad;
		g_toggles.fetch_add(1);
		logger::info("controls page: the {} section ({})", pad ? "controller" : "keyboard and mouse", a_why);
		RefreshMenus();
		return true;
	}

	bool Hooked() { return g_hooked.load(); }
	bool Pinned() { return g_pinActive.load(); }

	std::string StateJson()
	{
		return std::format("\"devicePin\":{{\"hooked\":{},\"site\":\"{}\",\"chained\":{},\"onPage\":{},\"pinned\":{},\"section\":\"{}\",\"answered\":{},\"toggles\":{},\"gameSays\":\"{}\"}}",
						   g_hooked.load() ? "true" : "false", g_siteText, g_inner ? "true" : "false", g_onPage.load() ? "true" : "false",
						   g_pinActive.load() ? "true" : "false", g_pinnedGamepad.load() ? "gamepad" : "keyboard", g_answered.load(), g_toggles.load(),
						   g_pinActive.load() ? "pinned" : (AskGame() ? "gamepad" : "keyboard"));
	}
}
