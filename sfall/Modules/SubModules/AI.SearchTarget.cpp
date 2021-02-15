/*
 *    sfall
 *    Copyright (C) 2021  The sfall team
 *
 */

#include "..\..\FalloutEngine\Fallout2.h"

#include "..\..\main.h"
#include "..\..\Utils.h"

#include "..\HookScripts\CombatHs.h"

#include "..\..\Game\items.h"
#include "..\..\Game\objects.h"

#include "AI.Behavior.h"
#include "AI.SearchTarget.h"

namespace sfall
{

static bool npcAttackWhoFix = false;
static bool reFindNewTargets = false;

fo::GameObject* AISearchTarget::rememberTarget = nullptr;

/////////////////////////////////////////////////////////////////////////////////////////

static void __declspec(naked) ai_danger_source_hack() {
	__asm {
		mov  eax, esi;
		call fo::funcoffs::ai_get_attack_who_value_;
		mov  dword ptr [esp + 0x34 - 0x1C + 4], eax; // attack_who
		retn;
	}
}

/////////////////////////////////////////////////////////////////////////////////////////

static long __fastcall AICombatCheckBadShotLite(fo::GameObject* source, fo::GameObject* target) {
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

// Sets a target for the AI from whoHitMe if an alternative target was not found
// or chooses a near target between the currently find target and rememberTarget
//static void __declspec(naked) combat_ai_hook_revert_target() {
//	__asm {
//		cmp   rememberTarget, 0;
//		jnz   pickNearTarget;
//		test  edi, edi; // find target?
//		cmovz edi, [esi + whoHitMe];
//		mov   edx, edi;
//		jmp   fo::funcoffs::cai_perform_distance_prefs_;
//
//pickNearTarget:
//		test  edi, edi; // find target?
//		jz    pickRemember;
//		call  fo::funcoffs::obj_dist_; // dist1: source & target
//		push  eax;
//		mov   eax, esi;
//		mov   edx, rememberTarget
//		call  fo::funcoffs::obj_dist_; // dist2: source & rememberTarget
//		pop   edx;
//		cmp   eax, edx;             // compare distance
//		cmovbe edi, rememberTarget; // dist2 <= dist1
//		mov   edx, edi;
//		mov   eax, esi; // restore source
//		mov   rememberTarget, 0;
//		jmp   fo::funcoffs::cai_perform_distance_prefs_;
//
//pickRemember:
//		mov   edi, rememberTarget;
//		mov   edx, edi;
//		mov   rememberTarget, 0;
//		jmp   fo::funcoffs::cai_perform_distance_prefs_;
//	}
//}

//static void __declspec(naked) ai_danger_source_hook() {
//	__asm {
//		cmp  dword ptr [esp + 56], 0x42B235 + 5; // called fr. combat_ai_
//		je   fix;
//		jmp  fo::funcoffs::combat_check_bad_shot_;
//fix:
//		mov  ecx, eax; // source
//		call AICombatCheckBadShotLite;
//		cmp  eax, 1;   // check result
//		jne  skip;
//		// weapon out of range
//		cmp  rememberTarget, 0;
//		jnz  skip;
//		mov  edx, [esp + edi + 4]; // offset from target1
//		mov  rememberTarget, edx;  // remember the target to return to it later
//skip:
//		retn;
//	}
//}

static void __declspec(naked) ai_danger_source_hook_party_member() {
	__asm {
		cmp  dword ptr [esp + 56], 0x42B235 + 5; // called fr. combat_ai_
		je   fix;
		jmp  fo::funcoffs::combat_check_bad_shot_;
fix:
		mov  ecx, eax; // source
		call AICombatCheckBadShotLite;
		cmp  eax, 1;   // check result
		setg al;       // set 0 for result OK
		retn;
	}
}

/////////////////////////////////////////////////////////////////////////////////////////

// Анализирует ситуацию для текущей выбранной цели атакующего, и если ситуация неблагоприятная для атакующего
// то будет совершена попытка сменить текущую цель на альтернативную. Поиск цели осуществляется другой функцей движка.
static bool FindAlternativeTarget(fo::GameObject* source, fo::GameObject* target) {
	DEV_PRINTF1("\n[AI] Analyzing target: %s ", fo::func::critter_name(target));

	int distance = fo::func::obj_dist(source, target); // возвращает дистанцию 1 если объекты расположены вплотную
	if (distance <= 1) return false;

	bool shotIsBlock = fo::func::combat_is_shot_blocked(source, source->tile, target->tile, target, 0);

	int pathToTarget = fo::func::make_path_func(source, source->tile, target->tile, 0, 0, (void*)fo::funcoffs::obj_blocking_at_);
	if (shotIsBlock && pathToTarget == 0) { // shot and move block to target
		DEV_PRINTF("-> is blocking!");
		return true;                        // picking alternate target
	}

	fo::AIcap* cap = fo::func::ai_cap(source);
	if (shotIsBlock && pathToTarget >= 5) { // shot block to target, can move
		long dist_disposition = distance;
		switch (cap->disposition) {
		case fo::AIpref::defensive:
			pathToTarget += 5;
			dist_disposition += 5;
			break;
		case fo::AIpref::aggressive: // AI aggressive never does not change its target if the move-path to the target is not blocked
			pathToTarget = 1;
			break;
		case fo::AIpref::berserk:
			pathToTarget /= 2;
			dist_disposition -= 5;
			break;
		}
		// поиск цели, возможно рядом есть альтернативная цель
		fo::GameObject* enemy = fo::func::ai_find_nearest_team(source, target, 1);
		if (enemy && enemy != target) {
			if (fo::func::obj_dist(source, enemy) <= 1) {
				DEV_PRINTF("-> has closer other enemy located!");
				return true; // поблизости имеется альтернативный враг -> picking alternate target
			}
			int path = fo::func::make_path_func(source, source->tile, enemy->tile, 0, 0, (void*)fo::funcoffs::obj_blocking_at_);
			if (path > 0 && path < pathToTarget) {
				DEV_PRINTF("-> has near other enemy located!");
				return true; // поблизости имеется альтернативный враг -> picking alternate target
			}
		}
		// dist=10 ap=8 cost=5 move=3
		// pathToTarget=6 8*2=16
		// 10 >= 8 && 6 >= 16
		// если дистанция до цели превышает 9 гексов а реальный путь до нее больше чем имеющихся очков действий в два раза тогда берем новую цель
		if (dist_disposition >= 10 && pathToTarget >= (source->critter.getAP() * 2)) {
			DEV_PRINTF("-> is far located!");
			return true; // target is far -> picking alternate target
		}
		DEV_PRINTF("-> shot is blocked. I can move to target.");
	}
	else if (shotIsBlock == false) { // can shot and move to target
		fo::GameObject* itemHand = fo::func::inven_right_hand(source); // current item
		if (!itemHand && pathToTarget == 0) {
			DEV_PRINTF("-> I unarmed and block move path to target!");
			return true; // no item and move block to target -> picking alternate target
		}
		if (!itemHand) return false; // безоружны

		fo::Proto* proto;
		if (GetProto(itemHand->protoId, &proto) && proto->item.type == fo::ItemType::item_type_weapon) {
			int hitMode = fo::func::ai_pick_hit_mode(source, itemHand, target);
			int maxRange = fo::func::item_w_range(source, hitMode);

			// атакующий не сможет атаковать если доступ к цели заблокирован и он имеет оружие ближнего действия
			if (maxRange == 1 && !pathToTarget) {
				DEV_PRINTF("-> move path is blocking and my weapon no have range!");
				return true;
			}

			int diff = distance - maxRange; // 3 - 1 = 2
			if (diff > 0) { // shot out of range (положительное число не хватает дистанции для оружия)
				/*if (!pathToTarget) return true; // move block to target and shot out of range -> picking alternate target (это больше не нужно т.к. есть твик к подходу)*/

				if (cap->disposition == fo::AIpref::coward && diff > GetRandom(8, 12)) {
					DEV_PRINTF("-> is located beyond range of weapon. I'm afraid to approach target!");
					return true;
				}

				long cost = game::Items::item_w_mp_cost(source, hitMode, 0);
				if (diff > (source->critter.getAP() - cost)) {
					DEV_PRINTF("-> I don't have enough AP to move to target and make shot!");
					return true; // не хватит очков действия для подхода и выстрела -> picking alternate target
				}
			}
		}    // can shot (or move), and hand item is not weapon
	} else { // block shot and can move
		DEV_PRINTF("-> shot is blocked. I can move to target [#2]");
		// Note: pathToTarget здесь будет всегда иметь значение 1-4
	}
	return false; // can shot and move / can shot and block move
}

static const char* reTargetMsg = "\n[AI] I can't get at my target. Try picking alternate.";
#ifndef NDEBUG
static const char* targetGood  = "-> is possible attack!\n";
#endif

static void __declspec(naked) ai_danger_source_hack_find() {
	static const uint32_t ai_danger_source_hack_find_PickRet = 0x42908C;
	static const uint32_t ai_danger_source_hack_find_Ret  = 0x4290BB;
	__asm {
		push eax;
		push edx;
		mov  edx, eax; // source.who_hit_me target
		mov  ecx, esi; // source
		call FindAlternativeTarget;
		pop  edx;
		test al, al;
		pop  eax;
		jnz  reTarget;
		add  esp, 0x1C;
		pop  ebp;
		pop  edi;
#ifndef NDEBUG
		push eax;
		push targetGood;
		call fo::funcoffs::debug_printf_;
		add  esp, 4;
		pop  eax;
#endif
		jmp  ai_danger_source_hack_find_Ret;
reTarget:
		push reTargetMsg;
		call fo::funcoffs::debug_printf_;
		add  esp, 4;
		jmp  ai_danger_source_hack_find_PickRet;
	}
}

static bool AIFindAlternativeTarget(fo::GameObject* source, fo::GameObject* target) {
	bool result = FindAlternativeTarget(source, target);

	#ifdef NDEBUG
		if (result) fo::func::debug_printf(reTargetMsg);
	#else
		DEV_PRINTF((result) ? reTargetMsg : "-> is possible attack!\n");
	#endif

	return result;
}

/////////////////////////////////////////////////////////////////////////////////////////

// Changes the behavior of the AI so that the AI moves to its target to perform an attack/shot when the range of its weapon is less than
// the distance to the target or the AI will choose the nearest target if any other targets are available
static long __fastcall AICombatCheckBadShot(fo::GameObject* source, fo::GameObject* target, long type) {
	if (type) {
		long result = AICombatCheckBadShotLite(source, target);
		if (result == 1 && !AISearchTarget::rememberTarget && type == 2) AISearchTarget::rememberTarget = target;
		return result;
	}
	return fo::func::combat_check_bad_shot(source, target, fo::AttackType::ATKTYPE_RWEAPON_PRIMARY, 0);
}

fo::GameObject* ai_danger_source_Extended(fo::GameObject* source, long type) {
	if (!source) return nullptr;

	fo::GameObject* targets[4];
	targets[0] = nullptr;

	bool isIgnoreFlee = false;
	long isPartyMember = fo::func::isPartyMember(source);

	fo::AIcap* cap = fo::func::ai_cap(source);
	fo::AIpref::attack_who attack_who = (isPartyMember || npcAttackWhoFix) // [add ext] NPCAttackWhoFix option
										? (fo::AIpref::attack_who)cap->attack_who
										: fo::AIpref::attack_who::no_attack_mode;

	if (isPartyMember) {
		if (cap->distance != fo::AIpref::distance::charge) {
			switch (cap->disposition) { // +1 in vanilla, but sfall have the correct values for 'disposition'
				case fo::AIpref::disposition::custom:
				case fo::AIpref::disposition::coward:
				case fo::AIpref::disposition::defensive:
				case fo::AIpref::disposition::aggressive:
					isIgnoreFlee = true;
					break;
				case fo::AIpref::disposition::berserk:
				case fo::AIpref::disposition::none:
					break;
				//default:
			}
		}

		switch (attack_who) {
			case fo::AIpref::attack_who::whomever_attacking_me:
			{
				fo::GameObject* lastTarget = fo::func::combatAIInfoGetLastTarget(fo::var::obj_dude);
				if (!lastTarget) lastTarget = fo::var::obj_dude->critter.getHitTarget(); // [add ext] Possible fix if suddenly last target is not be set

				if (!lastTarget || (lastTarget->critter.teamNum == source->critter.teamNum) ||
					(isIgnoreFlee && lastTarget->critter.combatState & fo::CombatStateFlag::IsFlee) ||
					/* [FIX] Fixed the bug of taking tile from obj_dude->who_hit_me instead of lastTarget */
					(fo::func::make_path_func(source, source->tile, lastTarget->tile, 0, 0, (void*)fo::funcoffs::obj_blocking_at_) == 0) ||
					(AICombatCheckBadShot(source, lastTarget, type))) // [add ext] TryToFindTargets option
				{
					// debug msg "\nai_danger_source: %s couldn't attack at target!  Picking alternate!"
					fo::func::debug_printf((const char*)0x5010BC, fo::func::critter_name(source));
					goto SearchTargets;
				}
				return lastTarget;
			}
			case fo::AIpref::attack_who::strongest:
			case fo::AIpref::attack_who::weakest:
			case fo::AIpref::attack_who::closest:
				source->critter.whoHitMe = nullptr; // unset the target to find new ones
				// [tweak FIX] Tweak for finding new targets for party members
				// Save the current target in the "target1" variable and find other potential targets
				targets[0] = source->critter.whoHitMe;
				break;
			//default:
		}
	}
SearchTargets:
	fo::GameObject* sourceTarget = source->critter.whoHitMe;

	if (sourceTarget && sourceTarget != source) {
		if (sourceTarget->critter.IsDead()) {
			if (sourceTarget->critter.teamNum != source->critter.teamNum) {           // the abuser is dead and not from the source team
				targets[0] = fo::func::ai_find_nearest_team(source, sourceTarget, 1); // search for the team's first nearby enemy
			}
		} else {
			if (attack_who == fo::AIpref::attack_who::whomever || attack_who == fo::AIpref::attack_who::no_attack_mode) {
				// NPCs (or PM with whomever) always attack the target that is set to who_hit_me
				// [add ext] Additional function allows to analyze the current target (TryToFindTargets option)
				if (reFindNewTargets && AIFindAlternativeTarget(source, sourceTarget)) {
					goto FindNewTargets;
				}
				return sourceTarget;
			}
		}
	}

FindNewTargets:
	fo::func::ai_find_attackers(source, &targets[1], &targets[2], &targets[3]);

	if (isIgnoreFlee) { // remove from the list of targets if the Flee flag is set for the target
		for (size_t i = 0; i < 4; i++) {
			if (targets[i] && targets[i]->critter.combatState & fo::CombatStateFlag::IsFlee) targets[i] = nullptr;
		}
	}

	DWORD funcComp = fo::funcoffs::compare_nearer_;
	if (attack_who == fo::AIpref::attack_who::strongest) {
		funcComp = fo::funcoffs::compare_strength_;
	}
	else if (attack_who == fo::AIpref::attack_who::weakest) {
		funcComp = fo::funcoffs::compare_weakness_;
	}
	fo::func::qsort(&targets, 4, 4, funcComp);

	FindTargetHook_Invoke(targets, source); // [HOOK_FINDTARGET]

	if (type) type++;

	// select the first available target
	for (size_t i = 0; i < 4; i++)
	{
		if (targets[i]) {
			if ((game::Objects::is_within_perception(source, targets[i], 3) == 0) || // [HOOK_WITHINPERCEPTION]
				(fo::func::make_path_func(source, source->tile, targets[i]->tile, 0, 0, (void*)fo::funcoffs::obj_blocking_at_) == 0) ||
				(AICombatCheckBadShot(source, targets[i], type))) // [add ext] TryToFindTargets option
			{
				fo::func::debug_printf((const char*)0x50110); // "\nai_danger_source: I couldn't get at my target!  Picking alternate!"
				continue;
			}
			return targets[i];
		}
	}
	return nullptr;
}

void AISearchTarget::init() {

	reFindNewTargets = (GetConfigInt("CombatAI", "TryToFindTargets", 0) > 0);

	//switch (GetConfigInt("CombatAI", "TryToFindTargets", 0)) {
	//case 1:
	//	MakeJump(0x4290B6, ai_danger_source_hack_find);
	//	break;
	//case 2: // w/o logic
	//	SafeWrite16(0x4290B3, 0xDFEB); // jmp 0x429094
	//	SafeWrite8(0x4290B5, 0x90);
	//}

	// Changes the behavior of the AI so that the AI moves to its target to perform an attack/shot when the range of its weapon is less than
	// the distance to the target or the AI will choose the nearest target if any other targets are available
	//HookCall(0x42918A, ai_danger_source_hook);
	//HookCall(0x42903A, ai_danger_source_hook_party_member);
	//HookCall(0x42B240, combat_ai_hook_revert_target); // also need for TryToFindTargets option


	// Enables the ability to use the AttackWho value from the AI-packet for the NPC
	if (GetConfigInt("CombatAI", "NPCAttackWhoFix", 0)) {
		MakeCall(0x428F70, ai_danger_source_hack, 3);
		npcAttackWhoFix = true;
	}
}

}