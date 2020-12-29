/*
 *    sfall
 *    Copyright (C) 2008 - 2021  The sfall team
 *
 */

#include "..\main.h"
#include "..\FalloutEngine\Fallout2.h"
#include "PartyControl.h"

#include "CritterPoison.h"

namespace sfall
{

//static long adjustPoison = -2;

long CritterPoison::adjustPoisonHP_Default = -1;
long CritterPoison::adjustPoisonHP; // default or returned value from HOOK_ADJUSTPOISON

void CritterPoison::SetDefaultAdjustPoisonHP(long value) {
	adjustPoisonHP_Default = adjustPoisonHP = value;
}

void __fastcall sf_critter_adjust_poison(fo::GameObject* critter, long amount) {
	if (amount == 0) return;
	if (amount > 0) {
		amount -= fo::func::stat_level(critter, fo::STAT_poison_resist) * amount / 100;
	} else if (critter->critter.poison == 0) {
		return;
	}
	critter->critter.poison += amount;
	if (critter->critter.poison < 0) {
		critter->critter.poison = 0; // level can't be negative
	} else {
		//// set uID for save queue
		//Objects::SetObjectUniqueID(critter);
		//fo::func::queue_add(10 * (505 - 5 * critter->critter.poison), critter, nullptr, fo::QueueType::poison_event);
	}
}

static void __declspec(naked) critter_adjust_poison_hack() {
	__asm {
		mov edx, esi;
		mov ecx, edi;
		jmp sf_critter_adjust_poison;
	}
}

void __declspec(naked) critter_check_poison_hack() {
	__asm {
		mov  eax, CritterPoison::adjustPoisonHP_Default;
		mov  edx, CritterPoison::adjustPoisonHP;
		mov  CritterPoison::adjustPoisonHP, eax;
		retn;
	}
}

void __fastcall critter_check_poison_fix() {
	if (PartyControl::IsNpcControlled()) {
		// since another critter is being controlled, we can't apply the poison effect to it
		// instead, we add the "poison" event to dude again, which is triggered when dude will again be under the player's control
		fo::func::queue_clear_type(fo::QueueType::poison_event, nullptr); // is it required?
		fo::GameObject* dude = PartyControl::RealDudeObject();
		fo::func::queue_add(10, dude, nullptr, fo::QueueType::poison_event);
	}
}

static void __declspec(naked) critter_check_poison_hack_fix() {
	using namespace fo;
	using namespace Fields;
	__asm {
		mov  ecx, [eax + protoId]; // critter.pid
		cmp  ecx, PID_Player;
		jnz  noDude;
		retn;
noDude:
		call critter_check_poison_fix;
		or   al, 1; // unset ZF (exit from func)
		retn;
	}
}

void __declspec(naked) critter_adjust_poison_hack_fix() { // also can called from HOOK_ADJUSTPOISON
	using namespace fo;
	using namespace Fields;
	__asm {
		mov  edx, ds:[FO_VAR_obj_dude];
		mov  ebx, [eax + protoId]; // critter.pid
		mov  ecx, PID_Player;
		retn;
	}
}

void CritterPoison::init() {
	// Allow changes poison level for critters
	MakeCall(0x42D226, critter_adjust_poison_hack);
	SafeWrite8(0x42D22C, 0xDA); // jmp 0x42D30A

	// Adjust poison damage
	SetDefaultAdjustPoisonHP(*(DWORD*)0x42D332);
	MakeCall(0x42D331, critter_check_poison_hack);

	// Fix tweak for critter control
	MakeCall(0x42D31F, critter_check_poison_hack_fix, 1);
	MakeCall(0x42D21C, critter_adjust_poison_hack_fix, 1);
	SafeWrite8(0x42D223, 0xCB); // cmp eax, edx -> cmp ebx, ecx
}

}