#pragma once

#include "layout/LayoutRows.h"
#include "layout/LayoutSettings.h"

#include <array>
#include <optional>
#include <string>
#include <vector>

// What Steam's own files say about the pad and how it is set up for this game: read only, at each open of the graphic, on the main
// thread. Steam Input's configs are undocumented, so anything not understood is simply not used: a button falls back to its usual
// place, a trackpad to "not bound by the game". A config's title is the player's own text: it is never read out, logged or shown, and
// nor is its file name.
namespace steamconfig
{
	struct TrackpadLines
	{
		bool read = false;               // its active group was found
		std::vector<std::string> lines;  // what it does, in words as read: "Up: K" four times, or the mode it is in
	};

	struct Info
	{
		bool steamInput = false;             // Steam's overlay is in this process, so its files say something about this run
		std::optional<layoutsettings::Faces> logPad;  // the pad the log says is connected, when it says clearly
		std::string logWhy;                  // what in the log said so; never a path
		bool configRead = false;             // a config for this game was found and parsed
		std::string configType;              // its controller_type: controller_triton
		std::string why;                     // why it was not read, for the log and the tool

		TrackpadLines left;   // left_trackpad: the Steam Controller's left pad, or the left half of a PlayStation touchpad
		TrackpadLines right;  // right_trackpad

		// Which control sends each XInput button by this config, in the order found: a rows::Button, or a padtables::Part. Empty: not read.
		std::array<std::vector<int>, rows::kButtonCount> senders;
	};

	// Reads everything again. Any failure leaves the Info saying so, with one log line.
	void Refresh();
	const Info& Current();  // main thread

	// The picture a config's controller_type stands for, from the types in Steam's own templates; nothing for a pad with no picture of its own.
	std::optional<layoutsettings::Faces> PadOfType(const std::string& a_type);
}
