#pragma once

#include <atomic>
#include <cstdint>
#include <string>

// The controller graphic's one setting: GamepadFaces in the [Controls] section of SKSE\Plugins\UnbindControls.ini, the same line the
// Controls sheet reads (UVC 1.4.0: the graphic was UVC Controller Layout, folded into this mod). Read again each time the graphic opens,
// so a changed line needs no restart. Written back line by line only by the DevBench tool (never with WritePrivateProfileString, which
// reports success and writes nothing under MO2's virtual folder). The log level is UVC's own (uLogLevel:Debug).
namespace layoutsettings
{
	enum class Faces : int
	{
		kPlayStation = 0,
		kXbox = 1,
		kAuto = 2,  // Steam's own files, then the game: see FacesNow in source/Layout.cpp
		kSteam = 3,  // the 2026 Steam Controller
	};

	struct Defaults
	{
		static constexpr Faces faces = Faces::kAuto;
	};

	inline std::atomic<Faces> faces{ Defaults::faces };  // GamepadFaces:Controls

	const char* FacesName(Faces a_faces);
	bool ParseFaces(const std::string& a_text, Faces& a_out);  // "Auto", "PlayStation", "Xbox" or "Steam", any case

	void Load();  // a missing INI or line keeps the default
	bool Save();  // every value, keeping the file's comments; false when the file cannot be written
	const std::string& IniPath();
}
