#include "PCH.h"

#include "Settings.h"

#include "Unbinder.h"
#include "utils/INISettingCollection.h"
#include "utils/Logger.h"
#include "utils/Setting.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace settings
{
	namespace
	{
		std::string iniPath;

		struct Defaults
		{
			std::uint32_t logLevel;
			bool enabled;
		} defaults{};

		std::string Lower(std::string a_s)
		{
			for (char& c : a_s) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
			return a_s;
		}

		std::string Trim(const std::string& a_s)
		{
			const auto b = a_s.find_first_not_of(" \t\r\n");
			if (b == std::string::npos) { return {}; }
			const auto e = a_s.find_last_not_of(" \t\r\n");
			return a_s.substr(b, e - b + 1);
		}

		bool ParseBool(const std::string& a_text, bool& a_out)
		{
			const std::string v = Lower(Trim(a_text));
			if (v == "1" || v == "true" || v == "yes") { a_out = true; return true; }
			if (v == "0" || v == "false" || v == "no") { a_out = false; return true; }
			return false;
		}

		bool ParseUInt(const std::string& a_text, std::uint32_t& a_out)
		{
			try { a_out = static_cast<std::uint32_t>(std::stoull(Trim(a_text), nullptr, 0)); return true; } catch (...) { return false; }
		}

		bool IsSectionHeader(const std::string& a_t) { return a_t.size() >= 2 && a_t.front() == '[' && a_t.back() == ']'; }

		// "0x0f/0xff,0x4f/0xff" -> pairs. A bare "0x0f" means modifier 0xff.
		bool ParseOriginals(const std::string& a_text, std::vector<unbinder::KeyPair>& a_out)
		{
			a_out.clear();
			std::string t = Trim(a_text);
			if (t.empty()) { return true; }  // no remembered key: captured at the first apply
			std::size_t pos = 0;
			while (pos <= t.size())
			{
				auto comma = t.find(',', pos);
				if (comma == std::string::npos) { comma = t.size(); }
				const std::string item = Trim(t.substr(pos, comma - pos));
				pos = comma + 1;
				if (item.empty()) { continue; }
				unbinder::KeyPair kp;
				const auto slash = item.find('/');
				try
				{
					kp.key = static_cast<std::uint16_t>(std::stoul(item.substr(0, slash), nullptr, 0));
					kp.modifier = slash == std::string::npos ? 0xFF : static_cast<std::uint16_t>(std::stoul(item.substr(slash + 1), nullptr, 0));
				}
				catch (...) { return false; }
				a_out.push_back(kp);
			}
			return true;
		}

		// Reads scalars into the key:section map and the [Unbound] lines into entries.
		void ReadFile(std::map<std::string, std::string>& a_keys, std::vector<unbinder::Entry>& a_entries, int& a_badLines)
		{
			std::ifstream in(iniPath);
			if (!in) { return; }
			std::string line, section;
			while (std::getline(in, line))
			{
				const std::string t = Trim(line);
				if (t.empty() || t[0] == ';' || t[0] == '#') { continue; }
				if (IsSectionHeader(t)) { section = Lower(t.substr(1, t.size() - 2)); continue; }
				const auto eq = t.find('=');
				if (eq == std::string::npos) { continue; }
				const std::string key = Trim(t.substr(0, eq));
				const std::string value = Trim(t.substr(eq + 1));
				if (section == "unbound")
				{
					// Context|Control|Device
					const auto p1 = key.find('|');
					const auto p2 = p1 == std::string::npos ? std::string::npos : key.find('|', p1 + 1);
					if (p1 == std::string::npos || p2 == std::string::npos) { ++a_badLines; logger::warn("INI [Unbound] line \"{}\" is not Context|Control|Device; ignored", key); continue; }
					unbinder::Entry e;
					e.context = Trim(key.substr(0, p1));
					e.event = Trim(key.substr(p1 + 1, p2 - p1 - 1));
					e.device = unbinder::DeviceIndex(Trim(key.substr(p2 + 1)));
					if (unbinder::ContextIndex(e.context) < 0) { ++a_badLines; logger::warn("INI [Unbound] line \"{}\": unknown context \"{}\"; ignored", key, e.context); continue; }
					if (e.device < 0) { ++a_badLines; logger::warn("INI [Unbound] line \"{}\": unknown device; ignored (keyboard, mouse or gamepad)", key); continue; }
					if (!ParseOriginals(value, e.original)) { ++a_badLines; logger::warn("INI [Unbound] line \"{}\": value \"{}\" is not a key list; the key will be captured at the next apply", key, value); e.original.clear(); }
					const bool dup = std::any_of(a_entries.begin(), a_entries.end(), [&](const unbinder::Entry& o) { return o.device == e.device && Lower(o.context) == Lower(e.context) && Lower(o.event) == Lower(e.event); });
					if (dup) { logger::warn("INI [Unbound] line \"{}\" repeats an earlier line; ignored", key); continue; }
					a_entries.push_back(std::move(e));
					continue;
				}
				a_keys[Lower(key) + ":" + section] = value;
			}
		}

		bool LoadFileValues()
		{
			if (!std::filesystem::exists(iniPath))
			{
				logger::warn("INI not found at {}; keeping compiled defaults and the shipped list ({} entries)", iniPath, unbinder::GetEntries().size());
				return false;
			}
			std::map<std::string, std::string> k;
			std::vector<unbinder::Entry> entries;
			int bad = 0;
			ReadFile(k, entries, bad);
			auto get = [&](const char* a_key, auto& a_out, auto a_parse) {
				const auto it = k.find(a_key);
				if (it == k.end()) { logger::debug("INI key {} missing; keeping current value", a_key); return; }
				if (!a_parse(it->second, a_out)) { logger::warn("INI value \"{}\" for {} is not valid; keeping current value", it->second, a_key); }
			};
			get("uloglevel:debug", debug::logLevel, ParseUInt);
			get("benabled:general", general::enabled, ParseBool);
			unbinder::SetEntries(std::move(entries));
			logger::info("settings loaded from {}: enabled={} logLevel={} unbound entries={}{}", iniPath, general::enabled, debug::logLevel,
						 unbinder::GetEntries().size(), bad ? std::format(" ({} bad line(s) ignored)", bad) : "");
			return true;
		}

		bool WriteKey(std::vector<std::string>& a_lines, const char* a_section, const char* a_key, const std::string& a_value)
		{
			const std::string wantSection = Lower(a_section);
			const std::string wantKey = Lower(a_key);
			std::string section;
			for (auto& line : a_lines)
			{
				const std::string t = Trim(line);
				if (IsSectionHeader(t)) { section = Lower(t.substr(1, t.size() - 2)); continue; }
				const auto eq = t.find('=');
				if (eq == std::string::npos || section != wantSection) { continue; }
				if (Lower(Trim(t.substr(0, eq))) == wantKey)
				{
					line = std::string(a_key) + "=" + a_value;
					return true;
				}
			}
			logger::warn("Save: key {} not found in [{}]", a_key, a_section);
			return false;
		}

		std::string EntryLine(const unbinder::Entry& a_e)
		{
			std::string v;
			for (const auto& k : a_e.original)
			{
				if (!v.empty()) { v += ","; }
				v += std::format("0x{:02x}/0x{:02x}", k.key, k.modifier);
			}
			return std::format("{}|{}|{}={}", a_e.context, a_e.event, unbinder::DeviceName(a_e.device), v);
		}

		// Rewrites the [Unbound] section: its header and leading comment lines are kept, every old
		// key line is dropped, the current list is written in their place. A missing section is
		// appended.
		void WriteUnbound(std::vector<std::string>& a_lines, const std::vector<unbinder::Entry>& a_entries)
		{
			std::size_t header = a_lines.size();
			for (std::size_t i = 0; i < a_lines.size(); ++i)
			{
				const std::string t = Trim(a_lines[i]);
				if (IsSectionHeader(t) && Lower(t) == "[unbound]") { header = i; break; }
			}
			std::vector<std::string> fresh;
			for (const auto& e : a_entries) { fresh.push_back(EntryLine(e)); }
			if (header == a_lines.size())
			{
				if (!a_lines.empty() && !Trim(a_lines.back()).empty()) { a_lines.push_back(""); }
				a_lines.push_back("[Unbound]");
				a_lines.push_back("; One line per control this mod leaves without a key: Context|Control|Device = the key(s) the game had there.");
				a_lines.insert(a_lines.end(), fresh.begin(), fresh.end());
				return;
			}
			// Keep comments and blanks directly under the header; drop key lines up to the next section.
			std::size_t keep = header + 1;
			while (keep < a_lines.size())
			{
				const std::string t = Trim(a_lines[keep]);
				if (IsSectionHeader(t)) { break; }
				if (t.empty() || t[0] == ';' || t[0] == '#') { ++keep; continue; }
				break;
			}
			std::size_t next = keep;
			while (next < a_lines.size() && !IsSectionHeader(Trim(a_lines[next]))) { ++next; }
			a_lines.erase(a_lines.begin() + static_cast<std::ptrdiff_t>(keep), a_lines.begin() + static_cast<std::ptrdiff_t>(next));
			a_lines.insert(a_lines.begin() + static_cast<std::ptrdiff_t>(keep), fresh.begin(), fresh.end());
			const std::size_t after = keep + fresh.size();
			if (after < a_lines.size() && IsSectionHeader(Trim(a_lines[after]))) { a_lines.insert(a_lines.begin() + static_cast<std::ptrdiff_t>(after), ""); }
		}
	}

	void Init(const std::string& a_iniFileName)
	{
		iniPath = (std::filesystem::current_path() / "Data" / "SKSE" / "Plugins" / a_iniFileName).string();

		defaults = { debug::logLevel, general::enabled };
		// Compiled default list = the shipped INI's [Unbound] lines (rule 16). An INI that exists
		// replaces it with whatever it lists, including nothing.
		unbinder::SetEntries(unbinder::DefaultEntries());

		auto* collection = utils::INISettingCollection::GetSingleton();
		collection->AddSettings(
			utils::MakeSetting("uLogLevel:Debug", static_cast<unsigned int>(debug::logLevel)),
			utils::MakeSetting("bEnabled:General", general::enabled));

		LoadFileValues();
	}

	bool Reload()
	{
		const bool ok = LoadFileValues();
		ApplyLogLevel();
		return ok;
	}

	bool Save()
	{
		std::vector<std::string> lines;
		{
			std::ifstream in(iniPath);
			if (!in) { logger::error("Save: could not open {} for reading", iniPath); return false; }
			std::string line;
			while (std::getline(in, line)) { lines.push_back(line); }
		}

		bool ok = true;
		ok &= WriteKey(lines, "Debug", "uLogLevel", std::to_string(debug::logLevel));
		ok &= WriteKey(lines, "General", "bEnabled", general::enabled ? "1" : "0");
		const auto entries = unbinder::GetEntries();
		WriteUnbound(lines, entries);

		std::ofstream out(iniPath, std::ios::trunc);
		if (!out) { logger::error("Save: could not open {} for writing", iniPath); return false; }
		for (const auto& line : lines) { out << line << '\n'; }
		logger::info("settings saved to {} ({} unbound entr{})", iniPath, entries.size(), entries.size() == 1 ? "y" : "ies");
		return ok;
	}

	void RestoreDefaults()
	{
		debug::logLevel = defaults.logLevel;
		general::enabled = defaults.enabled;
		ApplyLogLevel();
	}

	void ApplyLogLevel()
	{
		const auto lvl = static_cast<spdlog::level::level_enum>(std::clamp<std::uint32_t>(debug::logLevel, 0u, 6u));
		SKSE::log::set_level(lvl, lvl);
	}

	const std::string& GetIniPath() { return iniPath; }
}
