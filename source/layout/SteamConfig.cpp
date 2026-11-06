// Unbind Vanilla Controls, the controller graphic (was UVC Controller Layout until 1.4.0) - own code, GPL-3.0-or-later. Read-only: which pad Steam has, and what its controls send in this game's config.
//
// Where things are, as read on a real install on 2026-11-05 (the formats are Steam's own and undocumented):
// - HKCU\Software\Valve\Steam: SteamPath, and ActiveProcess\ActiveUser for the account's folder.
// - logs\controller.txt. "Steam Controller reserving XInput slot" is written for every pad, a DualShock too, so it names nothing. What does:
//   "Controller using HIDAPI driver, vid=0x054c" (a pad on USB or Bluetooth, by maker) and "Controller Connected to Dongle" (Valve's
//   wireless pad), each until a later "to disconnected" or "Controller device closed". "SetSelectedConfigForApp - idx 0 app: 489830, 0
//   CLOUD_<path>" says which config was picked, and is only written when the pick changes.
// - steamapps\common\Steam Controller Configs\<account>\config\configset_controller_<set>.vdf: "<app>" { "autosave" "1" } means
//   <app>\controller_<set>.vdf; "template" names a file in controller_base\templates. The name of a set is NOT the pad's type: the 2026
//   Steam Controller's autosave was found as controller_neptune.vdf, saying controller_type controller_triton, its own set empty. So the
//   type is always read from inside the file.
// - In the config: each "group" has an id, a mode and inputs; preset.group_source_bindings says which group is active on which control.
#include "PCH.h"

#include "layout/SteamConfig.h"

#include "layout/PadTables.h"
#include "layout/LayoutStrings.h"
#include "utils/Logger.h"

#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <sstream>
#include <string_view>

namespace steamconfig
{
	namespace
	{
		namespace fs = std::filesystem;

		constexpr const char* kAppId = "489830";  // Skyrim Special Edition, on every runtime
		constexpr std::uintmax_t kMaxFile = 16 * 1024 * 1024;

		Info g_info;

		std::string Lower(std::string a_s)
		{
			std::transform(a_s.begin(), a_s.end(), a_s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return a_s;
		}

		bool ReadFile(const fs::path& a_path, std::string& a_out)
		{
			std::error_code ec;
			const auto size = fs::file_size(a_path, ec);
			if (ec || size > kMaxFile) { return false; }
			std::ifstream file(a_path, std::ios::binary);  // Steam keeps its log open; this shares it
			if (!file) { return false; }
			a_out.assign(static_cast<std::size_t>(size), '\0');
			file.read(a_out.data(), static_cast<std::streamsize>(size));
			a_out.resize(static_cast<std::size_t>(file.gcount()));
			return true;
		}

		fs::path SteamRoot()
		{
			wchar_t buffer[1024]{};
			DWORD size = sizeof(buffer);
			if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", RRF_RT_REG_SZ, nullptr, buffer, &size) != ERROR_SUCCESS) { return {}; }
			return fs::path(buffer);
		}

		std::string ActiveAccount()
		{
			DWORD value = 0;
			DWORD size = sizeof(value);
			if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam\\ActiveProcess", L"ActiveUser", RRF_RT_REG_DWORD, nullptr, &value, &size) != ERROR_SUCCESS || value == 0) { return {}; }
			return std::to_string(value);
		}

		// ---- the log ----

		// The newest line of a kind before a_end, as its offset; npos for none.
		std::size_t LastOf(const std::string& a_log, std::string_view a_what) { return a_log.rfind(a_what); }

		void ReadLogPad(const std::string& a_log, Info& a_info)
		{
			constexpr std::string_view dongle = "Controller Connected to Dongle";
			constexpr std::string_view hid = "Controller using HIDAPI driver, vid=0x";
			const auto atDongle = LastOf(a_log, dongle);
			const auto atHid = LastOf(a_log, hid);
			if (atDongle == std::string::npos && atHid == std::string::npos)
			{
				a_info.logWhy = "the log names no pad";
				return;
			}
			const bool dongleNewer = atHid == std::string::npos || (atDongle != std::string::npos && atDongle > atHid);
			const std::size_t at = dongleNewer ? atDongle : atHid;
			const auto gone = std::max(a_log.rfind("to disconnected") == std::string::npos ? 0 : a_log.rfind("to disconnected"), a_log.rfind("Controller device closed") == std::string::npos ? 0 : a_log.rfind("Controller device closed"));
			if (gone > at)
			{
				a_info.logWhy = "the log's newest pad has since gone";
				return;
			}
			if (dongleNewer)
			{
				// Valve's wireless pad, by its dongle. The 2015 pad has a dongle too, so its controller_type has the last word (FacesNow's caller).
				a_info.logPad = layoutsettings::Faces::kSteam;
				a_info.logWhy = "the log has a Valve pad on its dongle";
				return;
			}
			const std::string vid = Lower(a_log.substr(at + hid.size(), 4));
			if (vid == "054c") { a_info.logPad = layoutsettings::Faces::kPlayStation; a_info.logWhy = "the log has a Sony pad"; }
			else if (vid == "045e") { a_info.logPad = layoutsettings::Faces::kXbox; a_info.logWhy = "the log has a Microsoft pad"; }
			else if (vid == "28de") { a_info.logPad = layoutsettings::Faces::kSteam; a_info.logWhy = "the log has a Valve pad"; }
			else { a_info.logWhy = std::format("the log has a pad of maker {}, with no picture of its own", vid); }
		}

		// ---- VDF ----

		struct Node
		{
			std::string key;
			std::string value;
			std::vector<Node> children;
			bool block = false;

			const Node* Child(std::string_view a_key) const
			{
				for (const auto& c : children)
				{
					if (c.key == a_key) { return &c; }
				}
				return nullptr;
			}
			std::string Value(std::string_view a_key) const
			{
				const auto* c = Child(a_key);
				return c && !c->block ? c->value : std::string();
			}
		};

		// "key" "value", "key" { ... }; a key can repeat. Comments do not occur in these files. Too deep or unbalanced: false.
		bool ParseVdf(const std::string& a_text, Node& a_root)
		{
			std::vector<Node*> stack{ &a_root };
			std::string pending;
			bool havePending = false;
			std::size_t i = 0;
			while (i < a_text.size())
			{
				const char c = a_text[i];
				if (c == '{')
				{
					if (!havePending || stack.size() > 32) { return false; }
					auto& top = *stack.back();
					top.children.push_back({ pending, {}, {}, true });
					stack.push_back(&top.children.back());
					havePending = false;
					++i;
				}
				else if (c == '}')
				{
					if (stack.size() <= 1) { return false; }
					stack.pop_back();
					havePending = false;
					++i;
				}
				else if (c == '"')
				{
					std::string token;
					++i;
					while (i < a_text.size() && a_text[i] != '"')
					{
						if (a_text[i] == '\\' && i + 1 < a_text.size()) { ++i; }
						token += a_text[i++];
					}
					++i;
					if (havePending)
					{
						stack.back()->children.push_back({ pending, token, {}, false });
						havePending = false;
					}
					else
					{
						pending = std::move(token);
						havePending = true;
					}
				}
				else { ++i; }
			}
			return stack.size() == 1;
		}

		// ---- which config ----

		bool LoadConfig(const fs::path& a_path, Node& a_out, std::string& a_type)
		{
			std::string text;
			Node root;
			if (!ReadFile(a_path, text) || !ParseVdf(text, root)) { return false; }
			const auto* mappings = root.Child("controller_mappings");
			if (!mappings) { return false; }
			a_type = mappings->Value("controller_type");
			a_out = *mappings;
			return !a_type.empty();
		}

		std::optional<fs::path> LoggedConfig(const std::string& a_log, const std::string& a_account)
		{
			const std::string what = std::format("SetSelectedConfigForApp - idx 0 app: {}, ", kAppId);
			const auto at = a_log.rfind(what);
			if (at == std::string::npos) { return std::nullopt; }
			const auto end = a_log.find_first_of("\r\n", at);
			std::string line = a_log.substr(at + what.size(), end == std::string::npos ? std::string::npos : end - at - what.size());
			const auto vdf = Lower(line).find(".vdf");
			const auto cloud = line.find("CLOUD_");
			if (vdf == std::string::npos || cloud == std::string::npos) { return std::nullopt; }
			line = line.substr(cloud + 6, vdf + 4 - cloud - 6);
			if (!a_account.empty() && line.find("\\" + a_account + "\\") == std::string::npos) { return std::nullopt; }  // another account's
			return fs::path(line);
		}

		// What the config sets say for this game, each a file to try.
		std::vector<fs::path> SetConfigs(const fs::path& a_steam, const fs::path& a_config)
		{
			std::vector<fs::path> out;
			std::error_code ec;
			for (const auto& entry : fs::directory_iterator(a_config, ec))
			{
				const std::string name = entry.path().filename().string();
				constexpr std::string_view prefix = "configset_controller_";
				if (!name.starts_with(prefix) || !name.ends_with(".vdf")) { continue; }
				const std::string set = name.substr(prefix.size(), name.size() - prefix.size() - 4);
				std::string text;
				Node root;
				if (!ReadFile(entry.path(), text) || !ParseVdf(text, root)) { continue; }
				const auto* sets = root.Child("controller_config");
				const auto* app = sets ? sets->Child(kAppId) : nullptr;
				if (!app) { continue; }
				if (!app->Value("autosave").empty()) { out.push_back(a_config / kAppId / std::format("controller_{}.vdf", set)); }
				const std::string tmpl = app->Value("template");
				if (!tmpl.empty()) { out.push_back(a_steam / "controller_base" / "templates" / fs::path(tmpl).filename()); }
			}
			return out;
		}

		// The families a pad's picture can use. Nothing is known about the pad: anything goes.
		bool TypeFits(const std::string& a_type, const std::optional<layoutsettings::Faces>& a_pad)
		{
			if (!a_pad) { return true; }
			const auto of = PadOfType(a_type);
			return of && *of == *a_pad;
		}

		bool FindConfig(const fs::path& a_steam, const std::string& a_log, const std::string& a_account, const std::optional<layoutsettings::Faces>& a_pad, Node& a_out, std::string& a_type)
		{
			if (a_account.empty()) { return false; }
			const fs::path config = a_steam / "steamapps" / "common" / "Steam Controller Configs" / a_account / "config";
			std::vector<fs::path> tries;
			if (auto logged = LoggedConfig(a_log, a_account)) { tries.push_back(std::move(*logged)); }
			for (auto& p : SetConfigs(a_steam, config)) { tries.push_back(std::move(p)); }
			// the newest in the game's folder, last
			std::vector<std::pair<fs::file_time_type, fs::path>> inFolder;
			std::error_code ec;
			for (const auto& entry : fs::directory_iterator(config / kAppId, ec))
			{
				if (entry.path().extension() == ".vdf")
				{
					std::error_code timeError;
					inFolder.emplace_back(entry.last_write_time(timeError), entry.path());
				}
			}
			std::sort(inFolder.begin(), inFolder.end(), [](const auto& a_a, const auto& a_b) { return a_a.first > a_b.first; });
			for (auto& [time, path] : inFolder) { tries.push_back(std::move(path)); }

			for (const auto& path : tries)
			{
				Node node;
				std::string type;
				if (LoadConfig(path, node, type) && TypeFits(type, a_pad))
				{
					a_out = std::move(node);
					a_type = std::move(type);
					return true;
				}
			}
			return false;
		}

		// ---- what a config says ----

		// "key_press K, , " to "K"; "controller_action SCREENSHOT, , " to "SCREENSHOT"; "xinput_button DPAD_UP, , " to "DPAD UP".
		std::string ActionWords(const std::string& a_binding)
		{
			std::string first = a_binding.substr(0, a_binding.find(','));
			const auto space = first.find(' ');
			first = space == std::string::npos ? first : first.substr(space + 1);
			while (!first.empty() && first.back() == ' ') { first.pop_back(); }
			std::replace(first.begin(), first.end(), '_', ' ');
			return first;
		}

		// A mode in the player's words: the ones in Steam's own templates that say something at a glance. Any other, as read.
		std::string ModeWords(const std::string& a_mode)
		{
			if (a_mode == "absolute_mouse") { return TR("UVCL_ModeMouse", "Mouse"); }
			if (a_mode == "scrollwheel") { return TR("UVCL_ModeScroll", "Scroll"); }
			if (a_mode == "joystick_move" || a_mode == "joystick_camera") { return TR("UVCL_ModeJoystick", "Joystick"); }
			std::string words = a_mode;
			std::replace(words.begin(), words.end(), '_', ' ');
			return words;
		}

		// The one binding of an input's press, "" for none. Only a full press: long presses and the like are left out, not guessed at.
		std::string FullPress(const Node& a_input)
		{
			const auto* activators = a_input.Child("activators");
			const auto* press = activators ? activators->Child("Full_Press") : nullptr;
			const auto* bindings = press ? press->Child("bindings") : nullptr;
			return bindings ? bindings->Value("binding") : std::string();
		}

		std::optional<rows::Button> XInputButton(const std::string& a_binding)
		{
			constexpr std::string_view kind = "xinput_button ";
			if (!a_binding.starts_with(kind)) { return std::nullopt; }
			std::string name = a_binding.substr(kind.size(), a_binding.find(',') - kind.size());
			while (!name.empty() && name.back() == ' ') { name.pop_back(); }
			name = Lower(name);
			static const std::map<std::string, rows::Button> names = {
				{ "a", rows::kA }, { "b", rows::kB }, { "x", rows::kX }, { "y", rows::kY },
				{ "shoulder_left", rows::kLB }, { "shoulder_right", rows::kRB }, { "trigger_left", rows::kLT }, { "trigger_right", rows::kRT },
				{ "start", rows::kStart }, { "select", rows::kBack },
				{ "dpad_up", rows::kDUp }, { "dpad_down", rows::kDDown }, { "dpad_left", rows::kDLeft }, { "dpad_right", rows::kDRight },
				{ "joystick_left", rows::kLStick }, { "joystick_right", rows::kRStick },
			};
			const auto it = names.find(name);
			if (it == names.end()) { return std::nullopt; }
			return it->second;
		}

		// The control a source and input stand for. -1: no place on the picture.
		int PartOf(const std::string& a_source, const std::string& a_input)
		{
			static const std::map<std::string, int> diamond = { { "button_a", rows::kA }, { "button_b", rows::kB }, { "button_x", rows::kX }, { "button_y", rows::kY } };
			static const std::map<std::string, int> dpad = { { "dpad_north", rows::kDUp }, { "dpad_south", rows::kDDown }, { "dpad_west", rows::kDLeft }, { "dpad_east", rows::kDRight } };
			static const std::map<std::string, int> leftPad = { { "dpad_north", padtables::kLeftPadUp }, { "dpad_south", padtables::kLeftPadDown }, { "dpad_west", padtables::kLeftPadLeft }, { "dpad_east", padtables::kLeftPadRight } };
			static const std::map<std::string, int> rightPad = { { "dpad_north", padtables::kRightPadUp }, { "dpad_south", padtables::kRightPadDown }, { "dpad_west", padtables::kRightPadLeft }, { "dpad_east", padtables::kRightPadRight } };
			// button_back_*: which is the upper is by name: the upper is L4 and R4 on the pad's own labels.
			static const std::map<std::string, int> sw = { { "button_escape", rows::kStart }, { "button_menu", rows::kBack }, { "left_bumper", rows::kLB }, { "right_bumper", rows::kRB },
				{ "button_back_left_upper", padtables::kL4 }, { "button_back_left", padtables::kL5 }, { "button_back_right_upper", padtables::kR4 }, { "button_back_right", padtables::kR5 } };
			const auto in = [&](const std::map<std::string, int>& a_map) { const auto it = a_map.find(a_input); return it == a_map.end() ? -1 : it->second; };
			if (a_source == "button_diamond") { return in(diamond); }
			if (a_source == "dpad") { return in(dpad); }
			if (a_source == "switch") { return in(sw); }
			if (a_source == "joystick" && a_input == "click") { return rows::kLStick; }
			if (a_source == "right_joystick" && a_input == "click") { return rows::kRStick; }
			if (a_source == "left_trigger" && a_input == "click") { return rows::kLT; }
			if (a_source == "right_trigger" && a_input == "click") { return rows::kRT; }
			if (a_source == "left_trackpad") { return a_input == "click" ? padtables::kLeftPad : in(leftPad); }
			if (a_source == "right_trackpad") { return a_input == "click" ? padtables::kRightPad : in(rightPad); }
			return -1;
		}

		TrackpadLines TrackpadOf(const Node& a_group)
		{
			TrackpadLines out;
			out.read = true;
			const std::string mode = a_group.Value("mode");
			const auto* inputs = a_group.Child("inputs");
			if (mode == "dpad")
			{
				const std::array<std::pair<const char*, std::string>, 4> dirs = { { { "dpad_north", TR("UVCL_PadUp", "Up") }, { "dpad_south", TR("UVCL_PadDown", "Down") }, { "dpad_west", TR("UVCL_PadLeft", "Left") }, { "dpad_east", TR("UVCL_PadRight", "Right") } } };
				for (const auto& [input, word] : dirs)
				{
					const auto* node = inputs ? inputs->Child(input) : nullptr;
					const std::string action = node ? ActionWords(FullPress(*node)) : std::string();
					out.lines.push_back(std::format("{}: {}", word, action.empty() ? "-" : action));
				}
				return out;
			}
			// any other mode: what it is, and what its click does if it is bound
			std::string click;
			if (const auto* node = inputs ? inputs->Child("click") : nullptr) { click = ActionWords(FullPress(*node)); }
			if (mode == "single_button")
			{
				out.lines.push_back(click.empty() ? TR("UVCL_PadNothing", "Nothing bound") : click);
				return out;
			}
			out.lines.push_back(ModeWords(mode));
			if (!click.empty()) { out.lines.push_back(std::format("{}: {}", TR("UVCL_PadClick", "Click"), click)); }
			return out;
		}

		void ReadBindings(const Node& a_config, Info& a_info)
		{
			std::map<std::string, const Node*> groups;
			for (const auto& c : a_config.children)
			{
				if (c.key == "group") { groups[c.Value("id")] = &c; }
			}
			const auto* preset = a_config.Child("preset");  // the first: a config with action layers adds more, which are not gone into
			const auto* sources = preset ? preset->Child("group_source_bindings") : nullptr;
			if (!sources)
			{
				a_info.configRead = false;
				a_info.why = "the config has no preset";
				return;
			}
			for (const auto& entry : sources->children)
			{
				std::istringstream words(entry.value);
				std::string source, state, extra;
				words >> source >> state >> extra;
				if (state != "active" || !extra.empty()) { continue; }  // inactive, or a mode shift's
				const auto it = groups.find(entry.key);
				if (it == groups.end()) { continue; }
				const Node& group = *it->second;
				if (source == "left_trackpad") { a_info.left = TrackpadOf(group); }
				if (source == "right_trackpad") { a_info.right = TrackpadOf(group); }
				const auto* inputs = group.Child("inputs");
				if (!inputs) { continue; }
				for (const auto& input : inputs->children)
				{
					const auto button = XInputButton(FullPress(input));
					const int part = PartOf(source, input.key);
					if (button && part >= 0) { a_info.senders[static_cast<std::size_t>(*button)].push_back(part); }
				}
			}
		}
	}

	std::optional<layoutsettings::Faces> PadOfType(const std::string& a_type)
	{
		// The 2026 Steam Controller's own config says controller_triton. controller_steamcontroller_gordon is the 2015 pad, with no stick
		// on the right and no picture here; controller_neptune is the Steam Deck.
		if (a_type == "controller_triton") { return layoutsettings::Faces::kSteam; }
		if (a_type == "controller_ps3" || a_type == "controller_ps4" || a_type == "controller_ps5") { return layoutsettings::Faces::kPlayStation; }
		if (a_type == "controller_xbox360" || a_type == "controller_xboxone") { return layoutsettings::Faces::kXbox; }
		return std::nullopt;
	}

	void Refresh()
	{
		Info info;
		info.steamInput = GetModuleHandleW(L"gameoverlayrenderer64.dll") != nullptr;
		if (!info.steamInput)
		{
			info.why = "Steam's overlay is not in the game, so its files say nothing about this run";
			logger::info("steam: {}", info.why);
			g_info = std::move(info);
			return;
		}
		try
		{
			const fs::path steam = SteamRoot();
			if (steam.empty())
			{
				info.why = "no SteamPath in the registry";
			}
			else
			{
				std::string log;
				(void)ReadFile(steam / "logs" / "controller.txt", log);
				ReadLogPad(log, info);
				Node config;
				if (FindConfig(steam, log, ActiveAccount(), info.logPad, config, info.configType))
				{
					info.configRead = true;
					ReadBindings(config, info);
				}
				else
				{
					info.why = "no config for this game was found for the pad";
				}
			}
		}
		catch (const std::exception& a_error)
		{
			info = Info{ true };
			info.why = std::format("reading failed: {}", a_error.what());
		}
		if (info.configRead) { logger::info("steam: {}; the game's config read, {}", info.logWhy.empty() ? "no word from the log" : info.logWhy, info.configType); }
		else { logger::info("steam: {}; {}", info.logWhy.empty() ? "no word from the log" : info.logWhy, info.why); }
		g_info = std::move(info);
	}

	const Info& Current() { return g_info; }
}
