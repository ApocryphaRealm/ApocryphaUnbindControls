// Unbind Vanilla Controls - own code, GPL-3.0-or-later (2026-09-13). Leaves the vanilla controls listed in its INI with
// no key at all, by writing the engine's own "unmapped" value into the live control map. No menu, no hooks,
// no ESP, no scripts, nothing written to the game's control files.
#include "PCH.h"

#include "DevBenchTool.h"
#include "Settings.h"
#include "Unbinder.h"

#include "utils/Logger.h"

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
