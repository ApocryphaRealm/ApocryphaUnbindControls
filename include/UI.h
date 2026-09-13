#pragma once

namespace UI
{
	// Adds this mod's tabs to the menu framework (Apocrypha Menu Framework preferred, stock SKSE
	// Menu Framework as the fallback). Call at kDataLoaded.
	void Register();

	namespace Panels
	{
		void __stdcall General();
		void __stdcall Gameplay();
		void __stdcall Menus();
		void __stdcall ConsoleAndDebug();
	}
}
