/*
 *    sfall
 *    Copyright (C) 2008 - 2020  The sfall team
 *
 */

#include "..\..\main.h"
#include "..\..\FalloutEngine\Fallout2.h"
#include "..\..\Translate.h"

#include "CombatBlock.h"

namespace sfall
{

static bool combatDisabled;

static void __stdcall CombatBlocked() {
	fo::func::display_print(Translate::CombatBlockMessage().c_str());
}

static void __declspec(naked) intface_use_item_hook() {
	static const DWORD BlockCombatHook1Ret1 = 0x45F6AF;
	static const DWORD BlockCombatHook1Ret2 = 0x45F6D7;
	__asm {
		cmp  combatDisabled, 0;
		jne  block;
		jmp  BlockCombatHook1Ret1;
block:
		call CombatBlocked;
		jmp  BlockCombatHook1Ret2;
	}
}

static void __declspec(naked) game_handle_input_hook() {
	__asm {
		mov  eax, dword ptr ds:[FO_VAR_intfaceEnabled];
		test eax, eax;
		jz   end;
		cmp  combatDisabled, 0; // eax = 1
		je   end; // no blocked
		push edx;
		call CombatBlocked;
		pop  edx;
		xor  eax, eax;
end:
		retn;
	}
}

void __stdcall CombatBlock::SetBlockCombat(long toggle) {
	combatDisabled = toggle != 0;
}

void CombatBlock::init() {
	HookCall(0x45F626, intface_use_item_hook); // jnz hook
	HookCall(0x4432A6, game_handle_input_hook);
}

}
