// Unbind Vanilla Controls - own code, GPL-3.0-or-later. See TweenWait.h for why this exists.
// The request and the vanilla Wait press mirror Tween Menu Overhaul 1.2's own code (github.com/jpsteel/skyrim-tweenmenu,
// commit 0d77caaa, src\Utility.cpp OpenMenuFromTween / TriggerVanillaWait), read 2026-10-25.
#include "PCH.h"

#include "TweenWait.h"

#include "utils/Logger.h"

#include <format>

namespace tweenwait
{
	namespace
	{
		bool g_pending = false;      // a request arrived while keyboard Wait had no key; act when the tween menu closes
		int g_requests = 0;          // Wait requests seen from Tween Menu Overhaul this session
		int g_handled = 0;           // of those, the ones this module answered (keyboard Wait had no key)
		std::string g_lastResult = "none";

		bool KeyboardWaitUnbound()
		{
			auto* map = RE::ControlMap::GetSingleton();
			if (!map)
			{
				logger::warn("tween wait: ControlMap singleton is null; leaving the request to Tween Menu Overhaul");
				return false;
			}
			const auto key = map->GetMappedKey("Wait", RE::INPUT_DEVICE::kKeyboard, RE::ControlMap::InputContextID::kGameplay);
			logger::debug("tween wait: keyboard Wait key is 0x{:X}", key);
			return key == RE::ControlMap::kInvalid;
		}

		// The game's own Wait, through the handler a Wait key press reaches (what Tween Menu Overhaul does with a key).
		void SendVanillaWait()
		{
			auto* controls = RE::MenuControls::GetSingleton();
			auto* events = RE::UserEvents::GetSingleton();
			if (!controls || !controls->menuOpenHandler || !events)
			{
				g_lastResult = "no menu-open handler";
				logger::warn("tween wait: the game's menu-open handler or user events are null; the wait menu was not opened");
				return;
			}
			auto* button = RE::ButtonEvent::Create(RE::INPUT_DEVICE::kKeyboard, events->wait, RE::ControlMap::kInvalid, 1.0f, 0.0f);
			if (!button)
			{
				g_lastResult = "no button event";
				logger::warn("tween wait: could not create the Wait button event; the wait menu was not opened");
				return;
			}
			const bool canProcess = controls->menuOpenHandler->CanProcess(button);
			bool processed = false;
			if (canProcess) { processed = controls->menuOpenHandler->ProcessButton(button); }
			button->~ButtonEvent();
			RE::free(button);
			++g_handled;
			g_lastResult = std::format("canProcess={} processButton={}", canProcess, processed);
			logger::info("tween wait: keyboard Wait has no key, sent the game's own Wait (CanProcess {}, ProcessButton {})", canProcess, processed);
		}

		class ModEventSink : public RE::BSTEventSink<SKSE::ModCallbackEvent>
		{
		public:
			static ModEventSink* GetSingleton() { static ModEventSink s; return &s; }

			RE::BSEventNotifyControl ProcessEvent(const SKSE::ModCallbackEvent* a_event, RE::BSTEventSource<SKSE::ModCallbackEvent>*) override
			{
				if (!a_event || a_event->eventName != "OpenMenuFromTween") { return RE::BSEventNotifyControl::kContinue; }
				// numArg 1 is Tween Menu Overhaul's Papyrus-event option, which it hands on as its own mod event.
				if (a_event->strArg != RE::SleepWaitMenu::MENU_NAME || a_event->numArg == 1.0f) { return RE::BSEventNotifyControl::kContinue; }
				++g_requests;
				g_pending = KeyboardWaitUnbound();
				logger::info("tween wait: Tween Menu Overhaul asked for the wait menu; {}", g_pending ? "keyboard Wait has no key, opening it when the tween menu closes" : "keyboard Wait has a key, Tween Menu Overhaul opens it");
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		class MenuSink : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
		{
		public:
			static MenuSink* GetSingleton() { static MenuSink s; return &s; }

			RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
			{
				if (!a_event || a_event->opening || !g_pending || a_event->menuName != RE::TweenMenu::MENU_NAME) { return RE::BSEventNotifyControl::kContinue; }
				g_pending = false;
				SendVanillaWait();
				return RE::BSEventNotifyControl::kContinue;
			}
		};
	}

	void Install()
	{
		static bool installed = false;
		if (installed) { return; }
		auto* modEvents = SKSE::GetModCallbackEventSource();
		auto* ui = RE::UI::GetSingleton();
		if (!modEvents || !ui)
		{
			logger::warn("tween wait: {} is null at kDataLoaded; Tween Menu Overhaul's Wait option needs a keyboard Wait key this session", modEvents ? "RE::UI" : "the SKSE mod-event source");
			return;
		}
		modEvents->AddEventSink(ModEventSink::GetSingleton());
		ui->AddEventSink<RE::MenuOpenCloseEvent>(MenuSink::GetSingleton());
		installed = true;
		logger::info("sink registered: Tween Menu Overhaul's Wait option opens the wait menu while keyboard Wait has no key");
	}

	std::string StateJson()
	{
		return std::format(R"("tweenWait":{{"pending":{},"requests":{},"handled":{},"lastResult":"{}"}})", g_pending ? "true" : "false", g_requests, g_handled, g_lastResult);
	}

	bool Fire()
	{
		auto* modEvents = SKSE::GetModCallbackEventSource();
		if (!modEvents) { return false; }
		const SKSE::ModCallbackEvent modEvent{ "OpenMenuFromTween", RE::SleepWaitMenu::MENU_NAME, 0.0f, nullptr };
		modEvents->SendEvent(&modEvent);
		return true;
	}
}
