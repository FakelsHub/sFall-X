/*
 *    sfall
 *    Copyright (C) 2012  The sfall team
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

#include <unordered_map>

#include "..\main.h"
#include "..\FalloutEngine\Fallout2.h"

#include "SubModules\AI.Behavior.h"

#include "AI.h"

namespace sfall
{

using namespace fo;
using namespace Fields;

static std::unordered_map<fo::GameObject*, fo::GameObject*> targets;
static std::unordered_map<fo::GameObject*, fo::GameObject*> sources;

static void __declspec(naked) ai_search_environ_hook() {
	static const DWORD ai_search_environ_ret = 0x429D3E;
	__asm {
		call fo::funcoffs::obj_dist_;
		cmp  [esp + 0x28 + 0x1C + 4], item_type_ammo;
		je   end;
		//
		push edx;
		push eax;
		mov  edx, STAT_max_move_points;
		mov  eax, esi;
		call fo::funcoffs::stat_level_;
		mov  edx, [esi + movePoints];    // source current ap
		cmp  edx, eax;                   // npc already used their ap?
		pop  eax;
		jge  skip;                       // yes
		// distance & AP check
		sub  edx, 3;                     // pickup cost ap
		cmp  edx, eax;                   // eax - distance to the object
		jl   continue;
skip:
		pop  edx;
end:
		retn;
continue:
		pop  edx;
		add  esp, 4;                     // destroy return
		jmp  ai_search_environ_ret;      // next object
	}
}

static void __declspec(naked) ai_try_attack_hook_FleeFix() {
	__asm {
		or  byte ptr [esi + combatState], 8; // set new 'ReTarget' flag
		jmp fo::funcoffs::ai_run_away_;
	}
}

static void __declspec(naked) combat_ai_hook_FleeFix() {
	static const DWORD combat_ai_hook_flee_Ret = 0x42B22F;
	__asm {
		test byte ptr [ebp], 8; // 'ReTarget' flag (critter.combat_state)
		jnz  reTarget;
		jmp  fo::funcoffs::critter_name_;
reTarget:
		and  byte ptr [ebp], ~(4 | 8); // unset Flee/ReTarget flags
		xor  edi, edi;
		mov  dword ptr [esi + whoHitMe], edi;
		add  esp, 4;
		jmp  combat_ai_hook_flee_Ret;
	}
}

static bool npcPercentMinHP = false;

static void __declspec(naked) combat_ai_hack() {
	static const DWORD combat_ai_hack_Ret = 0x42B204;
	__asm {
		mov  edx, [ebx + 0x10];             // cap.min_hp
		test npcPercentMinHP, 0xFF;
		jz   skip;
		cmp  dword ptr [ebx + 0x98], -1;    // cap.run_away_mode (none)
		je   skip;
		push eax;                           // current hp
		mov  eax, esi;
		call fo::funcoffs::isPartyMember_;
		test eax, eax;
		pop  eax;
		cmovz edx, [esp + 0x1C - 0x18 + 4]; // calculated min_hp (percent)
skip:
		cmp  eax, edx;
		jl   tryHeal; // curr_hp < min_hp
end:
		add  esp, 4;
		jmp  combat_ai_hack_Ret;
tryHeal:
		mov  eax, esi;
		call fo::funcoffs::ai_check_drugs_;
		cmp  [esi + health], edx; // edx - minimum hp, below which NPC will run away
		jge  end;
		retn; // flee
	}
}

static void __declspec(naked) ai_check_drugs_hook() {
	__asm {
		call fo::funcoffs::stat_level_;              // current hp
		mov  edx, dword ptr [esp + 0x34 - 0x1C + 4]; // ai cap
		mov  edx, [edx + 0x10];                      // min_hp
		cmp  eax, edx;                               // curr_hp <= cap.min_hp
		cmovl edi, edx;
		retn;
	}
}

static bool __fastcall TargetExistInList(fo::GameObject* target, fo::GameObject** targetList) {
	char i = 4;
	do {
		if (*targetList == target) return true;
		targetList++;
	} while (--i);
	return false;
}

static void __declspec(naked) ai_find_attackers_hack_target2() {
	__asm {
		mov  edi, [esp + 0x24 - 0x24 + 4] // critter (target)
		pushadc;
		lea  edx, [ebp - 4]; // start list of targets
		mov  ecx, edi;
		call TargetExistInList;
		test al, al;
		popadc;
		jnz  skip;
		inc  edx;
		mov  [ebp], edi;
skip:
		retn;
	}
}

static void __declspec(naked) ai_find_attackers_hack_target3() {
	__asm {
		mov  edi, [esp + 0x24 - 0x20 + 4] // critter (target)
		push eax;
		push edx;
		mov  eax, 4; // count targets
		lea  edx, [ebp - 4 * 2]; // start list of targets
continue:
		cmp  edi, [edx];
		je   break;          // target == targetList
		lea  edx, [edx + 4]; // next target in list
		dec  al;
		jnz  continue;
break:
		test al, al;
		pop  edx;
		pop  eax;
		jz   skip;
		xor  edi, edi;
		retn;
skip:
		inc  edx;
		retn;
	}
}

static void __declspec(naked) ai_find_attackers_hack_target4() {
	__asm {
		mov  eax, [ecx + eax]; // critter (target)
		pushadc;
		lea  edx, [esi - 4 * 3]; // start list of targets
		mov  ecx, eax;
		call TargetExistInList;
		test al, al;
		popadc;
		jnz  skip;
		inc  edx;
		mov  [esi], eax;
skip:
		retn;
	}
}

static void __declspec(naked) ai_danger_source_hack() {
	__asm {
		mov  eax, esi;
		call fo::funcoffs::ai_get_attack_who_value_;
		mov  dword ptr [esp + 0x34 - 0x1C + 4], eax; // attack_who
		retn;
	}
}

static long __fastcall sf_ai_check_weapon_switch(fo::GameObject* target, fo::GameObject* source) {
	fo::GameObject* item = fo::func::ai_search_inven_weap(source, 1, target);
	if (!item) return true; // no weapon in inventory, true to allow the to search continue weapon on the map

	long wType = fo::func::item_w_subtype(item, AttackType::ATKTYPE_RWEAPON_PRIMARY);
	if (wType < AttackSubType::THROWING) { // melee weapon, check the distance before switching
		if (fo::func::obj_dist(source, target) > 2) return false;
	}
	return true;
}

static void __declspec(naked) ai_try_attack_hook_switch_fix() {
	__asm {
		mov  eax, [eax + movePoints];
		test eax, eax;
		jz   noSwitch; // if movePoints == 0
		cmp  dword ptr [esp + 0x364 - 0x3C + 4], 0;
		jz   switch;   // no weapon in hand slot
		push edx;
		mov  edx, esi;
		call sf_ai_check_weapon_switch;
		pop  edx;
		test eax, eax;
		jz   noSwitch;
		mov  ecx, ebp;
switch:
		mov  eax, esi;
		jmp  fo::funcoffs::ai_switch_weapons_;
noSwitch:
		dec  eax; // -1 - for exit from ai_try_attack_
		retn;
	}
}

static int32_t RetryCombatMinAP;

static void __declspec(naked) RetryCombatHook() {
	static DWORD RetryCombatLastAP = 0;
	__asm {
		mov  RetryCombatLastAP, 0;
retry:
		call fo::funcoffs::combat_ai_;
process:
		cmp  dword ptr ds:[FO_VAR_combat_turn_running], 0;
		jle  next;
		call fo::funcoffs::process_bk_;
		jmp  process;
next:
		mov  eax, [esi + movePoints];
		cmp  eax, RetryCombatMinAP;
		jl   end;
		cmp  eax, RetryCombatLastAP;
		je   end;
		mov  RetryCombatLastAP, eax;
		mov  eax, esi;
		xor  edx, edx;
		jmp  retry;
end:
		retn;
	}
}

static int32_t __fastcall sf_ai_weapon_reload(fo::GameObject* weapon, fo::GameObject* ammo, fo::GameObject* critter) {
	fo::Proto* proto = nullptr;
	int32_t result = -1;
	long maxAmmo;

	fo::GameObject* _ammo = ammo;

	while (ammo)
	{
		result = fo::func::item_w_reload(weapon, ammo);
		if (result != 0) return result; // 1 - reload done, or -1 can't reload

		if (!proto) {
			proto = fo::GetProto(weapon->protoId);
			maxAmmo = proto->item.weapon.maxAmmo;
		}
		if (weapon->item.charges >= maxAmmo) break; // magazine is full

		long pidAmmo = ammo->protoId;
		fo::func::obj_destroy(ammo);
		ammo = nullptr;

		DWORD currentSlot = -1; // begin find at first slot
		while (fo::GameObject* ammoFind = fo::func::inven_find_type(critter, fo::item_type_ammo, &currentSlot))
		{
			if (ammoFind->protoId == pidAmmo) {
				ammo = ammoFind;
				break;
			}
		}
	}
	if (_ammo != ammo) {
		fo::func::obj_destroy(ammo);
		return 1; // notifies the engine that the ammo have already been destroyed
	}
	return result;
}

static void __declspec(naked) item_w_reload_hook() {
	__asm {
		cmp  dword ptr [eax + protoId], PID_SOLAR_SCORCHER;
		je   skip;
		push ecx;
		push esi;      // source
		mov  ecx, eax; // weapon
		call sf_ai_weapon_reload; // edx - ammo
		pop  ecx;
		retn;
skip:
		jmp fo::funcoffs::item_w_reload_;
	}
}

static int32_t __fastcall CheckWeaponRangeAndHitToTarget(fo::GameObject* source, fo::GameObject* target) {

	long weaponRange = fo::func::item_w_range(source, fo::ATKTYPE_RWEAPON_SECONDARY);
	long targetRange = fo::func::obj_dist(source, target);
	return (weaponRange >= targetRange); // 0 - don't use secondary mode

	///if (targetRange > weaponRange) return 0; // don't use secondary mode
	///long primaryHitChance = fo::func::determine_to_hit(source, target, 8, fo::ATKTYPE_RWEAPON_PRIMARY);
	///long secondaryHitChance = fo::func::determine_to_hit(source, target, 8, fo::ATKTYPE_RWEAPON_SECONDARY) + 5;
	///return (secondaryHitChance >= primaryHitChance); // 1 - use secondary mode
}

static void __declspec(naked) ai_pick_hit_mode_hook() {
	__asm {
		call fo::funcoffs::caiHasWeapPrefType_;
		test eax, eax;
		jnz  evaluation;
		retn;
evaluation:
		mov  edx, edi;
		mov  ecx, esi;
		jmp  CheckWeaponRangeAndHitToTarget;
	}
}

static void __declspec(naked) cai_perform_distance_prefs_hack() {
	__asm {
		mov  ecx, eax; // current distance to target
		mov  eax, esi;
		xor  ebx, ebx; // no called shot
		mov  edx, ATKTYPE_RWEAPON_PRIMARY;
		call fo::funcoffs::item_w_mp_cost_;
		mov  edx, [esi + movePoints];
		sub  edx, eax; // ap - cost = free AP's
		jle  moveAway; // <= 0
		lea  edx, [edx + ecx - 1];
		cmp  edx, 5;   // minimal threshold distance
		jge  skipMove; // distance >= 5?
moveAway:
		mov  ebx, 10;  // move away max distance
		retn;
skipMove:
		xor ebx, ebx;  // skip the move away at the beginning of the turn
		retn;
	}
}

/////////////////////////////////////////////////////////////////////////////////////////

static int32_t hitChance;
static int32_t loopCounter = 0;

static void __declspec(naked) combat_safety_invalidate_weapon_func_hook_init() {
	__asm {
		xor  ecx, ecx;
		cmp  edi, ANIM_fire_burst;
		setne cl; // set to 1 to skip check attempts for ANIM_fire_continuous weapon anim
		mov  loopCounter, ecx;
		mov  ecx, ebp;
		jmp  fo::funcoffs::combat_ctd_init_;
	}
}

static void __declspec(naked) combat_safety_invalidate_weapon_func_hook() {
	__asm {
		mov  hitChance, edx; // keep chance_to_hit
		jmp  fo::funcoffs::compute_spray_;
	}
}

static DWORD safety_invalidate_weapon_burst_loop = 0x42168B;
static DWORD safety_invalidate_weapon_burst_exit = 0x4217AB;

static void __declspec(naked) combat_safety_invalidate_weapon_func_hack1() { // jump
	__asm {
		dec  loopCounter;
		jz   break;
		jl   startLoop;
		mov  eax, hitChance;            // set hit chance for compute_spray_
		jmp  safety_invalidate_weapon_burst_loop;
startLoop:
		mov  eax, [esp + 0xF4 - 0x10];  // attacker
		mov  edx, STAT_iq;
		call fo::funcoffs::stat_level_; // number of checks depends on the attacker's IQ
		shl  eax, 4;                    // multiply by 16 (max 160 check attempts)
		mov  loopCounter, eax;
		mov  eax, hitChance;            // set hit chance for compute_spray_
		jmp  safety_invalidate_weapon_burst_loop;
break:
		jmp  safety_invalidate_weapon_burst_exit;
	}
}

static void __declspec(naked) combat_safety_invalidate_weapon_func_hack2() { // call
	__asm {
		add  ecx, 4;
		cmp  edi, ebp;
		//////////////
		jge  checkLoop; // all targets are checked
break:
		retn; // engine jl
checkLoop:
		dec  loopCounter;
		jz   break;
		lea  esp, [esp + 4];            // destroy ret addr
		jl   startLoop;
		mov  eax, hitChance;            // set hit chance for compute_spray_
		jmp  safety_invalidate_weapon_burst_loop;
startLoop:
		mov  eax, [esp + 0xF4  - 0x10]; // attacker
		mov  edx, STAT_iq;
		call fo::funcoffs::stat_level_; // number of checks depends on the attacker's IQ
		shl  eax, 4;                    // multiply by 16 (max 160 check attempts)
		mov  loopCounter, eax;
		mov  eax, hitChance;            // set hit chance for compute_spray_
		jmp  safety_invalidate_weapon_burst_loop;
	}
}

/////////////////////////////////////////////////////////////////////////////////////////

static void __fastcall CombatAttackHook(fo::GameObject* source, fo::GameObject* target) {
	sources[target] = source; // who attacked the 'target' for the last time
	targets[source] = target; // who was attacked by 'source' for the last time
}

static void __declspec(naked) combat_attack_hook() {
	__asm {
		push eax;
		push ecx;
		push edx;
		mov  ecx, eax;         // source
		call CombatAttackHook; // edx - target
		pop  edx;
		pop  ecx;
		pop  eax;
		jmp  fo::funcoffs::combat_attack_;
	}
}

void AI::init() {

	HookCalls(combat_attack_hook, {
		0x426A95, // combat_attack_this_
		0x42A796  // ai_attack_
	});

	RetryCombatMinAP = GetConfigInt("CombatAI", "NPCsTryToSpendExtraAP", -1);
	if (RetryCombatMinAP == -1) RetryCombatMinAP = GetConfigInt("Misc", "NPCsTryToSpendExtraAP", 0); // compatibility older versions
	if (RetryCombatMinAP > 0) {
		dlog("Applying retry combat patch.", DL_INIT);
		HookCall(0x422B94, RetryCombatHook); // combat_turn_
		dlogr(" Done", DL_INIT);
	}

	// Fixed weapon reloading for NPCs if the weapon has more ammo capacity than there are ammo in the pack
	HookCalls(item_w_reload_hook, {
		0x42AF15,           // cai_attempt_w_reload_
		0x42A970, 0x42AA56, // ai_try_attack_
	});

	// Adds for AI checks the distance to the target and the range of the weapon in choosing the best weapon shot mode
	HookCall(0x429F6D, ai_pick_hit_mode_hook);

	///////////////////// Combat AI behavior fixes /////////////////////////

	CombatAIBehaviorInit();

	// Fix friendly fire for shooting burst mode
	// Modification function of safe use of weapon when the AI uses burst shooting mode
	if (GetConfigInt("CombatAI", "CheckBurstFriendlyFire", 1)) {
		HookCall(0x421666, combat_safety_invalidate_weapon_func_hook_init);
		HookCall(0X4216A0, combat_safety_invalidate_weapon_func_hook);
		HookCall(0x4216F7, combat_safety_invalidate_weapon_func_hack1); // jle combat_safety_invalidate_weapon_func_hack1
		MakeCall(0x4217A0, combat_safety_invalidate_weapon_func_hack2);
	}

	// Enables the ability to use the AttackWho value from the AI-packet for the NPC
	if (GetConfigInt("CombatAI", "NPCAttackWhoFix", 0)) {
		MakeCall(0x428F70, ai_danger_source_hack, 3);
	}

	// Enables the use of the RunAwayMode value from the AI-packet for the NPC
	// the min_hp value will be calculated as a percentage of the maximum number of NPC health points, instead of using fixed min_hp values
	npcPercentMinHP = (GetConfigInt("CombatAI", "NPCRunAwayMode", 0) > 0);

	// When npc does not have enough AP to use the weapon, it begin looking in the inventory another weapon to use,
	// if no suitable weapon is found, then are search the nearby objects(weapons) on the ground to pick-up them
	// This fix prevents pick-up of the object located on the ground, if npc does not have the full amount of AP (ie, the action does occur at not the beginning of its turn)
	// or if there is not enough AP to pick up the object on the ground. Npc will not spend its AP for inappropriate use
	if (GetConfigInt("CombatAI", "ItemPickUpFix", 0)) {
		HookCall(0x429CAF, ai_search_environ_hook);
	}

	// Fixed switching weapons when action points is zero
	if (GetConfigInt("CombatAI", "NPCSwitchingWeaponFix", 0)) {
		HookCall(0x42AB57, ai_try_attack_hook_switch_fix);
	}

	// Fix adding duplicates the critters to the list of potential targets for AI
	MakeCall(0x428E75, ai_find_attackers_hack_target2, 2);
	MakeCall(0x428EB5, ai_find_attackers_hack_target3);
	MakeCall(0x428EE5, ai_find_attackers_hack_target4, 1);

	#ifndef NDEBUG
	if (GetConfigInt("Debugging", "AIBugFixes", 1) == 0) return;
	#endif

	// Fix to allow fleeing NPC to use drugs
	MakeCall(0x42B1DC, combat_ai_hack);
	// Fix minimum hp for use stimpack's (prevents premature flee)
	HookCall(0x428579, ai_check_drugs_hook);

	// Fix for NPC stuck in fleeing mode when the hit chance of a target was too low
	HookCall(0x42B1E3, combat_ai_hook_FleeFix);
	HookCalls(ai_try_attack_hook_FleeFix, { 0x42ABA8, 0x42ACE5 });
	// Disable fleeing when NPC cannot move closer to target
	BlockCall(0x42ADF6); // ai_try_attack_

	// Fix AI behavior for distance preference "Snipe"
	// The Attacker will try to shoot back from the attacker instead of always run away from him at the beginning of the turn
	MakeCall(0x42B086, cai_perform_distance_prefs_hack);
}

fo::GameObject* __stdcall AI::AIGetLastAttacker(fo::GameObject* target) {
	const auto itr = sources.find(target);
	return (itr != sources.end()) ? itr->second : 0;
}

fo::GameObject* __stdcall AI::AIGetLastTarget(fo::GameObject* source) {
	const auto itr = targets.find(source);
	return (itr != targets.end()) ? itr->second : 0;
}

void __stdcall AI::AICombatStart() {
	targets.clear();
	sources.clear();
}

void __stdcall AI::AICombatEnd() {
	targets.clear();
	sources.clear();
}

}
