#include "PCH.h"

#include "Functions.h"

#include "Unbinder.h"
#include "utils/Logger.h"

#include "REX/W32/KERNEL32.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <format>
#include <fstream>
#include <mutex>

namespace functions
{
	namespace
	{
		std::mutex g_lock;
		std::vector<Function> g_functions;
		std::string g_dataPath;  // the game's Data folder; "" until Install()

		bool IEquals(std::string_view a_lhs, std::string_view a_rhs)
		{
			return a_lhs.size() == a_rhs.size() &&
			       std::equal(a_lhs.begin(), a_lhs.end(), a_rhs.begin(),
			                  [](unsigned char a, unsigned char b) { return std::tolower(a) == std::tolower(b); });
		}

		std::string Trim(std::string_view a_text)
		{
			const auto first = a_text.find_first_not_of(" \t\r\n");
			if (first == std::string_view::npos) { return {}; }
			const auto last = a_text.find_last_not_of(" \t\r\n");
			return std::string(a_text.substr(first, last - first + 1));
		}

		bool IsSectionHeader(std::string_view a_line)
		{
			return a_line.size() >= 2 && a_line.front() == '[' && a_line.back() == ']';
		}

		// AMF's reserved keys, asked of whichever framework DLL is loaded - the same runtime question Unbinder asks
		// before applying a [Bound] line, so a function row cannot take the key that opens the menu either.
		bool IsAmfReserved(std::uint16_t a_key)
		{
			using func_t = std::uint32_t (*)(std::int32_t*, std::uint32_t);
			for (const char* dll : { "ApocryphaMenuFramework.dll", "SKSEMenuFramework.dll" })
			{
				const auto module = REX::W32::GetModuleHandleA(dll);
				if (!module) { continue; }
				const auto func = static_cast<func_t>(REX::W32::GetProcAddress(module, "SMF_GetReservedKeyCodes"));
				if (!func) { continue; }
				std::int32_t codes[32]{};
				const std::uint32_t count = func(codes, static_cast<std::uint32_t>(std::size(codes)));
				for (std::uint32_t i = 0; i < count && i < std::size(codes); ++i)
				{
					if (codes[i] == static_cast<std::int32_t>(a_key)) { return true; }
				}
				return false;
			}
			return false;
		}

		// Writes key=value inside a_section of a_lines, adding the section or the key when either is missing, and
		// leaving every other line of the file exactly as it was. The target file belongs to another mod: a rewrite
		// that reordered or dropped its comments would be a change this mod has no business making.
		bool WriteKey(std::vector<std::string>& a_lines, const std::string& a_section, const std::string& a_key, const std::string& a_value)
		{
			const std::string header = "[" + a_section + "]";
			std::size_t sectionAt = a_lines.size();
			for (std::size_t i = 0; i < a_lines.size(); ++i)
			{
				if (IEquals(Trim(a_lines[i]), header)) { sectionAt = i; break; }
			}
			if (sectionAt == a_lines.size())
			{
				if (!a_lines.empty() && !Trim(a_lines.back()).empty()) { a_lines.push_back(""); }
				a_lines.push_back(header);
				a_lines.push_back(a_key + "=" + a_value);
				return true;
			}
			for (std::size_t i = sectionAt + 1; i < a_lines.size(); ++i)
			{
				const std::string trimmed = Trim(a_lines[i]);
				if (IsSectionHeader(trimmed))
				{
					a_lines.insert(a_lines.begin() + static_cast<std::ptrdiff_t>(i), a_key + "=" + a_value);
					return true;
				}
				if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#') { continue; }
				const auto eq = trimmed.find('=');
				if (eq == std::string::npos) { continue; }
				if (IEquals(Trim(trimmed.substr(0, eq)), a_key))
				{
					a_lines[i] = a_key + "=" + a_value;
					return true;
				}
			}
			a_lines.push_back(a_key + "=" + a_value);
			return true;
		}

		// The binding the Controls page shows for a family. Keyboard and mouse share one column there, so the keyboard
		// binding wins and the mouse one is shown when the keyboard has none.
		Binding ShownFor(const Function& a_function, bool a_gamepad)
		{
			if (a_gamepad) { return a_function.bind[2]; }
			if (a_function.bind[0].key != kUnbound) { return a_function.bind[0]; }
			return a_function.bind[1];
		}

		// A target path carries backslashes, which are an escape in JSON - the DevBench tool's reader rejects the
		// state object if they go out raw.
		std::string JsonText(std::string_view a_text)
		{
			std::string out;
			out.reserve(a_text.size());
			for (const char c : a_text)
			{
				if (c == '\\' || c == '"') { out += '\\'; }
				out += c;
			}
			return out;
		}

		std::string KeyText(std::uint16_t a_key, int a_device)
		{
			if (a_key == kUnbound) { return {}; }
			const char* name = unbinder::ButtonName(a_key, a_device);
			if (name && name[0]) { return name; }
			return std::format("0x{:02x}", a_key);
		}
	}

	std::vector<Function> GetFunctions()
	{
		std::scoped_lock l(g_lock);
		return g_functions;
	}

	void SetFunctions(std::vector<Function> a_functions)
	{
		std::scoped_lock l(g_lock);
		g_functions = std::move(a_functions);
	}

	// One Click Power Attack already fires the power attack and already reads a key and a modifier; what it has no
	// place for is a player setting them where a player looks. MCM Helper keeps a mod's chosen values in
	// Data\MCM\Settings\<mod>.ini, which is the file OCPA reads over its shipped defaults, so that is what is written.
	// Shipped unbound: a row that arrived holding a button would take that button from whatever the player has on it.
	std::vector<Function> DefaultFunctions()
	{
		std::vector<Function> out;

		Function powerAttack;
		powerAttack.name = "Power Attack";
		powerAttack.target = { "MCM\\Settings\\OCPA.ini", "General", "", "iKeycode", "iModifierKey", -1 };
		out.push_back(std::move(powerAttack));

		// Stances (the owner, 2026-09-16: "we need to make a way for uvc to register stances then", then, after seeing
		// four rows: "the stances should just have a single Kiro that switches between stances when when toggled not a
		// bunch of different key rows"). So ONE row, not four. Stances NG has a cycling mode of its own - bUseCycling,
		// where the MID stance key steps through the stances - so the row drives that key and the other three are left
		// switched off. Its "off" value is 0, not -1 ("0 is to deactivate the key entirely"), which is why the no-key
		// value belongs to the target rather than to this mod.
		Function stance;
		stance.name = "Stance";
		stance.target = { "SKSE\\Plugins\\StancesNG.ini", "Keys", "", "iMidStanceKey", "iModifierKeyMidStance", 0 };
		out.push_back(std::move(stance));

		// The wheel (the owner, 2026-09-16: "i want to add a wheeler row that says wheel menu. and overwrites
		// whatever is currently bound to our wheeler mod"). Wheeler keeps the key per DEVICE, so the row does too:
		// the gamepad Controls page drives [InputBindings.GamePad] and the keyboard page [InputBindings.MKB],
		// independently, which is what he asked for. Its "no key" value is 0, as the rest of that file uses.
		Function wheel;
		wheel.name = "Wheel Menu";
		wheel.target = { "SKSE\\Plugins\\wheeler\\Controls.ini", "InputBindings.MKB", "InputBindings.GamePad",
						 "toggleWheel", "toggleWheelModifier", 0 };
		out.push_back(std::move(wheel));

		return out;
	}

	bool IsFunctionRow(std::string_view a_name, std::size_t* a_index)
	{
		std::scoped_lock l(g_lock);
		for (std::size_t i = 0; i < g_functions.size(); ++i)
		{
			if (IEquals(g_functions[i].name, a_name))
			{
				if (a_index) { *a_index = i; }
				return true;
			}
		}
		return false;
	}

	Binding ShownBinding(std::size_t a_index, bool a_gamepad)
	{
		std::scoped_lock l(g_lock);
		if (a_index >= g_functions.size()) { return {}; }
		return ShownFor(g_functions[a_index], a_gamepad);
	}

	std::string ShownText(std::size_t a_index, bool a_gamepad)
	{
		std::scoped_lock l(g_lock);
		if (a_index >= g_functions.size()) { return {}; }
		const Binding b = ShownFor(g_functions[a_index], a_gamepad);
		if (b.key == kUnbound) { return {}; }
		const int device = a_gamepad ? 2 : (g_functions[a_index].bind[0].key != kUnbound ? 0 : 1);
		const std::string key = KeyText(b.key, device);
		if (b.modifier == 0) { return key; }
		return KeyText(b.modifier, device) + " + " + key;
	}

	bool Bind(std::size_t a_index, int a_device, std::uint16_t a_key, std::string& a_why)
	{
		std::scoped_lock l(g_lock);
		if (a_index >= g_functions.size() || a_device < 0 || a_device > 2) { a_why = "no such row"; return false; }
		if (a_device == 0 && IsAmfReserved(a_key))
		{
			a_why = "AMF reserves that key";
			return false;
		}
		for (std::size_t i = 0; i < g_functions.size(); ++i)
		{
			if (i == a_index) { continue; }
			const auto& other = g_functions[i].bind[a_device];
			if (other.key == a_key && other.modifier == g_functions[a_index].bind[a_device].modifier)
			{
				a_why = std::format("\"{}\" already holds it", g_functions[i].name);
				return false;
			}
		}
		g_functions[a_index].bind[a_device].key = a_key;
		return true;
	}

	bool Unbind(std::size_t a_index, int a_device)
	{
		std::scoped_lock l(g_lock);
		if (a_index >= g_functions.size() || a_device < 0 || a_device > 2) { return false; }
		g_functions[a_index].bind[a_device] = {};
		return true;
	}

	bool HoldsKey(std::size_t a_index, int a_device, std::uint16_t a_key)
	{
		std::scoped_lock l(g_lock);
		if (a_index >= g_functions.size() || a_device < 0 || a_device > 2) { return false; }
		return g_functions[a_index].bind[a_device].key == a_key && a_key != kUnbound;
	}

	// SKSE's Input Script numbering (SKSE::InputMap): 0-255 keyboard DX scan codes, 256-263 mouse buttons,
	// 266-281 gamepad buttons. The gamepad half is an ORDER, not the XInput mask, so it is converted rather than
	// cast - the mask is what controlmap.txt and the ControlMap use, the order is what an SKSE mod's settings use.
	int ToInputCode(std::uint16_t a_key, int a_device)
	{
		if (a_key == kUnbound) { return -1; }
		switch (a_device)
		{
		case 0:
			return static_cast<int>(a_key);
		case 1:
			return SKSE::InputMap::kMacro_MouseButtonOffset + static_cast<int>(a_key);
		case 2:
			return static_cast<int>(SKSE::InputMap::GamepadMaskToKeycode(a_key));
		default:
			return -1;
		}
	}

	std::uint16_t FromInputCode(int a_code, int& a_device)
	{
		if (a_code < 0) { a_device = -1; return kUnbound; }
		if (a_code < SKSE::InputMap::kMacro_NumKeyboardKeys)
		{
			a_device = 0;
			return static_cast<std::uint16_t>(a_code);
		}
		if (a_code < SKSE::InputMap::kMacro_MouseWheelOffset)
		{
			a_device = 1;
			return static_cast<std::uint16_t>(a_code - SKSE::InputMap::kMacro_MouseButtonOffset);
		}
		if (a_code >= SKSE::InputMap::kMacro_GamepadOffset && a_code < SKSE::InputMap::kMaxMacros)
		{
			a_device = 2;
			return static_cast<std::uint16_t>(SKSE::InputMap::GamepadKeycodeToMask(static_cast<std::uint32_t>(a_code)));
		}
		a_device = -1;
		return kUnbound;
	}

	// Reads one key out of a section of an INI on disk. "" when the file, the section or the key is not there.
	namespace
	{
		std::string ReadKey(const std::filesystem::path& a_path, const std::string& a_section, const std::string& a_key)
		{
			std::ifstream in(a_path, std::ios::binary);
			if (!in) { return {}; }
			std::string line;
			bool inside = false;
			bool first = true;
			while (std::getline(in, line))
			{
				if (!line.empty() && line.back() == '\r') { line.pop_back(); }
				if (first && line.rfind("\xEF\xBB\xBF", 0) == 0) { line.erase(0, 3); }
				first = false;
				const std::string t = Trim(line);
				if (IsSectionHeader(t))
				{
					inside = IEquals(t.substr(1, t.size() - 2), a_section);
					continue;
				}
				if (!inside || t.empty() || t[0] == ';' || t[0] == '#') { continue; }
				const auto eq = t.find('=');
				if (eq == std::string::npos) { continue; }
				if (IEquals(Trim(t.substr(0, eq)), a_key)) { return Trim(t.substr(eq + 1)); }
			}
			return {};
		}
	}

	int Adopt(const char* a_reason)
	{
		if (g_dataPath.empty()) { return 0; }
		std::vector<Function> snapshot;
		{
			std::scoped_lock l(g_lock);
			snapshot = g_functions;
		}

		int adopted = 0;
		for (std::size_t i = 0; i < snapshot.size(); ++i)
		{
			const auto& f = snapshot[i];
			if (f.target.file.empty() || f.target.key.empty()) { continue; }
			// A SPLIT target is adopted PER DEVICE, because the INI stores only one device's binding per row: the
			// save writes the first device that is bound, so a row holding both a keyboard and a gamepad key loses
			// one of them on the next save. Re-adopting the missing side from the target on every load is what
			// makes that lossless - and it is free, because the target file is where that value lives anyway.
			// (Seen live 2026-09-16: the Wheel Menu row adopted D-pad Down and B, saved only B, and the gamepad
			// half then came back as "unbound" and wrote -1 over Wheeler's key.)
			//
			// A single-key target still adopts only when the whole row is unbound - there is one value, so one
			// binding, and adopting over a deliberate choice would undo it.
			const bool bound = std::any_of(f.bind.begin(), f.bind.end(), [](const Binding& a_b) { return a_b.key != kUnbound; });
			if (bound && f.target.gamepadSection.empty()) { continue; }

			const std::filesystem::path path = std::filesystem::path(g_dataPath) / f.target.file;

			// A split target holds one key per device, so each section is read into the device it belongs to and
			// the row can end up bound on both. A single-key target keeps the old behaviour: whichever device the
			// number decodes to is the one it lands on.
			if (!f.target.gamepadSection.empty())
			{
				bool took = false;
				for (const auto& [sectionName, forDevice] : { std::pair<const std::string&, int>{ f.target.gamepadSection, 2 },
															  std::pair<const std::string&, int>{ f.target.section, 0 } })
				{
					if (sectionName.empty()) { continue; }
					// This device already has a binding of its own - the INI kept it, so it is not missing.
					{
						std::scoped_lock l(g_lock);
						if (i < g_functions.size() && g_functions[i].bind[forDevice].key != kUnbound) { continue; }
					}
					const std::string raw = ReadKey(path, sectionName, f.target.key);
					if (raw.empty()) { continue; }
					int code = f.target.none;
					try { code = std::stoi(raw); } catch (...) { continue; }
					if (code == f.target.none) { continue; }
					int decoded = -1;
					const std::uint16_t key = FromInputCode(code, decoded);
					if (key == kUnbound) { continue; }
					// The SECTION says which device it is; the number only has to give the button.
					std::uint16_t modifier = 0;
					if (!f.target.modifierKey.empty())
					{
						const std::string modRaw = ReadKey(path, sectionName, f.target.modifierKey);
						int modCode = f.target.none;
						try { modCode = std::stoi(modRaw); } catch (...) { modCode = f.target.none; }
						if (modCode != f.target.none)
						{
							int ignored = -1;
							const std::uint16_t parsed = FromInputCode(modCode, ignored);
							if (parsed != kUnbound) { modifier = parsed; }
						}
					}
					{
						std::scoped_lock l(g_lock);
						if (i >= g_functions.size()) { continue; }
						g_functions[i].bind[forDevice].key = key;
						g_functions[i].bind[forDevice].modifier = modifier;
					}
					took = true;
					const char* name = unbinder::ButtonName(key, forDevice);
					logger::info("functions ({}): \"{}\" took the {} key the target already had - {} (code {})", a_reason, f.name,
								 unbinder::DeviceName(forDevice), (name && name[0]) ? name : "?", code);
				}
				if (took) { ++adopted; }
				continue;
			}

			const std::string value = ReadKey(path, f.target.section, f.target.key);
			if (value.empty()) { continue; }
			int code = f.target.none;
			try { code = std::stoi(value); } catch (...) { continue; }
			if (code == f.target.none) { continue; }
			int device = -1;
			const std::uint16_t key = FromInputCode(code, device);
			if (device < 0 || device > 2 || key == kUnbound) { continue; }

			std::uint16_t modifier = 0;
			if (!f.target.modifierKey.empty())
			{
				const std::string modValue = ReadKey(path, f.target.section, f.target.modifierKey);
				int modCode = f.target.none;
				try { modCode = std::stoi(modValue); } catch (...) { modCode = f.target.none; }
				if (modCode != f.target.none)
				{
					int modDevice = -1;
					const std::uint16_t parsed = FromInputCode(modCode, modDevice);
					if (parsed != kUnbound) { modifier = parsed; }
				}
			}

			{
				std::scoped_lock l(g_lock);
				if (i >= g_functions.size()) { continue; }
				g_functions[i].bind[device].key = key;
				g_functions[i].bind[device].modifier = modifier;
			}
			++adopted;
			const char* name = unbinder::ButtonName(key, device);
			logger::info("functions ({}): \"{}\" took the key the target already had - {} {} (code {}){}", a_reason, f.name,
						 unbinder::DeviceName(device), (name && name[0]) ? name : "?", code,
						 modifier ? std::format(", modifier {}", unbinder::ButtonName(modifier, device)) : "");
		}
		return adopted;
	}

	int Deliver(const char* a_reason, bool a_writeUnbound)
	{
		std::vector<Function> snapshot;
		{
			std::scoped_lock l(g_lock);
			snapshot = g_functions;
		}
		if (snapshot.empty()) { return 0; }
		if (g_dataPath.empty())
		{
			logger::warn("functions ({}): the game's Data folder is not known yet; nothing delivered", a_reason);
			return 0;
		}

		// Several rows can share one target file, so the file is read once, every row that writes into it is applied,
		// and it is written once - otherwise the second row would be applied to a copy read before the first.
		// A row bound to nothing is left alone unless the caller says otherwise: it would write the target's "none"
		// value over a setting the target mod's own menu put there, which is how the first load of this feature wiped
		// One Click Power Attack's key (2026-09-16). Adopt() is what fills such a row in instead.
		if (!a_writeUnbound)
		{
			std::erase_if(snapshot, [](const Function& a_f) {
				return std::none_of(a_f.bind.begin(), a_f.bind.end(), [](const Binding& a_b) { return a_b.key != kUnbound; });
			});
			if (snapshot.empty()) { return 0; }
		}

		std::vector<std::string> files;
		for (const auto& f : snapshot)
		{
			if (f.target.file.empty() || f.target.key.empty()) { continue; }
			if (std::none_of(files.begin(), files.end(), [&](const std::string& a_f) { return IEquals(a_f, f.target.file); })) { files.push_back(f.target.file); }
		}

		int written = 0;
		for (const auto& file : files)
		{
			const std::filesystem::path path = std::filesystem::path(g_dataPath) / file;
			std::vector<std::string> lines;
			bool bom = false;
			{
				std::ifstream in(path, std::ios::binary);
				std::string line;
				while (in && std::getline(in, line))
				{
					if (!line.empty() && line.back() == '\r') { line.pop_back(); }
					// MCM Helper writes its settings files with a UTF-8 BOM. Left on the first line it becomes part
					// of that line's text, so "[General]" is not recognised as a section header and a SECOND
					// [General] would be appended to a file that already had one. Stripped for the parse and put
					// back on write, because the file belongs to another mod and its encoding is not ours to change.
					if (lines.empty() && line.rfind("\xEF\xBB\xBF", 0) == 0)
					{
						bom = true;
						line.erase(0, 3);
					}
					lines.push_back(line);
				}
			}
			const bool existed = !lines.empty();
			const std::vector<std::string> before = lines;

			std::string applied;
			for (const auto& f : snapshot)
			{
				if (!IEquals(f.target.file, file) || f.target.key.empty()) { continue; }
				// One row, one value: the device that has a key wins, keyboard first, because the target setting is a
				// single key and the SKSE numbering puts all three devices in the same space.
				// Split target: each device writes into its own section and leaves the other alone, so the
				// gamepad Controls page drives the mod's gamepad binding and the keyboard page its keyboard one.
				if (!f.target.gamepadSection.empty())
				{
					for (const auto& [sectionName, forDevice] : { std::pair<const std::string&, int>{ f.target.gamepadSection, 2 },
																  std::pair<const std::string&, int>{ f.target.section, 0 } })
					{
						if (sectionName.empty()) { continue; }
						const auto& bound = f.bind[forDevice];
						const int code = bound.key == kUnbound ? f.target.none : ToInputCode(bound.key, forDevice);
						WriteKey(lines, sectionName, f.target.key, std::to_string(code));
						// The modifier is written on EVERY delivery, so a wrong "no key" value corrupts it even when
						// the row is bound - which is how Wheeler's toggleWheelModifier ended up -1 where it wanted 0
						// and the wheel stopped opening (2026-09-16). It is logged for the same reason: the key was
						// visible in the log and the modifier was not, so the damage was invisible.
						int modifierWritten = f.target.none;
						if (!f.target.modifierKey.empty())
						{
							modifierWritten = (bound.key != kUnbound && bound.modifier != 0) ? ToInputCode(bound.modifier, forDevice) : f.target.none;
							WriteKey(lines, sectionName, f.target.modifierKey, std::to_string(modifierWritten));
						}
						applied += std::format("{}\"{}\" {} -> key {} modifier {}", applied.empty() ? "" : ", ", f.name,
											   unbinder::DeviceName(forDevice), code, modifierWritten);
					}
					continue;
				}

				int code = f.target.none;
				int device = -1;
				for (int d : { 0, 1, 2 })
				{
					if (f.bind[d].key != kUnbound) { code = ToInputCode(f.bind[d].key, d); device = d; break; }
				}
				WriteKey(lines, f.target.section, f.target.key, std::to_string(code));
				int modifierWritten = f.target.none;
				if (!f.target.modifierKey.empty())
				{
					modifierWritten = (device >= 0 && f.bind[device].modifier != 0) ? ToInputCode(f.bind[device].modifier, device) : f.target.none;
					WriteKey(lines, f.target.section, f.target.modifierKey, std::to_string(modifierWritten));
				}
				applied += std::format("{}\"{}\" -> key {} modifier {}", applied.empty() ? "" : ", ", f.name, code, modifierWritten);
			}

			// Nothing to say that the file does not already say: leave another mod's file alone rather than rewrite
			// it to the byte. Delivery runs at every data load, and a rewrite that changes nothing still restyles
			// that mod's spacing and rewrites its timestamp.
			if (lines == before)
			{
				logger::debug("functions ({}): {} already says what the Controls page shows; not rewritten [{}]", a_reason, path.string(), applied);
				continue;
			}

			std::error_code ec;
			std::filesystem::create_directories(path.parent_path(), ec);
			std::ofstream out(path, std::ios::binary | std::ios::trunc);
			if (!out)
			{
				logger::error("functions ({}): could not write {}", a_reason, path.string());
				continue;
			}
			// The BOM the file arrived with goes back on: the file belongs to another mod (MCM Helper writes its
			// settings with one) and its encoding is not ours to change.
			if (bom) { out << "\xEF\xBB\xBF"; }
			for (const auto& line : lines) { out << line << "\r\n"; }
			++written;
			logger::info("functions ({}): {} {} [{}]", a_reason, existed ? "updated" : "created", path.string(), applied);
		}
		return written;
	}

	void Install()
	{
		std::error_code ec;
		const auto data = std::filesystem::current_path(ec) / "Data";
		if (ec)
		{
			logger::warn("functions: the working folder is unknown; rows cannot be delivered");
			return;
		}
		g_dataPath = data.string();
		logger::debug("functions: Data is {}", g_dataPath);
	}

	std::string StateJson()
	{
		std::scoped_lock l(g_lock);
		std::string out = "\"functions\":[";
		for (std::size_t i = 0; i < g_functions.size(); ++i)
		{
			const auto& f = g_functions[i];
			out += std::format(
				"{}{{\"name\":\"{}\",\"file\":\"{}\",\"section\":\"{}\",\"key\":\"{}\",\"modifierKey\":\"{}\"",
				i ? "," : "", JsonText(f.name), JsonText(f.target.file), JsonText(f.target.section), JsonText(f.target.key), JsonText(f.target.modifierKey));
			for (int d = 0; d < 3; ++d)
			{
				out += std::format(",\"{}\":{{\"key\":{},\"modifier\":{},\"code\":{}}}", unbinder::DeviceName(d),
								   f.bind[d].key, f.bind[d].modifier, ToInputCode(f.bind[d].key, d));
			}
			out += "}";
		}
		out += "]";
		return out;
	}
}
