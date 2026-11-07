// Unbind Vanilla Controls - Gamepad++ compatibility (1.5.0), own code, GPL-3.0-or-later. See include/GamepadPP.h.
#include "PCH.h"

#include "GamepadPP.h"

#include "Unbinder.h"
#include "utils/Logger.h"

#include <array>
#include <format>
#include <mutex>

namespace gpp
{
	namespace
	{
		constexpr std::string_view kPlugin = "Gamepad++.esp";
		constexpr RE::FormID kQuestID = 0x800;                                        // gpp_controlquest
		constexpr std::array<RE::FormID, 4> kComboGlobals = { 0x801, 0x802, 0x803, 0x804 };  // GPP_COMBO1..4
		constexpr const char* kScript = "gpp_keyhandler";

		struct SlotDef
		{
			const char* id;
			const char* property;
			const char* row;
		};
		constexpr std::array<SlotDef, kSlotCount> kSlots = {
			SlotDef{ "A1", "GPP_KEYCODE_A1", "Gamepad++ Action 1" },
			SlotDef{ "A2", "GPP_KEYCODE_A2", "Gamepad++ Action 2" },
			SlotDef{ "A3", "GPP_KEYCODE_A3", "Gamepad++ Action 3" },
			SlotDef{ "A4", "GPP_KEYCODE_A4", "Gamepad++ Action 4" },
			SlotDef{ "C1", "GPP_KEYCODE_C1", "Gamepad++ Combo 1" },
			SlotDef{ "C2", "GPP_KEYCODE_C2", "Gamepad++ Combo 2" },
			SlotDef{ "C3", "GPP_KEYCODE_C3", "Gamepad++ Combo 3" },
			SlotDef{ "C4", "GPP_KEYCODE_C4", "Gamepad++ Combo 4" },
		};

		std::mutex g_lock;
		bool g_found = false;
		std::array<Slot, kSlotCount> g_slots{};
		std::string g_source = "not read";  // "script", "globals", "nothing"
		std::string g_lastReason;
		bool g_fourth = false;
		int g_reads = 0;
		std::string g_lastSummary;

		// SKSE Input Script numbering (SKSE::InputMap: 266 + the button's place) to the control map's pad code.
		std::uint16_t PadOf(int a_skse)
		{
			switch (a_skse)
			{
			case 266: return 0x0001;  // D-pad up
			case 267: return 0x0002;  // D-pad down
			case 268: return 0x0004;  // D-pad left
			case 269: return 0x0008;  // D-pad right
			case 270: return 0x0010;  // Start
			case 271: return 0x0020;  // Back
			case 272: return 0x0040;  // left stick click
			case 273: return 0x0080;  // right stick click
			case 274: return 0x0100;  // LB
			case 275: return 0x0200;  // RB
			case 276: return 0x1000;  // A
			case 277: return 0x2000;  // B
			case 278: return 0x4000;  // X
			case 279: return 0x8000;  // Y
			case 280: return 0x0009;  // LT
			case 281: return 0x000A;  // RT
			default: return 0xFF;
			}
		}

		const char* PadText(std::uint16_t a_pad)
		{
			const char* name = a_pad == 0xFF ? "" : unbinder::ButtonName(a_pad, 2);
			return name && name[0] ? name : "none";
		}

		bool PluginLoaded()
		{
			auto* data = RE::TESDataHandler::GetSingleton();
			if (!data) { return false; }
			return data->LookupLoadedModByName(kPlugin) != nullptr || data->LookupLoadedLightModByName(kPlugin) != nullptr;
		}

		// The key handler's bound script object, on whichever alias of the control quest carries it.
		RE::BSTSmartPointer<RE::BSScript::Object> KeyHandler()
		{
			RE::BSTSmartPointer<RE::BSScript::Object> object;
			auto* data = RE::TESDataHandler::GetSingleton();
			auto* quest = data ? data->LookupForm<RE::TESQuest>(kQuestID, kPlugin) : nullptr;
			if (!quest)
			{
				logger::debug("gamepad++: the control quest ({:#x} in {}) was not found", kQuestID, kPlugin);
				return object;
			}
			auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
			auto* policy = vm ? vm->GetObjectHandlePolicy() : nullptr;
			if (!policy)
			{
				logger::debug("gamepad++: the Papyrus VM is not ready");
				return object;
			}
			for (auto* alias : quest->aliases)
			{
				if (!alias) { continue; }
				const auto handle = policy->GetHandleForObject(RE::BGSRefAlias::VMTYPEID, alias);
				if (handle == policy->EmptyHandle()) { continue; }
				if (vm->FindBoundObject(handle, kScript, object) && object)
				{
					logger::debug("gamepad++: {} found on alias {} of {}", kScript, alias->aliasID, quest->GetFormEditorID());
					return object;
				}
			}
			logger::debug("gamepad++: no alias of the control quest has {} bound yet", kScript);
			object.reset();
			return object;
		}

		bool ReadInt(RE::BSScript::Object& a_object, const char* a_property, int& a_out)
		{
			const auto* var = a_object.GetProperty(a_property);
			if (!var || !var->IsInt()) { return false; }
			a_out = var->GetSInt();
			return true;
		}
	}

	void AtDataLoaded()
	{
		const bool found = PluginLoaded();
		{
			std::scoped_lock l(g_lock);
			g_found = found;
		}
		if (found) { logger::info("Gamepad++ found ({}): the buttons it uses are read when a game loads and when the journal opens, shown on the Controls page and refused to binds there", kPlugin); }
		else { logger::info("Gamepad++ not found ({} is not loaded): nothing changes", kPlugin); }
	}

	void Refresh(const char* a_reason)
	{
		bool found = false;
		{
			std::scoped_lock l(g_lock);
			found = g_found;
		}
		if (!found) { return; }

		std::array<Slot, kSlotCount> slots{};
		std::string source = "nothing";
		bool fourth = false;
		auto object = KeyHandler();
		if (object)
		{
			source = "script";
			const auto* fourthVar = object->GetProperty("bFourthComboEnabled");
			fourth = fourthVar && fourthVar->IsBool() && fourthVar->GetBool();
			logger::debug("gamepad++ ({}): bFourthComboEnabled = {}", a_reason, fourth);
			for (int i = 0; i < kSlotCount; ++i)
			{
				auto& s = slots[static_cast<std::size_t>(i)];
				const auto& def = kSlots[static_cast<std::size_t>(i)];
				s.id = def.id;
				s.row = def.row;
				int value = -1;
				if (!ReadInt(*object, def.property, value))
				{
					logger::debug("gamepad++ ({}): {} could not be read (no such int property)", a_reason, def.property);
					continue;
				}
				s.skse = value;
				s.pad = PadOf(value);
				s.registered = s.pad != 0xFF && (i != 7 || fourth);
				logger::debug("gamepad++ ({}): {} = {} -> {}{}", a_reason, def.property, value, PadText(s.pad),
							  s.registered ? "" : (s.pad == 0xFF ? " (no button)" : " (not registered: the fourth combo key is off)"));
			}
		}
		else
		{
			// Before the key handler is bound: the combo globals, which Gamepad++ sets from the combo keys.
			auto* data = RE::TESDataHandler::GetSingleton();
			for (int i = 0; i < kSlotCount; ++i)
			{
				auto& s = slots[static_cast<std::size_t>(i)];
				s.id = kSlots[static_cast<std::size_t>(i)].id;
				s.row = kSlots[static_cast<std::size_t>(i)].row;
				if (i < 4) { continue; }
				const RE::FormID id = kComboGlobals[static_cast<std::size_t>(i - 4)];
				auto* global = data ? data->LookupForm<RE::TESGlobal>(id, kPlugin) : nullptr;
				if (!global)
				{
					logger::debug("gamepad++ ({}): global {:#x} not found", a_reason, id);
					continue;
				}
				source = "globals";
				s.skse = static_cast<int>(global->value);
				s.pad = PadOf(s.skse);
				s.registered = s.pad != 0xFF && i != 7;  // C4's switch is in the script; unknown here
				logger::debug("gamepad++ ({}): {} = {} -> {}", a_reason, global->GetFormEditorID(), s.skse, PadText(s.pad));
			}
		}

		std::string summary;
		for (const auto& s : slots)
		{
			if (s.registered) { summary += std::format("{}{} {}", summary.empty() ? "" : ", ", s.id, PadText(s.pad)); }
		}
		if (summary.empty()) { summary = "none"; }
		bool changed = false;
		{
			std::scoped_lock l(g_lock);
			g_slots = slots;
			g_source = source;
			g_fourth = fourth;
			g_lastReason = a_reason ? a_reason : "";
			++g_reads;
			changed = summary != g_lastSummary;
			g_lastSummary = summary;
		}
		logger::debug("gamepad++ ({}): read from {}: {}", a_reason, source, summary);
		if (!changed) { return; }
		logger::info("Gamepad++ buttons ({}, read from {}): {}", a_reason, source, summary);
		// A game control already on an action key fires as well as Gamepad++'s action: said once, nothing is changed. A combo key is
		// shared by design (held with an action key), so a control on it is only noted at debug.
		for (std::size_t i = 0; i < slots.size(); ++i)
		{
			const auto& s = slots[i];
			if (!s.registered) { continue; }
			unbinder::Holder holder;
			if (!unbinder::HolderOf(0, 2, s.pad, 0, holder)) { continue; }
			if (i < 4) { logger::info("Gamepad++ {} is on {}, which the game's \"{}\" also has in Gameplay - both act on a press", s.id, PadText(s.pad), holder.event); }
			else { logger::debug("Gamepad++ {} (a combo key, held with an action key) is on {}, shared with the game's \"{}\"", s.id, PadText(s.pad), holder.event); }
		}
	}

	bool Found()
	{
		std::scoped_lock l(g_lock);
		return g_found;
	}

	std::vector<Slot> Slots()
	{
		std::scoped_lock l(g_lock);
		return { g_slots.begin(), g_slots.end() };
	}

	std::uint16_t SlotPad(int a_slot)
	{
		if (a_slot < 0 || a_slot >= kSlotCount) { return 0xFF; }
		std::scoped_lock l(g_lock);
		const auto& s = g_slots[static_cast<std::size_t>(a_slot)];
		return g_found && s.registered ? s.pad : static_cast<std::uint16_t>(0xFF);
	}

	const char* SlotRow(int a_slot) { return a_slot >= 0 && a_slot < kSlotCount ? kSlots[static_cast<std::size_t>(a_slot)].row : ""; }

	int SlotOfRow(std::string_view a_row)
	{
		for (int i = 0; i < kSlotCount; ++i)
		{
			if (a_row == kSlots[static_cast<std::size_t>(i)].row) { return i; }
		}
		return -1;
	}

	int SlotFor(std::uint32_t a_code)
	{
		std::scoped_lock l(g_lock);
		if (!g_found) { return -1; }
		for (int i = 0; i < 4; ++i)  // the action keys; a combo key is only held with one of them
		{
			const auto& s = g_slots[static_cast<std::size_t>(i)];
			if (s.registered && s.pad == a_code) { return i; }
		}
		return -1;
	}

	std::string StateJson()
	{
		std::scoped_lock l(g_lock);
		std::string slots;
		for (const auto& s : g_slots)
		{
			if (!s.id[0]) { continue; }
			slots += std::format(R"({}{{"slot":"{}","row":"{}","skse":{},"pad":"{:#06x}","button":"{}","registered":{},"refusesBinds":{}}})", slots.empty() ? "" : ",", s.id, s.row, s.skse,
								 s.pad, PadText(s.pad), s.registered ? "true" : "false", s.registered && s.id[0] == 'A' ? "true" : "false");
		}
		return std::format(R"("found":{},"plugin":"{}","source":"{}","fourthCombo":{},"reads":{},"lastReason":"{}","summary":"{}","slots":[{}])", g_found ? "true" : "false", kPlugin, g_source,
						   g_fourth ? "true" : "false", g_reads, g_lastReason, g_lastSummary, slots);
	}
}
