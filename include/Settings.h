#pragma once

// Unbind Vanilla Controls - settings. Plain-file INI (redirector-proof, the project standard).
// Two scalars here; the list of unbound controls lives in the [Unbound] section and is owned by
// the unbinder (Unbinder.h) - Settings only reads it from and writes it to the file.

#include <cstdint>
#include <string>

namespace settings
{
	namespace debug
	{
		inline std::uint32_t logLevel = 0;  // uLogLevel:Debug
	}

	namespace general
	{
		inline bool enabled = true;  // bEnabled:General - apply the [Unbound] list in game
		inline bool keepRemapsInIni = true;  // bKeepRemapsInIni:General - Controls-menu remaps live in this INI, not ControlMap_Custom.txt
	}

	void Init(const std::string& a_iniFileName);
	bool Reload();          // re-reads scalars AND the [Unbound] list (does not apply it - the caller does)
	bool Save();            // writes scalars and rewrites the [Unbound] section from the unbinder's list
	void RestoreDefaults(); // scalars only; the caller clears the list
	void ApplyLogLevel();
	const std::string& GetIniPath();
}
