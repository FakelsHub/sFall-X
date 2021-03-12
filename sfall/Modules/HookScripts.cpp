/*
 *    sfall
 *    Copyright (C) 2008, 2009, 2010  The sfall team
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <string>
#include <vector>

#include "..\main.h"
#include "..\FalloutEngine\Fallout2.h"
#include "..\InputFuncs.h"
#include "..\Utils.h"
#include "LoadGameHook.h"

#include "HookScripts\Common.h"
#include "HookScripts\CombatHs.h"
#include "HookScripts\DeathHs.h"
#include "HookScripts\HexBlockingHs.h"
#include "HookScripts\InventoryHs.h"
#include "HookScripts\ObjectHs.h"
#include "HookScripts\MiscHs.h"

#include "HookScripts.h"

namespace sfall
{

// Number of types of hooks
static constexpr int numHooks = HOOK_COUNT;

static bool newHookRegister = true;
static bool injectAllHooks;

DWORD HookScripts::initingHookScripts;

std::string HookScripts::hookScriptPathFmt;
std::vector<HookFile> HookScripts::hookScriptFilesList;

typedef void(*HookInjectFunc)();

struct HooksInjectInfo {
	int id;
	HookInjectFunc inject;
	char injectState;
};

static struct HooksPositionInfo {
	long hsPosition    = 0; // index of the hs_* script, or the beginning of the position for registering scripts using register_hook
	long positionShift = 0; // offset to the last registered script by the register_hook function
	bool hasHsScript   = false;
} hooksInfo[numHooks];

static HooksInjectInfo injectHooks[] = {
	{HOOK_TOHIT,            Inject_ToHitHook,            0},
	{HOOK_AFTERHITROLL,     Inject_AfterHitRollHook,     0},
	{HOOK_CALCAPCOST,       Inject_CalcApCostHook,       0},
	{HOOK_DEATHANIM1,       Inject_DeathAnim1Hook,       0},
	{HOOK_DEATHANIM2,       Inject_DeathAnim2Hook,       0},
	{HOOK_COMBATDAMAGE,     Inject_CombatDamageHook,     0},
	{HOOK_ONDEATH,          Inject_OnDeathHook,          0},
	{HOOK_FINDTARGET,       Inject_FindTargetHook,       0},
	{HOOK_USEOBJON,         Inject_UseObjOnHook,         0},
	{HOOK_REMOVEINVENOBJ,   Inject_RemoveInvenObjHook,   0},
	{HOOK_BARTERPRICE,      Inject_BarterPriceHook,      0},
	{HOOK_MOVECOST,         Inject_MoveCostHook,         0},
	{HOOK_HEXMOVEBLOCKING,  Inject_HexMoveBlockHook,     0},
	{HOOK_HEXAIBLOCKING,    Inject_HexIABlockHook,       0},
	{HOOK_HEXSHOOTBLOCKING, Inject_HexShootBlockHook,    0},
	{HOOK_HEXSIGHTBLOCKING, Inject_HexSightBlockHook,    0},
	{HOOK_ITEMDAMAGE,       Inject_ItemDamageHook,       0},
	{HOOK_AMMOCOST,         Inject_AmmoCostHook,         0},
	{HOOK_USEOBJ,           Inject_UseObjHook,           0},
	{HOOK_KEYPRESS,         nullptr,                     2}, // no embed code to the engine
	{HOOK_MOUSECLICK,       nullptr,                     2}, // no embed code to the engine
	{HOOK_USESKILL,         Inject_UseSkillHook,         0},
	{HOOK_STEAL,            Inject_StealCheckHook,       0},
	{HOOK_WITHINPERCEPTION, Inject_WithinPerceptionHook, 0},
	{HOOK_INVENTORYMOVE,    Inject_InventoryMoveHook,    0},
	{HOOK_INVENWIELD,       Inject_InvenWieldHook,       0},
	{HOOK_ADJUSTFID,        nullptr,                     2}, // always embedded to the engine
	{HOOK_COMBATTURN,       Inject_CombatTurnHook,       0},
	{HOOK_CARTRAVEL,        Inject_CarTravelHook,        0},
	{HOOK_SETGLOBALVAR,     Inject_SetGlobalVarHook,     0},
	{HOOK_RESTTIMER,        Inject_RestTimerHook,        0},
	{HOOK_GAMEMODECHANGE,   nullptr,                     2}, // always embedded to the engine
	{HOOK_USEANIMOBJ,       Inject_UseAnimateObjHook,    0},
	{HOOK_EXPLOSIVETIMER,   Inject_ExplosiveTimerHook,   0},
	{HOOK_DESCRIPTIONOBJ,   Inject_DescriptionObjHook,   0},
	{HOOK_USESKILLON,       Inject_UseSkillOnHook,       0},
	{HOOK_ONEXPLOSION,      Inject_OnExplosionHook,      0},
	{HOOK_SUBCOMBATDAMAGE,  Inject_SubCombatDamageHook,  0}, // replace the code logic
	{HOOK_SETLIGHTING,      Inject_SetLightingHook,      0},
	{HOOK_SNEAK,            Inject_SneakCheckHook,       0},
	{HOOK_STDPROCEDURE,     Inject_ScriptProcedureHook,  0},
	{HOOK_STDPROCEDURE_END, Inject_ScriptProcedureHook2, 0},
	{HOOK_TARGETOBJECT,     Inject_TargetObjectHook,     0},
	{HOOK_ENCOUNTER,        Inject_EncounterHook,        0},
	{HOOK_ADJUSTPOISON,     Inject_AdjustPoisonHook,     0},
	{HOOK_ADJUSTRADS,       Inject_AdjustPadsHook,       1},  // always embedded for party control fix
	{HOOK_ROLLCHECK,        Inject_RollCheckHook,        0},
};

void HookScripts::InjectingHook(int hookId) {
	if (IsInjectHook(hookId) == false && injectHooks[hookId].id == hookId) {
		injectHooks[hookId].injectState = 2;
		injectHooks[hookId].inject();
		devlog_f("Inject hook ID: %d\n", DL_INIT, hookId);
	}
}

bool HookScripts::IsInjectHook(int hookId) {
	return injectHooks[hookId].injectState == 2;
}

bool HookScripts::HookHasScript(int hookId) {
	return (hooks[hookId].empty() == false);
}

void HookScripts::RegisterHook(fo::Program* script, int id, int procNum, bool specReg) {
	if (id >= numHooks) return;
	for (std::vector<HookScript>::iterator it = hooks[id].begin(); it != hooks[id].end(); ++it) {
		if (it->prog.ptr == script) {
			if (procNum == 0) {
				if (newHookRegister) {
					long index = std::distance(hooks[id].begin(), it);
					if (hooksInfo[id].hsPosition > index) {
						hooksInfo[id].hsPosition--;        // for register_hook_proc_spec
					} else if (hooksInfo[id].hasHsScript && hooksInfo[id].hsPosition == index) {
						hooksInfo[id].hasHsScript = false; // for unregister hs_ script
					} else if (hooksInfo[id].positionShift && it->isGlobalScript == 2) hooksInfo[id].positionShift--; // for register_hook
				}
				hooks[id].erase(it); // unregister
			}
			return;
		}
	}
	if (procNum == 0) return; // prevent registration to first location in procedure when reusing "unregister" method

	ScriptProgram *prog = ScriptExtender::GetGlobalScriptProgram(script);
	if (prog) {
		dlog_f("Script: %s registered as hook ID %d\n", DL_HOOK, script->fileName, id);
		HookScript hook;
		hook.prog = *prog;
		hook.callback = procNum;
		hook.isGlobalScript = (procNum == -1) ? 2 : 1;

		auto c_it = hooks[id].cend();
		if (specReg) {
			c_it = hooks[id].cbegin();
			hooksInfo[id].hsPosition++;
		} else if (newHookRegister && procNum == -1) { // for register_hook
			size_t index = hooksInfo[id].hsPosition + hooksInfo[id].positionShift; // last position of the registered script using register_hook
			if (hooksInfo[id].hasHsScript) index++;
			if (hooks[id].size() > index) c_it = hooks[id].cbegin() + index;
			hooksInfo[id].positionShift++;
		}
		hooks[id].insert(c_it, hook);

		HookScripts::InjectingHook(id); // inject hook to engine code
	}
}

// run specific event procedure for all hook scripts
void HookScripts::RunHookScriptsAtProc(DWORD procId) {
	for (int i = 0; i < numHooks; i++) {
		if (hooksInfo[i].hasHsScript /*&& !hooks[i][hooksInfo[i].hsPosition].isGlobalScript*/) {
			RunScriptProc(&hooks[i][hooksInfo[i].hsPosition].prog, procId); // run hs_*.int
		}
	}
}

void HookScripts::LoadHookScript(const char* name, int id) {
	char filePath[MAX_PATH];

	bool customPath = !HookScripts::hookScriptPathFmt.empty();
	if (!customPath) {
		sprintf(filePath, "scripts\\%s.int", name);
	} else {
		sprintf_s(filePath, HookScripts::hookScriptPathFmt.c_str(), name);
		name = filePath;
	}
	bool hookHasScript = fo::func::db_access(filePath);

	if (hookHasScript || injectHooks[id].injectState == 1 || (injectAllHooks && id != HOOK_SUBCOMBATDAMAGE)) {
		HookScripts::InjectingHook(id); // inject hook to engine code

		if (!hookHasScript) return; // only inject

		HookFile hookFile = { id, filePath, name };
		HookScripts::hookScriptFilesList.push_back(hookFile);

		dlog_f("Found hook script: %s\n", DL_HOOK, filePath);
	}
}

void HookScripts::InitHookScriptFile(std::string &filePath, const char* name, int id, bool fullPath) {
	ScriptProgram prog;
	dlog("Init: " + filePath, DL_HOOK);
	InitScriptProgram(prog, name, fullPath);
	if (prog.ptr) {
		HookScript hook;
		hook.prog = prog;
		hook.callback = -1;
		hook.isGlobalScript = 0;
		hooks[id].push_back(hook);
		ScriptExtender::AddProgramToMap(prog);
		dlogr(" Done", DL_HOOK);
	} else {
		dlogr(" Error!", DL_HOOK);
	}
	//return (prog.ptr != nullptr);
}

void HookScripts::LoadHookScripts() {
	dlogr("Loading hook scripts:", DL_HOOK|DL_INIT);

	static bool hooksFilesLoaded = false;
	if (!hooksFilesLoaded) { // hook files are already put in the list
		HookScripts::hookScriptFilesList.clear();

		InitCombatHookScripts();
		InitDeathHookScripts();
		InitHexBlockingHookScripts();
		InitInventoryHookScripts();
		InitObjectHookScripts();
		InitMiscHookScripts();

		HookScripts::LoadHookScript("hs_keypress", HOOK_KEYPRESS);
		HookScripts::LoadHookScript("hs_mouseclick", HOOK_MOUSECLICK);
		HookScripts::LoadHookScript("hs_gamemodechange", HOOK_GAMEMODECHANGE);

		hooksFilesLoaded = !alwaysFindScripts;
	}
	dlogr("Finished loading hook scripts.", DL_HOOK|DL_INIT);
}

void HookScripts::InitHookScripts() {
	// Note: here isGlobalScriptLoading must be is set, this should allow to register global exported variables
	dlog("Running hook scripts...\n", DL_HOOK);

	bool customPath = !HookScripts::hookScriptPathFmt.empty();
	for (auto& hook : HookScripts::hookScriptFilesList) {
		HookScripts::InitHookScriptFile(hook.filePath, hook.name.c_str(), hook.id, customPath);
	}

	initingHookScripts = 1;
	for (int i = 0; i < numHooks; i++) {
		if (!hooks[i].empty()) {
			hooksInfo[i].hasHsScript = true;
			RunScriptProgram(hooks[i][0].prog); // zero hook is always hs_*.int script because Hook scripts are loaded BEFORE global scripts
		}
	}
	initingHookScripts = 0;
}

void HookScripts::HookScriptClear() {
	for(int i = 0; i < numHooks; i++) {
		hooks[i].clear();
	}
	std::memset(hooksInfo, 0, numHooks * sizeof(HooksPositionInfo));
	HookCommon::Reset();
}

void HookScripts::init() {
	OnMouseClick() += HookCommon::MouseClickHook;
	LoadGameHook::OnGameModeChange() += HookCommon::GameModeChangeHook;
	LoadGameHook::OnAfterGameStarted() += SourceUseSkillOnInit;

	if (GetConfigInt("Scripts", "BackwardHooksRegistration", 1) > 0) {
		newHookRegister = false;
		dlogr("Enabled backward compatibility mode of scripts registration for register_hook function.", DL_INIT);
	}

	hookScriptPathFmt = GetConfigString("Scripts", "HookScriptPath", "", 255);
	if (!hookScriptPathFmt.empty()) {
		ToLowerCase(hookScriptPathFmt);
		int len = hookScriptPathFmt.length();
		if (hookScriptPathFmt[len - 1] != '\\' ) {
			hookScriptPathFmt += "\\%s.int";
		} else {
			hookScriptPathFmt += "%s.int";
		}
	}

	injectAllHooks = isDebug && (IniReader::GetIntDefaultConfig("Debugging", "InjectAllGameHooks", 0) != 0);
}

}
