/*
 *    sfall
 *    Copyright (C) 2008-2020  The sfall team
 *
 */

#include "..\main.h"
#include "..\FalloutEngine\Fallout2.h"
#include "LoadGameHook.h"

#include "MetaruleExtender.h"

namespace sfall
{

static signed char HorriganEncounterDefaultDays = 35;
static signed char HorriganEncounterSetDays = 35;
static bool HorriganEncounterDisabled = false;

enum class MetaruleFunction : long
{
	SET_HORRIGAN_ENCOUNTER = 200, // sets the number of days for the Fran Horrigan meeting or disable encounter
};

/*
	args - contains a pointer to an array (size of 3) of arguments for the metarule3 function [located on the stack]
*/
static int32_t __fastcall op_metarule3_ext(int32_t metafunc, int32_t* &args) {
	int32_t result = 0;

	switch (static_cast<MetaruleFunction>(metafunc)) {
		case MetaruleFunction::SET_HORRIGAN_ENCOUNTER:
		{
			int32_t argValue = args[0];     // arg1
			if (argValue <= 0) {            // set horrigan disable
				SafeWrite8(0x4C06D8, 0xEB); // skip the Horrigan encounter check
				HorriganEncounterDisabled = true;
			} else {
				if (argValue > 127) argValue = 127;
				SafeWrite8(0x4C06EA, argValue);
				HorriganEncounterSetDays = argValue;
			}
			break;
		}
		default:
			fo::func::debug_printf("\nOPCODE ERROR: metarule3(%d, ...) - specified metarule function number does not exist.\n > Script: %s, procedure %s.\n",
								   metafunc, fo::var::currentProgram->fileName, fo::func::findCurrentProc(fo::var::currentProgram));
			break;
	}
	return result;
}

static void __declspec(naked) op_metarule3_hack() {
	static const DWORD op_metarule3_hack_Ret = 0x45732C;
	__asm {
		cmp  ecx, 111;
		jnz  extended;
		retn;
extended:
		lea  edx, [esp + 0x4C - 0x4C + 4];
		push edx; // pointer to args
		// swap arg1 <> arg3
		mov  eax, [edx];       // get: arg3
		xchg eax, [edx + 2*4]; // get: arg1, set: arg3 > arg1
		mov  [edx], eax;       // set: arg1 > arg3
		//
		mov  edx, esp;
		call op_metarule3_ext;
		add  esp, 8;
		jmp  op_metarule3_hack_Ret;
	}
}

static void Reset() {
	if (HorriganEncounterSetDays != HorriganEncounterDefaultDays) {
		SafeWrite8(0x4C06EA, HorriganEncounterDefaultDays);
	}
	if (HorriganEncounterDisabled) {
		HorriganEncounterDisabled = false;
		SafeWrite8(0x4C06D8, 0x75); // enable
	}
}

void MetaruleExtender::init() {
	// Keep default value
	HorriganEncounterDefaultDays = *(BYTE*)0x4C06EA;

	MakeCall(0x457322, op_metarule3_hack);

	LoadGameHook::OnGameReset() += Reset;
}

}
