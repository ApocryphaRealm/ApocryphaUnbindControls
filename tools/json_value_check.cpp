// Off-game check of include/JsonValueCore.h - reading and writing one value in another mod's JSON without restyling it
// (1.2.0, the same rule as IniLineCore.h after batch 13).
//   tools\json_value_check.bat
// Prints one line per check and "json_value_check: N of N passed"; exits 1 on any failure.
#include "../include/JsonValueCore.h"

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
	using namespace jsoncore;
	const std::string doc =
		"{\r\n"
		"    \"Name\": \"x \\\" y\",\r\n"
		"    \"Hotkeys\" : { \"iCall\" : 34 , \"iMenu\": -1 },\r\n"
		"    \"PowerAttackActionIDs\": [ 3, 7 ,12 ],\r\n"
		"    \"ComboActionIDs\": [],\r\n"
		"    \"bOn\": true\r\n"
		"}\r\n";
	{
		std::string t = doc;
		Check(WriteValue(t, "Hotkeys.iCall", "34") == Write::kSame && t == doc, "the same number leaves the file byte-identical");
		Check(WriteValue(t, "Hotkeys.iCall", "34.0") == Write::kSame && t == doc, "... compared by value (34 and 34.0)");
		Check(WriteValue(t, "bOn", "TRUE") == Write::kSame && t == doc, "true compares ignoring case");
		Check(WriteValue(t, "Hotkeys.iCall", "48") == Write::kWritten &&
				  t.find("{ \"iCall\" : 48 , \"iMenu\": -1 }") != std::string::npos && t.size() == doc.size(),
			  "a changed number is replaced in place - the spacing round it and the CRLF line ends stay");
		Check(WriteValue(t, "Hotkeys.iMenu", "-1") == Write::kSame, "a negative number compares by value");
	}
	{
		std::string t = doc;
		Check(WriteValue(t, "Hotkeys.iNew", "1") == Write::kMissing && t == doc, "a key the file does not have is NOT added");
		Check(WriteValue(t, "iCall", "1") == Write::kMissing && t == doc, "a nested key is not found at the top level");
		Check(WriteValue(t, "Name", "\"x \\\" y\"") == Write::kSame, "an escaped quote inside a string does not end it");
	}
	{
		Check(ReadIntArray(doc, "PowerAttackActionIDs") == std::vector<int>{ 3, 7, 12 }, "an int array is read (BFCO NG's PowerAttackActionIDs)");
		Check(ReadIntArray(doc, "ComboActionIDs").empty(), "an empty array reads as empty");
		Check(ReadIntArray(doc, "Missing").empty() && ReadIntArray(doc, "bOn").empty(), "a missing key or a non-array reads as empty");
		Check(ReadIntArray("\xEF\xBB\xBF{\"a\":[1]}", "a") == std::vector<int>{ 1 }, "a UTF-8 byte-order mark is skipped");
		Check(!FindValue("{\"a\": [1, 2", "a") && ReadIntArray("not json", "a").empty(), "broken JSON finds nothing rather than a wrong span");
	}

	std::cout << "json_value_check: " << g_pass << " of " << (g_pass + g_fail) << " passed\n";
	return g_fail ? 1 : 0;
}
