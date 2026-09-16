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
		Function powerAttack;
		powerAttack.name = "Power Attack";
		powerAttack.target = { "MCM\\Settings\\OCPA.ini", "General", "iKeycode", "iModifierKey", -1 };
		return { powerAttack };
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

	int Deliver(const char* a_reason)
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
			{
				std::ifstream in(path);
				std::string line;
				while (in && std::getline(in, line))
				{
					if (!line.empty() && line.back() == '\r') { line.pop_back(); }
					lines.push_back(line);
				}
			}
			const bool existed = !lines.empty();

			std::string applied;
			for (const auto& f : snapshot)
			{
				if (!IEquals(f.target.file, file) || f.target.key.empty()) { continue; }
				// One row, one value: the device that has a key wins, keyboard first, because the target setting is a
				// single key and the SKSE numbering puts all three devices in the same space.
				int code = f.target.none;
				int device = -1;
				for (int d : { 0, 1, 2 })
				{
					if (f.bind[d].key != kUnbound) { code = ToInputCode(f.bind[d].key, d); device = d; break; }
				}
				WriteKey(lines, f.target.section, f.target.key, std::to_string(code));
				if (!f.target.modifierKey.empty())
				{
					const int modifier = (device >= 0 && f.bind[device].modifier != 0) ? ToInputCode(f.bind[device].modifier, device) : f.target.none;
					WriteKey(lines, f.target.section, f.target.modifierKey, std::to_string(modifier));
				}
				applied += std::format("{}\"{}\" -> {}", applied.empty() ? "" : ", ", f.name, code);
			}

			std::error_code ec;
			std::filesystem::create_directories(path.parent_path(), ec);
			std::ofstream out(path, std::ios::binary | std::ios::trunc);
			if (!out)
			{
				logger::error("functions ({}): could not write {}", a_reason, path.string());
				continue;
			}
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
