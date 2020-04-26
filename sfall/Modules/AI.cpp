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

//#include "HookScripts\MiscHS.h"

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

static int32_t __fastcall CheckWeaponRangeAndHitToTarget(fo::GameObject* source, fo::GameObject* target, fo::GameObject* weapon) {

	long weaponRange = fo::func::item_w_range(source, fo::ATKTYPE_RWEAPON_SECONDARY);
	long targetRange = fo::func::obj_dist(source, target);
	if (targetRange > weaponRange) return 0; // don't use secondary mode

	long primaryHitChance = fo::func::determine_to_hit(source, target, 8, fo::ATKTYPE_RWEAPON_PRIMARY);
	long secondaryHitChance = fo::func::determine_to_hit(source, target, 8, fo::ATKTYPE_RWEAPON_SECONDARY) + 10;
	return (secondaryHitChance >= primaryHitChance); // 1 - use secondary mode
}

static void __declspec(naked) ai_pick_hit_mode_hook() {
	__asm {
		call fo::funcoffs::caiHasWeapPrefType_;
		test eax, eax;
		jnz  evaluation;
		retn;
evaluation:
		push ebp;
		mov  edx, edi;
		mov  ecx, esi;
		call CheckWeaponRangeAndHitToTarget;
		retn;
	}
}

///////////////////////////////////////////////////////////////////////////////

static int32_t __fastcall sf_combat_check_bad_shot(fo::GameObject* source, fo::GameObject* target) {
	long distance = 1, tile = -1;
	long hitMode = fo::ATKTYPE_RWEAPON_PRIMARY;

	if (target && target->critter.damageFlags & fo::DAM_DEAD) return 4; // target is dead

	fo::GameObject* item = fo::func::inven_right_hand(source);
	if (!item) return 0; // unarmed

	if (target) {
		tile = target->tile;
		distance = fo::func::obj_dist(source, target);
		hitMode = fo::func::ai_pick_hit_mode(source, item, target);
	}

	long flags = source->critter.damageFlags;
	if (flags & fo::DAM_CRIP_ARM_LEFT && flags & fo::DAM_CRIP_ARM_RIGHT) {
		return 3; // crippled both hands
	}
	///if (flags & (fo::DAM_CRIP_ARM_RIGHT | fo::DAM_CRIP_ARM_LEFT) && fo::func::item_w_is_2handed(item)) {
	///	return 3; // one of the hands is crippled, can't use a two-handed weapon
	///}

	long attackRange = fo::func::item_w_range(source, hitMode);
	if (attackRange > 1 && fo::func::combat_is_shot_blocked(source, source->tile, tile, target, 0)) {
		return 2; // shot to target is blocked
	}
	return (attackRange < distance); // 1 - target is out of range of the attack
}

fo::GameObject* rememberTarget = nullptr;

// Sets a target for the AI from whoHitMe if an alternative target was not found
// or chooses a near target between the currently find target and rememberTarget
static void __declspec(naked) combat_ai_hook_revert_target() {
	__asm {
		cmp   rememberTarget, 0;
		jnz   pickNearTarget;
		test  edi, edi; // find target?
		cmovz edi, [esi + whoHitMe];
		mov   edx, edi;
		jmp   fo::funcoffs::cai_perform_distance_prefs_;

pickNearTarget:
		test  edi, edi; // find target?
		jz    pickRemember;
		call  fo::funcoffs::obj_dist_; // dist1: source & target
		push  eax;
		mov   eax, esi;
		mov   edx, rememberTarget
		call  fo::funcoffs::obj_dist_; // dist2: source & rememberTarget
		pop   edx;
		cmp   eax, edx;             // compare distance
		cmovbe edi, rememberTarget; // dist2 <= dist1
		mov   edx, edi;
		mov   eax, esi; // restore source
		mov   rememberTarget, 0;
		jmp   fo::funcoffs::cai_perform_distance_prefs_;

pickRemember:
		mov   edi, rememberTarget;
		mov   edx, edi;
		mov   rememberTarget, 0;
		jmp   fo::funcoffs::cai_perform_distance_prefs_;
	}
}

static void __declspec(naked) ai_danger_source_hook() {
	__asm {
		cmp  dword ptr [esp + 56], 0x42B235 + 5; // called fr. combat_ai_
		je   fix;
		jmp  fo::funcoffs::combat_check_bad_shot_;
fix:
		mov  ecx, eax; // source
		call sf_combat_check_bad_shot;
		cmp  eax, 1;   // check result
		jne  skip;
		// weapon out of range
		cmp  rememberTarget, 0;
		jnz  skip;
		mov  edx, [esp + edi + 4]; // offset from target1
		mov  rememberTarget, edx;  // remember the target to return to it later
skip:
		retn;
	}
}

static void __declspec(naked) ai_danger_source_hook_party_member() {
	__asm {
		cmp  dword ptr [esp + 56], 0x42B235 + 5; // called fr. combat_ai_
		je   fix;
		jmp  fo::funcoffs::combat_check_bad_shot_;
fix:
		mov  ecx, eax; // source
		call sf_combat_check_bad_shot;
		cmp  eax, 1;   // check result
		setg al;       // set 0 for result OK
		retn;
	}
}

static int32_t __fastcall ai_try_move_steps_closer(fo::GameObject* source, fo::GameObject* target) {
	///long result = sf_is_within_perception(source, target);
	///if (!result) return 1; // the attacker can't see the target

	long getTile = -1, dist = -1;

	fo::GameObject* itemHand = fo::func::inven_right_hand(source);
	if (itemHand) {
		long mode = fo::func::ai_pick_hit_mode(source, itemHand, target);
		long cost = fo::func::item_w_mp_cost(source, mode, 0);

		// check the distance and number of remaining AP's
		long weaponRange = fo::func::item_w_range(source, mode);
		dist = fo::func::obj_dist(source, target) - weaponRange; // required approach distance
		long ap = source->critter.movePoints - dist; // subtract the number of action points to the move, leaving the number for the shot
		long remainingAP = ap - cost;

		bool notEnoughAP = (cost > ap); // check whether the critter has enough AP to perform the attack

		char rotationData[800];
		long pathLength = fo::func::make_path_func(source, source->tile, target->tile, rotationData, 0, (void*)fo::funcoffs::obj_blocking_at_);

		if (pathLength > 0) {
			if (notEnoughAP) return 1;

			dist += remainingAP; // add remaining AP's to distance
			if (dist > pathLength) dist = pathLength;

			getTile = source->tile;

			// get tile to perform an attack
			for (long i = 0; i < dist; i++)	{
				getTile = fo::func::tile_num_in_direction(getTile, rotationData[i], 1);
			}
		}
		else if (!notEnoughAP) {
			long dir = fo::func::tile_dir(source->tile, target->tile);
			getTile = fo::func::tile_num_in_direction(source->tile, dir, dist); // get tile to move to

			// make a path and check the actual distance of the path
			pathLength = fo::func::make_path_func(source, source->tile, getTile, 0, 0, (void*)fo::funcoffs::obj_blocking_at_);
			if (pathLength > dist) {
				long diff = pathLength - dist;
				if (diff > remainingAP) return 1;
			}

			long _dir = dir, _getTile = getTile;
			long i = 0;
			while (true)
			{
				// check if the tile is blocked
				if (!fo::func::obj_blocking_at(source, _getTile, source->elevation)) {
					getTile = _getTile;
					break; // OK, tile is free
				}
				if (++i > 2) return 1; // neighboring tiles are also blocked
				if (i == 1) {
					if (++_dir > 5) _dir = 0;
				} else {
					_dir = dir - 1;
					if (_dir < 0) _dir = 5;
				}
				if (remainingAP < 1) _dir = (_dir + 3) % 6; // invert direction, если в резерве имеются AP
				_getTile = fo::func::tile_num_in_direction(getTile, _dir, 1);
			}
			// Note: here the value of dist and the distance between getTile and source tile may not match by 1 unit
		}
		// make sure that the distance is within the range of the weapon and the attack is not blocked
		if (getTile != -1 && (fo::func::obj_dist_with_tile(source, getTile, target, target->tile) > weaponRange ||
			fo::func::combat_is_shot_blocked(source, getTile, target->tile, target, 0)))
		{
			return 1;
		}
	}
	if (getTile == -1) return 1;
	//if (dist == -1) dist = source->critter.movePoints; // in dist - the distance to move

	fo::func::register_begin(fo::RB_RESERVED);
	fo::func::register_object_move_to_tile(source, getTile, source->elevation, dist, -1);
	long result = fo::func::register_end();
	if (!result) __asm call fo::funcoffs::combat_turn_run_;

	return result;
}

static void __declspec(naked) ai_try_attack_hook_out_of_range() {
	__asm {
		pushadc;
		mov  ecx, eax;
		call ai_try_move_steps_closer;
		test eax, eax;
		popadc;
		jnz  defaultMove;
		retn;
defaultMove:
		jmp fo::funcoffs::ai_move_steps_closer_; // default behavior
	}
}

//////////////////////////////////////////////////////////////////////////////////////////////

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

	// Adds for AI an evaluation the hit chance, and checks the distance to the target and the range of the weapon in choosing the best weapon shot mode
	HookCall(0x429F6D, ai_pick_hit_mode_hook);

	///////////////////// Combat AI behavior fixes /////////////////////////

	CombatAIBehaviorInit();

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

	// Changes the behavior of the AI so that the AI moves to its target to perform an attack/shot when the range of its weapon is less than 
	// the distance to the target or the AI will choose the nearest target if any other targets are available
	HookCall(0x42918A, ai_danger_source_hook);
	HookCall(0x42903A, ai_danger_source_hook_party_member);
	HookCall(0x42B240, combat_ai_hook_revert_target);
	// Forces the AI to move to target closer to make an attack on the target when the distance exceeds the range of the weapon
	HookCall(0x42ABD7, ai_try_attack_hook_out_of_range);
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
