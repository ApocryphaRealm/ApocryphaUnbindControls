#pragma once

#include "layout/LayoutSettings.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

// What each button does, read fresh from Unbind Vanilla Controls every time the graphic opens (UVC_ForEachBinding). The graphic is
// modal, so nothing can change while it is up: reading at each open is all the "hearing a change" there is, and it needs no event.
namespace rows
{
	enum Button : int
	{
		kLT,
		kLB,
		kBack,
		kDUp,
		kDLeft,
		kDDown,
		kDRight,
		kLStick,
		kRT,
		kRB,
		kStart,
		kY,
		kB,
		kA,
		kX,
		kRStick,
		kButtonCount,
	};

	struct Line
	{
		std::string text;
		bool mod = false;  // a mod's row, not the game's control: a little dimmer
	};

	using Table = std::array<std::vector<Line>, kButtonCount>;

	// One control map: what the buttons do in one context. Only contexts with something bound are read back.
	struct Context
	{
		std::string id;    // the block's name in UVC, for the log and the tool: never translated
		std::string name;  // the name at the top of the graphic
		Table table{};
	};

	bool ApiThere();                    // UVC is loaded and has UVC_ForEachBinding
	// Every context, in the order the graphic steps through them. a_faces is Xbox, PlayStation or Steam, never Auto. Main thread.
	std::vector<Context> Read(layoutsettings::Faces a_faces, int& a_rowsSeen);
	std::string ShortName(Button a_button, layoutsettings::Faces a_faces);  // "L1" or "LB"; "" for a face button or the D-pad, which are drawn
	const char* ButtonId(Button a_button);  // for the log and the tool: never translated
}
