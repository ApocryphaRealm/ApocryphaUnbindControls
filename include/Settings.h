#pragma once

// Unbind Vanilla Controls - settings. Plain-file INI (redirector-proof, the project standard).
// A few scalars here; the list of unbound controls lives in the [Unbound] section and is owned by
// the unbinder (Unbinder.h) - Settings only reads it from and writes it to the file.

#include <cstdint>
#include <string>

namespace settings
{
	namespace debug
	{
		inline std::uint32_t logLevel = 2;  // uLogLevel:Debug - ships at info (rule 14, 2026-09-26)
	}

	namespace general
	{
		inline bool enabled = true;  // bEnabled:General - apply the [Unbound] list in game
		inline bool keepRemapsInIni = true;  // bKeepRemapsInIni:General - Controls-menu remaps live in this INI, not ControlMap_Custom.txt
	}

	namespace controls
	{
		// GamepadFaces:Controls - which controller art the Controls sheet draws, on a journal whose rows the sheet draws
		// its own tiles on (the game's own journal, SkyUI's). Auto: the art the game's button names ask for (360_ from
		// the game's own gamepad.txt); Xbox / PlayStation: that set, whatever the names say. Read only: never written
		// back, so the player's line stays exactly as typed. gamepad.txt and the control map are never touched.
		enum class Faces : std::uint32_t { kAuto = 0, kXbox = 1, kPlayStation = 2 };
		inline Faces gamepadFaces = Faces::kAuto;
		const char* FacesName(Faces a_faces);
	}

	void Init(const std::string& a_iniFileName);
	bool Reload();          // re-reads scalars AND the [Unbound] list (does not apply it - the caller does)
	bool Save();            // writes scalars and rewrites the [Unbound] section from the unbinder's list
	void RestoreDefaults(); // scalars only; the caller clears the list
	void ApplyLogLevel();
	const std::string& GetIniPath();
}
