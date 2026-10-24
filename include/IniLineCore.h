#pragma once

// WRITING ONE KEY INTO ANOTHER MOD'S INI (1.2.0) - pure logic, no game types, so tools\ini_line_check.cpp tests it.
//
// Batch 13 (2026-10-23): at the first launch the mod rewrote TK Dodge AIO.ini and TrueDirectionalMovement.ini with the
// SAME values - "uDodgeKey = 42" became "uDodgeKey=42", so the file's hash changed although nothing it says did. The
// write replaced every matching line with "key=value", and the "already says" test compared the text of the lines, so a
// value written with spaces round the '=' always looked different. Now a key whose value already parses the same is left
// exactly as it is, and a value that does change is replaced in place - the key's spelling, the spaces round the '=' and
// a trailing comment stay as the file had them.

#include <algorithm>
#include <cctype>
#include <charconv>
#include <string>
#include <string_view>
#include <vector>

namespace inicore
{
	inline bool IEquals(std::string_view a_lhs, std::string_view a_rhs)
	{
		return a_lhs.size() == a_rhs.size() &&
		       std::equal(a_lhs.begin(), a_lhs.end(), a_rhs.begin(),
		                  [](unsigned char a, unsigned char b) { return std::tolower(a) == std::tolower(b); });
	}

	inline std::string Trim(std::string_view a_text)
	{
		const auto first = a_text.find_first_not_of(" \t\r\n");
		if (first == std::string_view::npos) { return {}; }
		const auto last = a_text.find_last_not_of(" \t\r\n");
		return std::string(a_text.substr(first, last - first + 1));
	}

	inline bool IsSectionHeader(std::string_view a_line)
	{
		return a_line.size() >= 2 && a_line.front() == '[' && a_line.back() == ']';
	}

	// Where the value sits in a "key = value ; comment" line: [begin, end) of the value text, spaces excluded.
	struct ValueSpan
	{
		std::size_t begin = 0;
		std::size_t end = 0;
	};

	inline ValueSpan FindValue(std::string_view a_line, std::size_t a_eq)
	{
		std::size_t b = a_eq + 1;
		while (b < a_line.size() && (a_line[b] == ' ' || a_line[b] == '\t')) { ++b; }
		std::size_t e = a_line.size();
		// An inline comment starts at a ';' or '#' that follows a space (a value such as "a;b" keeps its ';').
		for (std::size_t i = b; i < a_line.size(); ++i)
		{
			if ((a_line[i] == ';' || a_line[i] == '#') && i > b && (a_line[i - 1] == ' ' || a_line[i - 1] == '\t')) { e = i; break; }
		}
		while (e > b && (a_line[e - 1] == ' ' || a_line[e - 1] == '\t' || a_line[e - 1] == '\r')) { --e; }
		return { b, e };
	}

	// The same setting: equal text ignoring case, or two numbers of equal value ("42" and "42.0", "-1" and "-1.000").
	inline bool SameValue(std::string_view a_have, std::string_view a_want)
	{
		const std::string have = Trim(a_have);
		const std::string want = Trim(a_want);
		if (IEquals(have, want)) { return true; }
		double h = 0, w = 0;
		const auto rh = std::from_chars(have.data(), have.data() + have.size(), h);
		const auto rw = std::from_chars(want.data(), want.data() + want.size(), w);
		return rh.ec == std::errc{} && rh.ptr == have.data() + have.size() &&
		       rw.ec == std::errc{} && rw.ptr == want.data() + want.size() && h == w;
	}

	// Writes key=value inside a_section of a_lines, adding the section or the key when either is missing, and leaving
	// every other line of the file exactly as it was. A key that already holds this value is not touched; a different
	// value is replaced in place. Returns true when a_lines changed.
	inline bool WriteKey(std::vector<std::string>& a_lines, const std::string& a_section, const std::string& a_key, const std::string& a_value)
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
			const auto eq = a_lines[i].find('=');
			if (eq == std::string::npos) { continue; }
			if (IEquals(Trim(std::string_view(a_lines[i]).substr(0, eq)), a_key))
			{
				const ValueSpan v = FindValue(a_lines[i], eq);
				if (SameValue(std::string_view(a_lines[i]).substr(v.begin, v.end - v.begin), a_value)) { return false; }
				a_lines[i].replace(v.begin, v.end - v.begin, a_value);
				return true;
			}
		}
		a_lines.push_back(a_key + "=" + a_value);
		return true;
	}
}
