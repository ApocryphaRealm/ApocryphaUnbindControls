#pragma once

// READING AND WRITING ONE VALUE IN ANOTHER MOD'S JSON (1.2.0) - pure logic, no game types, so tools\json_value_check.cpp
// tests it. The same rule as IniLineCore.h (batch 13): a value that already says the same thing - numbers by value - leaves
// the file exactly as it is, and a value that does change is replaced in place, so the file's indentation, key order and
// line ends stay the other mod's. A key that is not in the file is NOT added: a JSON file's shape belongs to the mod that
// reads it, and a key it never wrote is a key it may not read.
//
// A key path names nested objects with '.', e.g. "Hotkeys.iCallHorse". Keys are matched exactly (JSON keys are
// case-sensitive). Used for BFCO NG's Settings.json (its PowerAttackActionIDs array names the Input Manager actions its
// power attack listens to).

#include "IniLineCore.h"

#include <cctype>
#include <charconv>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace jsoncore
{
	namespace detail
	{
		inline std::size_t SkipSpace(std::string_view a_text, std::size_t a_pos)
		{
			while (a_pos < a_text.size() && std::isspace(static_cast<unsigned char>(a_text[a_pos]))) { ++a_pos; }
			return a_pos;
		}

		// a_pos at the opening quote; returns the position after the closing quote, npos when unterminated. a_out gets the
		// text between them with \" and \\ unescaped (enough to compare a key).
		inline std::size_t ReadString(std::string_view a_text, std::size_t a_pos, std::string* a_out)
		{
			if (a_pos >= a_text.size() || a_text[a_pos] != '"') { return std::string_view::npos; }
			for (std::size_t i = a_pos + 1; i < a_text.size(); ++i)
			{
				const char c = a_text[i];
				if (c == '\\')
				{
					if (++i >= a_text.size()) { return std::string_view::npos; }
					if (a_out) { a_out->push_back(a_text[i]); }
					continue;
				}
				if (c == '"') { return i + 1; }
				if (a_out) { a_out->push_back(c); }
			}
			return std::string_view::npos;
		}

		// a_pos at the first character of a value; returns the position just after it, npos when the text is not JSON.
		inline std::size_t SkipValue(std::string_view a_text, std::size_t a_pos, int a_depth = 0)
		{
			if (a_pos >= a_text.size() || a_depth > 64) { return std::string_view::npos; }
			const char c = a_text[a_pos];
			if (c == '"') { return ReadString(a_text, a_pos, nullptr); }
			if (c == '{' || c == '[')
			{
				const char close = c == '{' ? '}' : ']';
				std::size_t p = SkipSpace(a_text, a_pos + 1);
				if (p < a_text.size() && a_text[p] == close) { return p + 1; }
				while (p < a_text.size())
				{
					if (c == '{')
					{
						p = ReadString(a_text, p, nullptr);
						if (p == std::string_view::npos) { return p; }
						p = SkipSpace(a_text, p);
						if (p >= a_text.size() || a_text[p] != ':') { return std::string_view::npos; }
						p = SkipSpace(a_text, p + 1);
					}
					p = SkipValue(a_text, p, a_depth + 1);
					if (p == std::string_view::npos) { return p; }
					p = SkipSpace(a_text, p);
					if (p >= a_text.size()) { return std::string_view::npos; }
					if (a_text[p] == close) { return p + 1; }
					if (a_text[p] != ',') { return std::string_view::npos; }
					p = SkipSpace(a_text, p + 1);
				}
				return std::string_view::npos;
			}
			// number, true, false, null
			std::size_t p = a_pos;
			while (p < a_text.size() && (std::isalnum(static_cast<unsigned char>(a_text[p])) || a_text[p] == '-' || a_text[p] == '+' || a_text[p] == '.')) { ++p; }
			return p == a_pos ? std::string_view::npos : p;
		}
	}

	// The span [begin, end) of the value at a_path, or nullopt when the path is not in the text (or the text is not JSON
	// up to that point).
	struct Span
	{
		std::size_t begin = 0, end = 0;
	};

	inline std::optional<Span> FindValue(std::string_view a_text, std::string_view a_path)
	{
		std::size_t p = detail::SkipSpace(a_text, 0);
		if (a_text.substr(p, 3) == "\xEF\xBB\xBF") { p = detail::SkipSpace(a_text, p + 3); }
		while (true)
		{
			const auto dot = a_path.find('.');
			const std::string_view head = a_path.substr(0, dot);
			if (p >= a_text.size() || a_text[p] != '{') { return std::nullopt; }
			p = detail::SkipSpace(a_text, p + 1);
			bool found = false;
			while (p < a_text.size() && a_text[p] != '}')
			{
				std::string key;
				p = detail::ReadString(a_text, p, &key);
				if (p == std::string_view::npos) { return std::nullopt; }
				p = detail::SkipSpace(a_text, p);
				if (p >= a_text.size() || a_text[p] != ':') { return std::nullopt; }
				p = detail::SkipSpace(a_text, p + 1);
				if (key == head)
				{
					found = true;
					break;
				}
				p = detail::SkipValue(a_text, p);
				if (p == std::string_view::npos) { return std::nullopt; }
				p = detail::SkipSpace(a_text, p);
				if (p < a_text.size() && a_text[p] == ',') { p = detail::SkipSpace(a_text, p + 1); }
			}
			if (!found) { return std::nullopt; }
			if (dot == std::string_view::npos)
			{
				const std::size_t end = detail::SkipValue(a_text, p);
				if (end == std::string_view::npos) { return std::nullopt; }
				return Span{ p, end };
			}
			a_path.remove_prefix(dot + 1);
		}
	}

	// The integers of the array at a_path (non-integers are skipped); empty when it is missing or not an array.
	inline std::vector<int> ReadIntArray(std::string_view a_text, std::string_view a_path)
	{
		std::vector<int> out;
		const auto span = FindValue(a_text, a_path);
		if (!span || a_text[span->begin] != '[') { return out; }
		std::size_t p = detail::SkipSpace(a_text, span->begin + 1);
		while (p < span->end && a_text[p] != ']')
		{
			const std::size_t end = detail::SkipValue(a_text, p);
			if (end == std::string_view::npos) { break; }
			int v = 0;
			const auto r = std::from_chars(a_text.data() + p, a_text.data() + end, v);
			if (r.ec == std::errc{} && r.ptr == a_text.data() + end) { out.push_back(v); }
			p = detail::SkipSpace(a_text, end);
			if (p < span->end && a_text[p] == ',') { p = detail::SkipSpace(a_text, p + 1); }
		}
		return out;
	}

	enum class Write
	{
		kSame,     // the file already says it; the text is untouched
		kWritten,  // the value was replaced in place
		kMissing   // the path is not in the file; nothing written
	};

	// a_value is a JSON literal: a number, true/false, or a quoted string. Numbers compare by value, true/false ignoring
	// case, strings exactly.
	inline Write WriteValue(std::string& a_text, std::string_view a_path, std::string_view a_value)
	{
		const auto span = FindValue(a_text, a_path);
		if (!span) { return Write::kMissing; }
		const std::string_view current = std::string_view(a_text).substr(span->begin, span->end - span->begin);
		const bool quoted = !current.empty() && current.front() == '"';
		if (quoted ? current == a_value : inicore::SameValue(current, a_value)) { return Write::kSame; }
		a_text.replace(span->begin, span->end - span->begin, a_value);
		return Write::kWritten;
	}
}
