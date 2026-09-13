// Unbind Vanilla Controls - own code, MIT (2026-09-13). Leaves any of Skyrim's own controls with
// no key at all, per device, by writing the engine's own "unmapped" value into the live control
// map. No hooks, no ESP, no scripts, nothing written to the game's control files.
#include "PCH.h"

#include "DevBenchTool.h"
#include "Settings.h"
#include "UI.h"
#include "Unbinder.h"

#include "utils/Logger.h"
#include "utils/Strings.h"

namespace
{
	void MessageHandler(SKSE::MessagingInterface::Message* a_msg)
	{
		switch (a_msg->type)
		{
		case SKSE::MessagingInterface::kPostLoad:
			DevBenchTool::Init(false);
			break;
		case SKSE::MessagingInterface::kDataLoaded:
			strings::Configure("ApocryphaUnbindControls");
			UI::Register();
			unbinder::Install();
			unbinder::ApplyAll("data loaded");
			DevBenchTool::Init(true);
			break;
		case SKSE::MessagingInterface::kPostLoadGame:
			unbinder::ApplyAll("game loaded");
			break;
		case SKSE::MessagingInterface::kNewGame:
			unbinder::ApplyAll("new game");
			break;
		default:
			break;
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse);
	SKSE::log::init("ApocryphaUnbindControls");

	settings::Init("ApocryphaUnbindControls.ini");
	settings::ApplyLogLevel();
	SKSE::log::describe_level("ApocryphaUnbindControls.ini");

	logger::info("Unbind Vanilla Controls {} loading",
				 SKSE::PluginDeclaration::GetSingleton()->GetVersion().string("."));

	SKSE::GetMessagingInterface()->RegisterListener(MessageHandler);

	return true;
}
