/*
 *    sfall
 *    Copyright (C) 2008 - 2021  The sfall team
 *
 */

#include "..\..\main.h"
#include "..\..\FalloutEngine\Fallout2.h"
#include "..\Combat.h"

#include "..\..\Game\items.h"

#include "..\..\Game\ImprovedAI\AI.FuncHelpers.h"

#include "CombatExt.h"

namespace sfall
{

static void __declspec(naked) compute_damage_hack_knockback() {
	static const DWORD compute_damage_hack_knockback_Ret = 0x424AF1;
	using namespace fo;
	__asm {
		cmp [esi + 8], 0; // ctd.weapon
		jz  checkHit;
noKnockback:
		retn;
checkHit:
		mov edx, [esi + 4]; // ctd.hit_mode
		cmp edx, ATKTYPE_KICK;
		je  knockback;
		cmp edx, ATKTYPE_STRONGKICK;
		jl  noKnockback;
knockback:
		add esp, 4; // destroy return addr
		jmp compute_damage_hack_knockback_Ret;
	}
}

/////////////////////////////////////////////////////////////////////////////////////////

// Returns the distance to the target or -1 if the attack is not possible
static long DudeCanMeleeAttack(fo::GameObject* target, long hitMode, long isCalledShot, fo::GameObject* weapon) {
	long wType = fo::func::item_w_subtype(weapon, hitMode);
	if (wType > fo::AttackSubType::MELEE) return -1;

	long distance = fo::func::make_path_func(fo::var::obj_dude, fo::var::obj_dude->tile, target->tile, 0, 0, (void*)fo::funcoffs::obj_blocking_at_);
	if (distance == 0) return -1;

	distance -= fo::func::item_w_range(fo::var::obj_dude, hitMode);

	// unarmed and melee weapon, check the distance and cost AP
	long dudeAP = fo::var::obj_dude->critter.getAP() + fo::var::combat_free_move;
	long needAP = fo::func::critter_compute_ap_from_distance(fo::var::obj_dude, distance);
	if (needAP > dudeAP) return -1;
	needAP += game::Items::item_w_mp_cost(fo::var::obj_dude, hitMode, isCalledShot);

	return (needAP <= dudeAP) ? distance : -1;
}

static long __fastcall DudeMoveToAttackTarget(fo::GameObject* target, fo::AttackType hitMode, long isCalledShot) {
	fo::GameObject* weapon = fo::func::item_hit_with(fo::var::obj_dude, hitMode);
	long distance = DudeCanMeleeAttack(target, hitMode, isCalledShot, weapon);
	if (distance == -1) return -1;

	// check ammo
	int result = ((fo::func::item_w_max_ammo(weapon) > 0) && !Combat::check_item_ammo_cost(weapon, hitMode));
	return (result || game::imp_ai::AIHelpers::CombatMoveToObject(fo::var::obj_dude, target, distance));
}

static void __declspec(naked) combat_attack_this_hack() {
	static const DWORD combat_attack_this_hack_Ret1 = 0x4269F0;
	static const DWORD combat_attack_this_hack_Ret2 = 0x4268AA;
	static const DWORD combat_attack_this_hack_Ret3 = 0x426938;
	__asm {
		mov  ecx, esi;                     // target
		mov  edx, [esp + 0xAC - 0x20 + 4]; // hit mode
		push [esp + 0xAC - 0x1C + 4];      // called shot
		call DudeMoveToAttackTarget;
		test eax, eax;
		jl   outRange;
		jg   noAmmo;
		add  esp, 4;
		jmp  combat_attack_this_hack_Ret1; // attack target
noAmmo:
		add  esp, 4;
		jmp  combat_attack_this_hack_Ret2; // no ammo report
outRange:
		cmp  eax, -2;
		jz   breakAttack;
		mov  ecx, 102; // engine code
		retn;
breakAttack:
		add  esp, 4;
		jmp  combat_attack_this_hack_Ret3;
	}
}

static int32_t __fastcall CheckMeleeAttack(fo::GameObject* target, int32_t hitMode, int32_t isCalledShot) {
	fo::GameObject* weapon = fo::func::item_hit_with(fo::var::obj_dude, hitMode);
	return DudeCanMeleeAttack(target, hitMode, isCalledShot, weapon);
}

static int16_t setHitColor = 0;

static void __declspec(naked) combat_to_hit_hack() {
	static const DWORD combat_to_hit_hack_Ret = 0x426786;
	__asm {
		cmp  eax, 2;                       // out of range result combat_check_bad_shot_
		je   checkMelee;
		xor  eax,eax;                      // engine code
		retn 8;
checkMelee:
		mov  ecx, esi;                     // target
		mov  edx, [esp + 0x18 - 0x14 + 4]; // hit mode
		push [esp + 0x18 - 0x18 + 4];      // called shot
		call CheckMeleeAttack;
		test eax, eax;
		mov  eax, 0;
		jge  canAttack;                    // >=0
		retn 8;
canAttack:
		mov  setHitColor, 1;
		mov  [esp], eax;                   // set NoRange for determine_to_hit_func_
		jmp  combat_to_hit_hack_Ret;
	}
}

static void __declspec(naked) gmouse_bk_process_hack() {
	__asm {
		mov   al, ds:[FO_VAR_WhiteColor]; // default color
		cmp   setHitColor, 0;
		cmovne ax, ds:[FO_VAR_DarkYellowColor];
		mov   setHitColor, 0;
		retn;
	}
}

void CombatExt::init() {

	if (GetConfigInt("Misc", "DisablePunchKnockback", 0)) {
		MakeCall(0x424AD7, compute_damage_hack_knockback, 1);
	}

	if (GetConfigInt("Misc", "AutoMoveToAttack", 0)) {
		MakeCall(0x42690C, combat_attack_this_hack);
		MakeCall(0x42677A, combat_to_hit_hack);
		MakeCall(0x44BBD2, gmouse_bk_process_hack);
	}
}

}
