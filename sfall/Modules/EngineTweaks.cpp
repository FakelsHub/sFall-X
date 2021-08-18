/*
 *    sfall
 *    Copyright (C) 2021  The sfall team
 *
 */

#include "..\FalloutEngine\Fallout2.h"
#include "..\main.h"

#include "..\Game\items.h"

#include "EngineTweaks.h"

namespace sfall
{

void EngineTweaks::init() {
	auto tweakFile = IniReader::GetConfigString("Misc", "TweakFile", "", MAX_PATH);
	if (!tweakFile.empty()) {
		const char* cTweakFile = tweakFile.insert(0, ".\\").c_str();

		game::Items::SetHealingPID(0, IniReader::GetInt("Items", "STIMPAK", fo::PID_STIMPAK, cTweakFile));
		game::Items::SetHealingPID(1, IniReader::GetInt("Items", "SUPER_STIMPAK", fo::PID_SUPER_STIMPAK, cTweakFile));
		game::Items::SetHealingPID(2, IniReader::GetInt("Items", "HEALING_POWDER", fo::PID_HEALING_POWDER, cTweakFile));
	}
}

void EngineTweaks::exit() {}

}
