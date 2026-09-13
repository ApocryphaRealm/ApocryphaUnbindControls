#include "PCH.h"

#include "UI.h"

#include "SKSEMenuFramework.h"

#include "Settings.h"
#include "Unbinder.h"

#include "utils/Logger.h"
#include "utils/Strings.h"
#include "utils/Toggle.h"

#include <algorithm>
#include <format>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace UI
{
	namespace
	{
		std::mutex g_statusLock;
		std::string g_status;

		constexpr const char* kLogLevelNames[] = { "Trace", "Debug", "Info", "Warning", "Error", "Critical", "Off" };
		constexpr const char* kLogLevelKeys[] = { "UVC_LogLevel_Trace", "UVC_LogLevel_Debug", "UVC_LogLevel_Info",
												  "UVC_LogLevel_Warning", "UVC_LogLevel_Error", "UVC_LogLevel_Critical", "UVC_LogLevel_Off" };
		constexpr int kLogLevelCount = 7;

		void SetStatus(std::string a_s)
		{
			std::scoped_lock l(g_statusLock);
			g_status = std::move(a_s);
		}

		std::string GetStatus()
		{
			std::scoped_lock l(g_statusLock);
			return g_status;
		}

		void OnMainThread(std::function<void()> a_task)
		{
			if (auto* taskInterface = SKSE::GetTaskInterface())
			{
				taskInterface->AddTask(std::move(a_task));
			}
		}

		bool HasRequiredExports()
		{
			constexpr const char* required[] = {
				"AddSectionItem",
				"igTextV",
				"igTextDisabledV",
				"igTextColoredV",
				"igTextWrappedV",
				"igSetTooltipV",
				"igSeparatorText",
				"igCombo_Str_arr",
				"igIsItemHovered",
				"igButton",
				"igSameLine",
				"igSpacing",
				"igPushItemWidth",
				"igPopItemWidth",
				"igGetCursorScreenPos",
				"igGetWindowDrawList",
				"igGetFrameHeight",
				"igInvisibleButton",
				"igPushID_Str",
				"igPushID_Int",
				"igPopID",
				"igBeginTable",
				"igEndTable",
				"igTableNextRow",
				"igTableNextColumn",
				"igTableSetupColumn",
				"igTableHeadersRow",
				"igCollapsingHeader_TreeNodeFlags",
				"ImDrawList_AddRectFilled",
				"ImDrawList_AddCircleFilled"
			};

			for (const char* name : required)
			{
				if (!GetMenuFrameworkFunction<void*>(name))
				{
					logger::warn("The menu framework does not export \"{}\"", name);
					return false;
				}
			}
			return true;
		}

		void HelpMarker(const char* a_description)
		{
			ImGuiMCP::SameLine();
			ImGuiMCP::TextDisabled("%s", strings::TR("UVC_HelpMark", "(?)"));
			if (ImGuiMCP::IsItemHovered())
			{
				ImGuiMCP::SetTooltip("%s", a_description);
			}
		}

		// The page's name for a context (the INI keeps the engine's English name; this is what is drawn).
		const char* ContextLabel(int a_context)
		{
			const std::string_view n = unbinder::ContextName(a_context);
			if (n == "Gameplay") { return strings::TR("UVC_Ctx_Gameplay", "Gameplay"); }
			if (n == "Menu Mode") { return strings::TR("UVC_Ctx_MenuMode", "Menus (general)"); }
			if (n == "Console") { return strings::TR("UVC_Ctx_Console", "Console"); }
			if (n == "Item Menus") { return strings::TR("UVC_Ctx_ItemMenus", "Item menus"); }
			if (n == "Inventory") { return strings::TR("UVC_Ctx_Inventory", "Inventory"); }
			if (n == "Debug Text") { return strings::TR("UVC_Ctx_DebugText", "Debug text"); }
			if (n == "Favorites") { return strings::TR("UVC_Ctx_Favorites", "Favorites menu"); }
			if (n == "Map") { return strings::TR("UVC_Ctx_Map", "Map"); }
			if (n == "Stats") { return strings::TR("UVC_Ctx_Stats", "Skills"); }
			if (n == "Cursor") { return strings::TR("UVC_Ctx_Cursor", "Cursor"); }
			if (n == "Book") { return strings::TR("UVC_Ctx_Book", "Books"); }
			if (n == "Debug Overlay") { return strings::TR("UVC_Ctx_DebugOverlay", "Debug overlay"); }
			if (n == "Journal") { return strings::TR("UVC_Ctx_Journal", "Journal"); }
			if (n == "TFC") { return strings::TR("UVC_Ctx_TFC", "Free camera"); }
			if (n == "Debug Map") { return strings::TR("UVC_Ctx_DebugMap", "Debug map"); }
			if (n == "Lockpicking") { return strings::TR("UVC_Ctx_Lockpicking", "Lockpicking"); }
			if (n == "Marketplace") { return strings::TR("UVC_Ctx_Marketplace", "Creations"); }
			if (n == "Favor") { return strings::TR("UVC_Ctx_Favor", "Follower commands"); }
			return unbinder::ContextName(a_context);
		}

		void QueueToggle(int a_context, const std::string& a_event, int a_device, bool a_unbind)
		{
			OnMainThread([=]() {
				std::string why;
				const bool ok = a_unbind ? unbinder::Unbind(a_context, a_event, a_device, why) : unbinder::Rebind(a_context, a_event, a_device, why);
				const std::string ctxName = unbinder::ContextName(a_context);
				if (ok)
				{
					settings::Save();
					const std::string fmt = a_unbind ? strings::TR("UVC_StatusUnbound", "{} - {} now has no key. Saved.") : strings::TR("UVC_StatusRebound", "{} - {} has its key back. Saved.");
					SetStatus(std::vformat(fmt, std::make_format_args(ctxName, a_event)));
				}
				else
				{
					const std::string fmt = strings::TR("UVC_StatusRefused", "{} - {}: {}");
					SetStatus(std::vformat(fmt, std::make_format_args(ctxName, a_event, why)));
				}
			});
		}

		// One context's table. Rows come from the snapshot the main thread rebuilds after every change.
		void RenderContext(int a_context, const std::vector<unbinder::Row>& a_rows)
		{
			ImGuiMCP::PushID(a_context);
			if (ImGuiMCP::CollapsingHeader(ContextLabel(a_context), ImGuiMCP::ImGuiTreeNodeFlags_DefaultOpen))
			{
				bool any = false;
				for (const auto& r : a_rows) { if (r.context == a_context) { any = true; break; } }
				if (!any)
				{
					ImGuiMCP::TextDisabled("%s", strings::TR("UVC_NoRows", "The game has no controls in this context on this runtime."));
				}
				else if (ImGuiMCP::BeginTable("controls", 4, ImGuiMCP::ImGuiTableFlags_RowBg | ImGuiMCP::ImGuiTableFlags_BordersInnerH | ImGuiMCP::ImGuiTableFlags_SizingStretchProp))
				{
					ImGuiMCP::TableSetupColumn(strings::TR("UVC_ColControl", "Control"), ImGuiMCP::ImGuiTableColumnFlags_WidthStretch, 2.2F);
					ImGuiMCP::TableSetupColumn(strings::TR("UVC_ColKeyboard", "Keyboard"), ImGuiMCP::ImGuiTableColumnFlags_WidthStretch, 1.6F);
					ImGuiMCP::TableSetupColumn(strings::TR("UVC_ColMouse", "Mouse"), ImGuiMCP::ImGuiTableColumnFlags_WidthStretch, 1.3F);
					ImGuiMCP::TableSetupColumn(strings::TR("UVC_ColGamepad", "Gamepad"), ImGuiMCP::ImGuiTableColumnFlags_WidthStretch, 1.6F);
					ImGuiMCP::TableHeadersRow();
					int index = 0;
					for (const auto& r : a_rows)
					{
						if (r.context != a_context) { continue; }
						ImGuiMCP::PushID(index++);
						ImGuiMCP::TableNextRow();
						ImGuiMCP::TableNextColumn();
						ImGuiMCP::Text("%s", r.label.c_str());
						if (ImGuiMCP::IsItemHovered()) { ImGuiMCP::SetTooltip("%s", r.event.c_str()); }
						if (r.warn)
						{
							ImGuiMCP::SameLine();
							ImGuiMCP::TextColored(ImGuiMCP::ImVec4{ 1.0F, 0.75F, 0.2F, 1.0F }, "%s", strings::TR("UVC_WarnMark", "!"));
							if (ImGuiMCP::IsItemHovered()) { ImGuiMCP::SetTooltip("%s", strings::TR("UVC_WarnSystem", "Without this control on a device there may be no way to open the system menu from that device. The framework's own key still works.")); }
						}
						for (int d = 0; d < 3; ++d)
						{
							ImGuiMCP::TableNextColumn();
							const unbinder::Cell& c = r.cell[d];
							if (!c.exists)
							{
								ImGuiMCP::TextDisabled("%s", strings::TR("UVC_NoKey", "-"));
								continue;
							}
							ImGuiMCP::PushID(d);
							bool unbound = c.unbound;
							if (ImGuiMCP::Toggle("##unbound", &unbound))
							{
								QueueToggle(a_context, r.event, d, unbound);
							}
							if (ImGuiMCP::IsItemHovered())
							{
								ImGuiMCP::SetTooltip("%s", c.unbound ? strings::TR("UVC_TipRebind", "On = this control has no key on this device. Switch off to give it its key back.")
																	 : strings::TR("UVC_TipUnbind", "Switch on to leave this control with no key on this device. The key it had is remembered."));
							}
							ImGuiMCP::SameLine();
							if (c.unbound) { ImGuiMCP::TextDisabled("%s", c.keyName.c_str()); }
							else { ImGuiMCP::Text("%s", c.keyName.c_str()); }
							ImGuiMCP::PopID();
						}
						ImGuiMCP::PopID();
					}
					ImGuiMCP::EndTable();
				}
			}
			ImGuiMCP::PopID();
		}

		void RenderContexts(const int* a_contexts, int a_count)
		{
			strings::Tick();
			auto rows = unbinder::GetRows();
			if (!settings::general::enabled)
			{
				ImGuiMCP::TextWrapped("%s", strings::TR("UVC_DisabledNote", "The mod is switched off on the General tab: switches here are remembered but nothing is unbound in the game."));
				ImGuiMCP::Spacing();
			}
			for (int i = 0; i < a_count; ++i)
			{
				const int c = a_contexts[i];
				if (c < 0 || c >= unbinder::ContextCount()) { continue; }
				RenderContext(c, *rows);
				ImGuiMCP::Spacing();
			}
		}

		void RenderLogLevel()
		{
			int level = static_cast<int>(settings::debug::logLevel);
			level = std::clamp(level, 0, kLogLevelCount - 1);
			std::vector<std::string> store;
			store.reserve(kLogLevelCount);
			for (int i = 0; i < kLogLevelCount; ++i) { store.push_back(strings::TR(kLogLevelKeys[i], kLogLevelNames[i])); }
			std::vector<const char*> labels;
			labels.reserve(store.size());
			for (const auto& s : store) { labels.push_back(s.c_str()); }
			if (ImGuiMCP::Combo(strings::TR("UVC_LogLevel", "Log level"), &level, labels.data(), kLogLevelCount))
			{
				settings::debug::logLevel = static_cast<std::uint32_t>(level);
				settings::ApplyLogLevel();
				OnMainThread([]() { settings::Save(); });
			}
			HelpMarker(strings::TR("UVC_HelpLogLevel", "Applies and saves immediately. The log is at Documents\\My Games\\Skyrim Special Edition\\SKSE\\ApocryphaUnbindControls.log."));
		}
	}

	void Register()
	{
		if (!SKSEMenuFramework::IsInstalled())
		{
			logger::info("No menu framework is installed; the list is applied from the INI only");
			return;
		}
		if (!HasRequiredExports())
		{
			logger::warn("The installed menu framework is older than this plugin's page needs. Update it (Apocrypha Menu Framework, or SKSE Menu Framework version 3 or newer).");
			return;
		}

		SKSEMenuFramework::SetSection("Unbind Vanilla Controls");
		SKSEMenuFramework::AddSectionItem("General", Panels::General);
		SKSEMenuFramework::AddSectionItem("Gameplay", Panels::Gameplay);
		SKSEMenuFramework::AddSectionItem("Menus", Panels::Menus);
		SKSEMenuFramework::AddSectionItem("Console and Debug", Panels::ConsoleAndDebug);
		logger::info("Registered the page with the menu framework (4 tabs)");
	}

	void __stdcall Panels::General()
	{
		strings::Tick();

		ImGuiMCP::TextWrapped("%s", strings::TR("UVC_Intro", "Switch any of the game's own controls to have NO key on the keyboard, the mouse or the gamepad, without giving that key to something else. Every switch applies and saves at once; the key a control had is remembered so it can be given back."));
		ImGuiMCP::Spacing();

		ImGuiMCP::PushItemWidth(260.0F);

		ImGuiMCP::SeparatorText(strings::TR("UVC_Title", "Unbind Vanilla Controls"));
		if (ImGuiMCP::Toggle(strings::TR("UVC_Enabled", "Enabled"), &settings::general::enabled))
		{
			const bool on = settings::general::enabled;
			OnMainThread([on]() {
				if (on) { unbinder::ApplyAll("enabled from the page"); }
				else { unbinder::RestoreAll("disabled from the page"); }
				settings::Save();
			});
		}
		HelpMarker(strings::TR("UVC_HelpEnabled", "Off gives every listed control its key back while keeping the list, so switching on again restores your choices."));

		const auto entries = unbinder::GetEntries();
		ImGuiMCP::Text(strings::TR("UVC_Count", "Controls with no key: %d"), static_cast<int>(entries.size()));

		ImGuiMCP::Spacing();
		ImGuiMCP::SeparatorText(strings::TR("UVC_Debug", "Debug"));
		RenderLogLevel();

		ImGuiMCP::PopItemWidth();

		ImGuiMCP::SeparatorText("");
		if (ImGuiMCP::Button(strings::TR("UVC_ReloadBtn", "Reload from INI")))
		{
			OnMainThread([]() {
				unbinder::RestoreAll("reload from INI");
				const bool ok = settings::Reload();
				unbinder::ApplyAll("reload from INI");
				SetStatus(ok ? strings::TR("UVC_StatusReloaded", "Settings reloaded from the INI and applied.") : strings::TR("UVC_StatusReloadFail", "Could not read the INI. See the log for why."));
			});
		}
		HelpMarker(strings::TR("UVC_HelpReload", "Gives every control its key back, re-reads the INI from disk and applies what it lists."));

		ImGuiMCP::SameLine();

		if (ImGuiMCP::Button(strings::TR("UVC_RestoreBtn", "Restore defaults")))
		{
			OnMainThread([]() {
				unbinder::ClearAll();
				settings::RestoreDefaults();
				unbinder::SetEntries(unbinder::DefaultEntries());
				unbinder::ApplyAll("defaults restored");
				settings::Save();
				SetStatus(strings::TR("UVC_StatusRestored", "Back to the shipped list: the keys for screens the Tween Menu already opens have no key again, and every other control has its key back. Saved."));
				logger::info("defaults restored from the page");
			});
		}
		HelpMarker(strings::TR("UVC_HelpRestore", "Gives every control its key back, then unbinds only the shipped list - the keyboard keys for Journal, Inventory, Magic, Map, Skills and Wait, which the Tween Menu already opens - and saves."));

		const std::string status = GetStatus();
		if (!status.empty())
		{
			ImGuiMCP::TextWrapped("%s", status.c_str());
		}

		ImGuiMCP::Spacing();
		ImGuiMCP::TextWrapped("%s", strings::TR("UVC_Note", "The game's own Controls menu shows an unbound control with no key. If you remove this mod, one Reset to defaults there clears anything the game saved while a control had no key."));
		ImGuiMCP::Spacing();
		ImGuiMCP::Text("%s", settings::GetIniPath().c_str());
	}

	void __stdcall Panels::Gameplay()
	{
		static const int contexts[] = { 0 };
		RenderContexts(contexts, 1);
	}

	void __stdcall Panels::Menus()
	{
		// Menu Mode, Item Menus, Inventory, Favorites, Map, Stats, Cursor, Book, Journal, Lockpicking, Marketplace (AE), Favor
		std::vector<int> contexts = { 1, 3, 4, 6, 7, 8, 9, 10, 12, 15 };
		if (unbinder::ContextCount() == 18) { contexts.push_back(16); contexts.push_back(17); }
		else { contexts.push_back(16); }
		RenderContexts(contexts.data(), static_cast<int>(contexts.size()));
	}

	void __stdcall Panels::ConsoleAndDebug()
	{
		// Console, Debug Text, Debug Overlay, TFC, Debug Map
		static const int contexts[] = { 2, 5, 11, 13, 14 };
		RenderContexts(contexts, 5);
	}
}
