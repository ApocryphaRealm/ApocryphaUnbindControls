// Off-game check of include/IniLineCore.h - writing one key into another mod's INI without restyling it (1.2.0, batch 13:
// TK Dodge AIO.ini and TrueDirectionalMovement.ini were rewritten with the same values, "key = value" -> "key=value").
//   tools\ini_line_check.bat
// Prints one line per check and "ini_line_check: N of N passed"; exits 1 on any failure.
#include "../include/IniLineCore.h"

#include <iostream>
#include <string>
#include <vector>

namespace
{
	int g_pass = 0, g_fail = 0;

	void Check(bool a_ok, const std::string& a_what)
	{
		(a_ok ? g_pass : g_fail)++;
		std::cout << (a_ok ? "PASS " : "FAIL ") << a_what << "\n";
	}
}

int main()
{
	using namespace inicore;
	{
		std::vector<std::string> l{ "[Main]", "uDodgeKey = 42", "fSpeed = 1.5" };
		const auto before = l;
		Check(!WriteKey(l, "Main", "uDodgeKey", "42") && l == before, "the same value with spaces round '=' is left as the file has it (batch 13)");
		Check(!WriteKey(l, "main", "udodgekey", "42") && l == before, "... matched case-insensitively on section and key");
		Check(WriteKey(l, "Main", "uDodgeKey", "56") && l[1] == "uDodgeKey = 56", "a changed value is replaced in place - spaces kept");
	}
	{
		std::vector<std::string> l{ "[Controls]", "iModifier = -1.000 ; none", "Key=0x2A" };
		const auto before = l;
		Check(!WriteKey(l, "Controls", "iModifier", "-1") && l == before, "-1 and -1.000 are the same value; the comment stays");
		Check(WriteKey(l, "Controls", "iModifier", "42") && l[1] == "iModifier = 42 ; none", "a changed value keeps its trailing comment");
		Check(WriteKey(l, "Controls", "Key", "42") && l[2] == "Key=42", "text that is not a decimal number is compared as text");
	}
	{
		std::vector<std::string> l{ "[A]", "bOn = true" };
		Check(!WriteKey(l, "A", "bOn", "TRUE"), "text values compare ignoring case");
		Check(!WriteKey(l, "A", "bOn", " true "), "... and ignoring surrounding spaces");
	}
	{
		std::vector<std::string> l{ "[A]", "x=1", "[B]", "y=2" };
		Check(WriteKey(l, "A", "z", "3") && l.size() == 5 && l[2] == "z=3" && l[3] == "[B]", "a missing key is added at the end of its section");
		Check(WriteKey(l, "C", "w", "4") && l.back() == "w=4" && l[l.size() - 2] == "[C]" && l[l.size() - 3].empty(), "a missing section is added after a blank line");
		std::vector<std::string> e;
		Check(WriteKey(e, "S", "k", "1") && e.size() == 2 && e[0] == "[S]", "an empty file gets the section and the key");
	}
	{
		std::vector<std::string> l{ "[A]", "; k = 9", "k =", "v = a;b" };
		Check(WriteKey(l, "A", "k", "5") && l[1] == "; k = 9" && l[2] == "k =5", "a commented-out key is skipped; an empty value is filled");
		Check(!WriteKey(l, "A", "v", "a;b"), "a ';' with no space before it is part of the value");
	}
	{
		Check(SameValue("42", "42.0") && SameValue("0", "0.000") && !SameValue("1", "10"), "numbers compare by value");
		Check(!SameValue("42abc", "42"), "a number followed by text is not that number");
	}

	std::cout << "ini_line_check: " << g_pass << " of " << (g_pass + g_fail) << " passed\n";
	return g_fail ? 1 : 0;
}
