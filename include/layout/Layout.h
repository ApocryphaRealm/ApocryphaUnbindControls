#pragma once

#include <string>

// The graphic on the game's Controls page. See the head of source/Layout.cpp.
namespace layout
{
	void Install();  // the AdvanceMovie wrap; call once at kDataLoaded

	// Asks the next journal frame to open or close the graphic (DevBench, any thread). It only opens on the Controls page.
	void RequestOpen();
	void RequestClose();
	void RequestContext(int a_delta);  // the open graphic's context, by a_delta, wrapping round
	void SetHintPosition(double a_right, double a_y);  // DevBench, to place the hint in a test; not kept

	std::string StateJson();  // the members of the tool's reply, no braces round them
}
