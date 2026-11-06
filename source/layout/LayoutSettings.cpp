#include "PCH.h"

#include "layout/LayoutSettings.h"

#include "utils/Logger.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <mutex>
#include <vector>

namespace layoutsettings
{
	namespace
	{
		std::string g_iniPath;
		std::mutex g_saveLock;

		std::string Lower(std::string a_text)
		{
			std::ranges::transform(a_text, a_text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return a_text;
		}

		std::string Trim(const std::string& a_text)
		{
			const auto first = a_text.find_first_not_of(" \t\r\n");
			if (first == std::string::npos) { return {}; }
			return a_text.substr(first, a_text.find_last_not_of(" \t\r\n") - first + 1);
		}

		std::vector<std::string> ReadLines(const std::string& a_path)
		{
			std::vector<std::string> lines;
			std::ifstream file(a_path);
			for (std::string line; std::getline(file, line);)
			{
				if (!line.empty() && line.back() == '\r') { line.pop_back(); }
				lines.push_back(line);
			}
			return lines;
		}

		// "section:key" (lower case) -> value
		std::map<std::string, std::string> ReadKeys(const std::string& a_path)
		{
			std::map<std::string, std::string> out;
			std::string section;
			for (const auto& raw : ReadLines(a_path))
			{
				const std::string line = Trim(raw);
				if (line.empty() || line[0] == ';' || line[0] == '#') { continue; }
				if (line.front() == '[' && line.back() == ']')
				{
					section = Lower(line.substr(1, line.size() - 2));
					continue;
				}
				const auto eq = line.find('=');
				if (eq == std::string::npos) { continue; }
				out[Lower(Trim(line.substr(0, eq))) + ":" + section] = Trim(line.substr(eq + 1));
			}
			return out;
		}

		// One key's value, in the file's own line; the section and the line are added when the file has neither.
		void WriteKey(std::vector<std::string>& a_lines, const std::string& a_section, const std::string& a_key, const std::string& a_value)
		{
			const std::string wantSection = "[" + Lower(a_section) + "]";
			const std::string wantKey = Lower(a_key);
			std::size_t sectionEnd = a_lines.size();
			bool inSection = false, haveSection = false;
			for (std::size_t i = 0; i < a_lines.size(); ++i)
			{
				const std::string line = Trim(a_lines[i]);
				if (!line.empty() && line.front() == '[')
				{
					if (inSection) { sectionEnd = i; break; }
					inSection = Lower(line) == wantSection;
					haveSection = haveSection || inSection;
					continue;
				}
				if (!inSection || line.empty() || line[0] == ';') { continue; }
				const auto eq = line.find('=');
				if (eq != std::string::npos && Lower(Trim(line.substr(0, eq))) == wantKey)
				{
					a_lines[i] = a_key + "=" + a_value;
					return;
				}
			}
			if (!haveSection)
			{
				a_lines.push_back("[" + a_section + "]");
				a_lines.push_back(a_key + "=" + a_value);
				return;
			}
			// before the blank lines that end the section
			while (sectionEnd > 0 && Trim(a_lines[sectionEnd - 1]).empty()) { --sectionEnd; }
			a_lines.insert(a_lines.begin() + static_cast<std::ptrdiff_t>(sectionEnd), a_key + "=" + a_value);
		}
	}

	const char* FacesName(Faces a_faces)
	{
		switch (a_faces)
		{
		case Faces::kAuto: return "Auto";
		case Faces::kXbox: return "Xbox";
		case Faces::kSteam: return "Steam";
		default: return "PlayStation";
		}
	}

	bool ParseFaces(const std::string& a_text, Faces& a_out)
	{
		const std::string v = Lower(Trim(a_text));
		if (v == "playstation" || v == "ps") { a_out = Faces::kPlayStation; return true; }
		if (v == "xbox") { a_out = Faces::kXbox; return true; }
		if (v == "steam") { a_out = Faces::kSteam; return true; }
		if (v == "auto") { a_out = Faces::kAuto; return true; }
		return false;
	}

	void Load()
	{
		g_iniPath = (std::filesystem::current_path() / "Data" / "SKSE" / "Plugins" / "UnbindControls.ini").string();
		faces = Defaults::faces;
		if (!std::filesystem::exists(g_iniPath))
		{
			logger::warn("{} is not there; the compiled defaults are in force", g_iniPath);
			return;
		}
		const auto k = ReadKeys(g_iniPath);
		const auto it = k.find("gamepadfaces:controls");
		if (it != k.end())
		{
			Faces read = Defaults::faces;
			if (ParseFaces(it->second, read)) { faces = read; }
			else { logger::warn("INI GamepadFaces: \"{}\" is not Auto, PlayStation, Xbox or Steam; the default stays", it->second); }
		}
		logger::debug("settings: {} read: faces {}", g_iniPath, FacesName(faces.load()));
	}

	bool Save()
	{
		if (g_iniPath.empty()) { return false; }
		std::scoped_lock l(g_saveLock);
		auto lines = ReadLines(g_iniPath);
		WriteKey(lines, "Controls", "GamepadFaces", FacesName(faces.load()));
		std::ofstream file(g_iniPath, std::ios::trunc);
		if (!file)
		{
			logger::warn("settings: {} could not be written", g_iniPath);
			return false;
		}
		for (const auto& line : lines) { file << line << '\n'; }
		logger::debug("settings: {} written", g_iniPath);
		return static_cast<bool>(file);
	}

	const std::string& IniPath() { return g_iniPath; }

}
