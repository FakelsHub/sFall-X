/*
 *    sfall
 *    Copyright (C) 2021  The sfall team
 *
 */

#include "..\..\FalloutEngine\Fallout2.h"

#include "..\..\main.h"
#include "..\..\Utils.h"

#include "..\..\Modules\HookScripts\CombatHs.h"

#include "..\items.h"
#include "..\objects.h"
#include "..\tilemap.h"

#include "AI.Behavior.h"
#include "AI.Combat.h"
#include "AI.Inventory.h"
#include "AI.FuncHelpers.h"

#include "AI.SearchTarget.h"

namespace game
{
namespace imp_ai
{

namespace sf = sfall;

static bool npcAttackWhoFix = false;
static long reFindNewTargets = 0;

fo::GameObject* rememberTarget = nullptr;

// Sets a target for the AI from whoHitMe if an alternative target was not found
// or chooses a near target between the currently find target and rememberTarget
fo::GameObject* __fastcall AISearchTarget::RevertTarget(fo::GameObject* source, fo::GameObject* target) {
	if (rememberTarget) { // rememberTarget: первая цель до которой превышен радиус действия атаки
		DEV_PRINTF1("\n[AI] I have remember target: %s", fo::func::critter_name(rememberTarget));
		if (target && target != rememberTarget) {
			// выбрать лучшую цель
			long hit1 = fo::func::determine_to_hit_no_range(source, target, fo::BodyPart::Uncalled, fo::AttackType::ATKTYPE_RWEAPON_PRIMARY);
			long hit2 = fo::func::determine_to_hit_no_range(source, rememberTarget, fo::BodyPart::Uncalled, fo::AttackType::ATKTYPE_RWEAPON_PRIMARY);

			// выбрать ближайшую цель (ближайшая всегда rememberTarget?)
			long dist1 = fo::func::obj_dist(source, target);
			long dist2 = fo::func::obj_dist(source, rememberTarget);
			if ((hit1 < hit2) && (dist1 > dist2)) target = rememberTarget;
		} else {
			target = rememberTarget;
		}
		rememberTarget = nullptr;
	}
	else if (!target && source->critter.getHitTarget() /*&& source->critter.getHitTarget()->critter.IsNotDead()*/) {
		target = source->critter.getHitTarget(); // в случае если новая цель не была найдена
		DEV_PRINTF1("\n[AI] Get my hit target: %s", fo::func::critter_name(target));
	}
	return target;
}

static void __declspec(naked) combat_ai_hook_revert_target() {
	__asm {
		push ecx
		mov  ecx, esi; // source
		call AISearchTarget::RevertTarget; // edx - target from ai_danger_source_
		mov  edi, eax;
		pop  ecx;
		mov  edx, edi; // restore target
		mov  eax, esi; // restore source
		jmp  fo::funcoffs::cai_perform_distance_prefs_;
	}
}

/////////////////////////////////////////////////////////////////////////////////////////

// Анализирует ситуацию для текущей цели атакующего, и если ситуация неблагоприятная для атакующего
// то будет совершена попытка сменить текущую цель на альтернативную. Поиск целей осуществляется функцей движка.
static bool CheckAttackerTarget(fo::GameObject* source, fo::GameObject* target) {
	DEV_PRINTF1("\n[AI] Analyzing target: %s ", fo::func::critter_name(target));

	int distance = fo::func::obj_dist(source, target); // возвращает дистанцию 1 если цель расположена вплотную
	if (distance <= 1) {
		if (fo::func::determine_to_hit(source, target, fo::BodyPart::Uncalled, fo::AttackType::ATKTYPE_RWEAPON_PRIMARY) >= 50) return false;
	}
	bool shotIsBlock = fo::func::combat_is_shot_blocked(source, source->tile, target->tile, target, 0);

	int pathToTarget = fo::func::make_path_func(source, source->tile, target->tile, 0, 0, (void*)fo::funcoffs::obj_blocking_at_);
	if (shotIsBlock && pathToTarget == 0) { // shot and move block to target
		DEV_PRINTF("-> is blocking!");
		return true;                        // picking alternate target
	}

	fo::AIcap* cap = AICombat::AttackerAI();
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
		// если дистанция до цели превышает 9 гексов, а реальный путь до нее больше чем имеющихся очков действий в два раза, тогда берем новую цель
		if (dist_disposition >= 10 && pathToTarget >= (source->critter.getMoveAP() * 2)) {
			DEV_PRINTF("-> is far located!");
			return true; // target is far -> picking alternate target
		}
		DEV_PRINTF("-> shot is blocked. I can move to target.");
	}
	else if (shotIsBlock == false){ // can shot and move to target
		fo::GameObject* itemHand = fo::func::inven_right_hand(source); // current item
		if (!itemHand && pathToTarget == 0) {
			DEV_PRINTF("->. I unarmed and block move path to target!");
			return true; // no item and move block to target -> picking alternate target
		}
		if (!itemHand) return false; // безоружный

		fo::Proto* proto;
		if (fo::util::GetProto(itemHand->protoId, &proto) && proto->item.type == fo::ItemType::item_type_weapon) {
			int hitMode = fo::func::ai_pick_hit_mode(source, itemHand, target);
			int maxRange = fo::func::item_w_range(source, hitMode);

			// атакующий не сможет атаковать если доступ к цели заблокирован и он имеет оружие ближнего действия
			if (maxRange == 1 && !pathToTarget) {
				DEV_PRINTF("-> move path is blocking and my weapon no have range!");
				return true;
			}

			int diff = distance - maxRange;
			if (diff > 0) { // shot out of range (положительное число не хватает дистанции для оружия)
				/*if (!pathToTarget) return true; // move block to target and shot out of range -> picking alternate target (это больше не нужно т.к. есть твик к подходу)*/

				if (cap->disposition == fo::AIpref::coward && diff > sf::GetRandom(8, 12)) {
					DEV_PRINTF("-> is located beyond range of weapon. I'm afraid to approach target!");
					return true;
				}

				long cost = game::Items::item_w_mp_cost(source, hitMode, 0);
				if (source->critter.getAP(diff) < cost) {
					DEV_PRINTF("-> I don't have enough AP to move to target and make shot!");
					return true; // не хватит очков действия для подхода и выстрела -> picking alternate target
				}
			}
		}    // can shot (or move), and hand item is not weapon
	} else { // block shot and can move
		// Note: pathToTarget здесь будет всегда иметь значение 1-4
		DEV_PRINTF1("-> shot is blocked. I can move to target. [pathToTarget: %d]", pathToTarget);
	}
	// can shot and move / can shot and block move
	return (fo::func::determine_to_hit(source, target, fo::BodyPart::Uncalled, fo::AttackType::ATKTYPE_RWEAPON_PRIMARY) <= 30);
}

static const char* reTargetMsg = "\n[AI] I can't get at my target. Try picking alternate.";

static bool AICheckCurrentAttackerTarget(fo::GameObject* source, fo::GameObject* target) {
	bool result = CheckAttackerTarget(source, target);

	#ifdef NDEBUG
		if (result) fo::func::debug_printf(reTargetMsg);
	#else
		DEV_PRINTF((result) ? reTargetMsg : "-> is possible attack!\n");
	#endif

	return result;
}

/////////////////////////////////////////////////////////////////////////////////////////

static CombatShootResult CombatCheckBadShotLite(fo::GameObject* source, fo::GameObject* target) {
	long distance = 1, tile = -1;
	long hitMode = fo::ATKTYPE_RWEAPON_PRIMARY;

	if (target && target->critter.damageFlags & fo::DAM_DEAD) return CombatShootResult::TargetDead; // target is dead

	fo::GameObject* item = fo::func::inven_right_hand(source);
	if (!item) return CombatShootResult::Ok; // for unarmed

	long flags = source->critter.damageFlags;
	if (flags & fo::DAM_CRIP_ARM_LEFT && flags & fo::DAM_CRIP_ARM_RIGHT) {
		return CombatShootResult::CrippledHands; // crippled both hands
	}
	///if (flags & (fo::DAM_CRIP_ARM_RIGHT | fo::DAM_CRIP_ARM_LEFT) && fo::func::item_w_is_2handed(item)) {
	///	return 6; // one of the hands is crippled, can't use a two-handed weapon
	///}

	if (target) {
		tile = target->tile;
		distance = fo::func::obj_dist(source, target);
		hitMode = fo::func::ai_pick_hit_mode(source, item, target);
	}

	long attackRange = fo::func::item_w_range(source, hitMode);
	if (attackRange > 1) {
		if (AIHelpersExt::IsGunOrThrowingWeapon(item, hitMode) == false) {
			return CombatShootResult::Ok; // for melee weapon
		}
		if (fo::func::combat_is_shot_blocked(source, source->tile, tile, target, 0)) {
			return CombatShootResult::ShootBlock; // shot to target is blocked
		}
	}
	return (attackRange >= distance) ? CombatShootResult::Ok : CombatShootResult::OutOfRange; // 2 - target is out of range of the attack
}

// Changes the behavior of the AI so that the AI moves to its target to perform an attack/shot when the range of its weapon is less than
// the distance to the target or the AI will choose the nearest target if any other targets are available
static long AICombatCheckBadShot(fo::GameObject* source, fo::GameObject* target, long type) {
	if (type) {
		CombatShootResult result = CombatCheckBadShotLite(source, target);
		DEV_PRINTF2("\n[AI] DangerSource: %s <CombatCheckBadShotLite> result: %d", fo::func::critter_name(target), result);

		if (type & 1 && result == CombatShootResult::OutOfRange) return 0; // good

		if (type & 2 && !rememberTarget && (result == CombatShootResult::OutOfRange || result == CombatShootResult::ShootBlock)) {
			rememberTarget = target; // запоминаем первую цель
		}
		if (result == CombatShootResult::Ok) {
			if (type & 4) { // for retarget
				long hit = fo::func::determine_to_hit_no_range(source, target, fo::BodyPart::Uncalled, fo::AttackType::ATKTYPE_RWEAPON_PRIMARY);
				if (hit <= 30) return 1; // bad
			}
			else if (type & 8) { // for attack bad to hit
				long minToHit = AICombat::AttackerAI()->min_to_hit;
				long hit = fo::func::determine_to_hit_no_range(source, target, fo::BodyPart::Uncalled, AICombat::AttackerHitMode());
				if (hit < minToHit) return 1; // bad
			}
		}
		return (long)result;
	}
	return fo::func::combat_check_bad_shot(source, target, fo::AttackType::ATKTYPE_RWEAPON_PRIMARY, 0);
}

/////////////////////////////////////////////////////////////////////////////////////////

fo::GameObject* AISearchTarget::AIDangerSource(fo::GameObject* source, long type) {
	fo::GameObject* targets[4];
	targets[0] = nullptr;

	bool isIgnoreFlee = false;
	long isPartyMember = fo::func::isPartyMember(source);

	fo::AIcap* cap = AICombat::AttackerAI();
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
					(isIgnoreFlee && lastTarget->critter.IsFleeing()) ||
					/* [FIX] Fixed the bug of taking tile from obj_dude->who_hit_me instead of lastTarget */ // TODO add hack fix to AI.cpp for phobos
					(fo::func::make_path_func(source, source->tile, lastTarget->tile, 0, 0, (void*)fo::funcoffs::obj_blocking_at_) == 0) ||
					(AICombatCheckBadShot(source, lastTarget, type))) // [add ext]
				{
					fo::func::debug_printf((const char*)0x5010BC, fo::func::critter_name(source)); // \nai_danger_source: %s couldn't attack at target!  Picking alternate!"
					goto SearchTargets;
				}
				return lastTarget;
			}
			case fo::AIpref::attack_who::strongest:
			case fo::AIpref::attack_who::weakest:
			case fo::AIpref::attack_who::closest:
				// [tweak FIX] Tweak for finding new targets for party members
				// Save the current target in the "target1" variable and find other potential targets
				fo::GameObject* hitTarget = source->critter.getHitTarget();
				if (hitTarget && hitTarget->critter.IsNotDead()) {
					targets[0] = hitTarget;
				} else {
					targets[0] = AIHelpersExt::GetNearestEnemyCritter(source);
				}
				goto FindNewTargets;
				//source->critter.whoHitMe = nullptr; // unset the target to find new ones
				//break;
			//default:
		}
	}
SearchTargets:
	fo::GameObject* sourceTarget = source->critter.whoHitMe;

	if (sourceTarget && sourceTarget != source) {
		if (sourceTarget->critter.IsDead()) {
ReFindNewTargets:
			if (sourceTarget->critter.teamNum != source->critter.teamNum) {           // the abuser is dead and not from the source team
				targets[0] = fo::func::ai_find_nearest_team(source, sourceTarget, 1); // search for the team's first nearby enemy
			}
		} else {
			if (attack_who == fo::AIpref::attack_who::whomever || attack_who == fo::AIpref::attack_who::no_attack_mode) {
				// NPCs (or PM with whomever) always attack the target that is set to who_hit_me
				// [add ext] Additional function allows to analyze the current target (TryToFindTargets option)
				if (reFindNewTargets > 1) goto ReFindNewTargets;
				if (reFindNewTargets && AICheckCurrentAttackerTarget(source, sourceTarget)) {
					if (!(type & 8)) type |= 4;
					goto ReFindNewTargets;
				}
				return sourceTarget;
			}
		}
	} else {
		// [add ext] когда нет собсвенной цели у source, находим первый враждебный криттер
		targets[0] = AIHelpersExt::GetNearestEnemyCritter(source);
	}

FindNewTargets:
	fo::func::ai_find_attackers(source, &targets[1], &targets[2], &targets[3]);

	if (isIgnoreFlee) { // remove from the list of targets if the Flee flag is set for the target
		for (size_t i = 0; i < 4; i++) {
			if (targets[i] && targets[i]->critter.IsFleeing()) targets[i] = nullptr;
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

	for (size_t i = 0; i < 4; i++)
		DEV_PRINTF2("\n[AI] DangerSource: Find possible target: %s, ID:%d", (targets[i]) ? fo::func::critter_name(targets[i]) : "<None>", (targets[i]) ? targets[i]->id : 0);

	sf::FindTargetHook_Invoke(targets, source); // [HOOK_FINDTARGET] // TODO: Нужно добавлять тип крючка для вызовов когда происходит проверки для присоединею/отсоединение к бою

	// [add ext] Переключаем режим с 1 к 2
	if (type & 1) type ^= 3; // unset 1 and set type 2

	// select the first available target
	for (size_t i = 0; i < 4; i++)
	{
		if (targets[i]) {
			bool noPerception = (game::Objects::is_within_perception(source, targets[i], 3) == 0); // [HOOK_WITHINPERCEPTION]
			if (noPerception ||	(fo::func::make_path_func(source, source->tile, targets[i]->tile, 0, 0, (void*)fo::funcoffs::obj_blocking_at_) == 0) ||
				(AICombatCheckBadShot(source, targets[i], type))) // [add ext]
			{
				const char* name = fo::func::critter_name(targets[i]);
				if (noPerception) {
					fo::func::debug_printf("\n[AI] ai_danger_source: I don't see (%s) target! Picking alternate!", name);
					targets[i] = nullptr;
				} else {
					fo::func::debug_printf("\n[AI] ai_danger_source: I couldn't get (%s) target!", name);
				}
				continue;
			}
			return targets[i];
		}
	}
	// Ни одна цель не была выбрана: Взять цель до которой можно дойти [add ext]
	for (size_t i = 0; i < 4; i++) {
		if (targets[i] && fo::func::make_path_func(source, source->tile, targets[i]->tile, 0, 0, game::Tilemap::obj_path_blocking_at_)) {
			fo::func::debug_printf("\n[AI] ai_danger_source: I get block target (%s).", fo::func::critter_name(targets[i]));
			return targets[i];
		}
	}
	return nullptr;
}

fo::GameObject* __fastcall AISearchTarget::AIDangerSource_Extended(fo::GameObject* source, long type) {
	if (!source) return nullptr;

	fo::GameObject* weapon = nullptr;
	fo::GameObject* handItem = fo::func::inven_right_hand(source);
	if (!handItem) {
		// если атакующий не вооружен, найти оружие в инвентаре перед поиском цели (исправляет ситуацию при поиске целей)
		weapon = AIInventory::GetInventoryWeapon(source, true, false); // выбрать лучшее оружие из инвентаря (если имеется)
		if (weapon) {
			DEV_PRINTF("\n[AI] Attacker wield weapon.");
			weapon->flags |= fo::ObjectFlag::Right_Hand;
		}
	}

	fo::GameObject* target = AIDangerSource(source, type);

	if (weapon) weapon->flags ^= fo::ObjectFlag::Right_Hand;

	return target;
}

static void __declspec(naked) ai_danger_source_replacement() {
	__asm { // push edx;
		push ecx;
		xor  edx, edx; // type
		cmp  dword ptr [esp + 8], 0x42B235 + 5; // called combat_ai_
		sete dl;       // set type 1 if called from combat_ai_
		mov  ecx, eax; // source
		call AISearchTarget::AIDangerSource_Extended;
		pop  ecx;
		pop  edx;
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

void AISearchTarget::init(bool smartBehavior) {
	// Enables the ability to use the AttackWho value from the AI-packet for the NPC
	npcAttackWhoFix = (sf::IniReader::GetConfigInt("CombatAI", "NPCAttackWhoFix", 0) > 0);

	if (smartBehavior) {
		sf::MakeJump(fo::funcoffs::ai_danger_source_ + 1, ai_danger_source_replacement); // 0x428F4C
		sf::SafeWrite8(0x428F4C, 0x52); // push edx

		if (sf::IniReader::GetConfigInt("CombatAI", "TryToFindTargets", 1) > 0) {
			reFindNewTargets = 1;
		} else if (sf::IniReader::GetConfigInt("CombatAI", "ReFindTargets", 0)) {
			reFindNewTargets = 2; // w/o logic
		}
	} else {
		/// Changes the behavior of the AI so that the AI moves to its target to perform an attack/shot when the range of its weapon is less than
		/// the distance to the target or the AI will choose the nearest target if any other targets are available

		sf::HookCall(0x42B240, combat_ai_hook_revert_target); // also need for TryToFindTargets option

		if (npcAttackWhoFix) sf::MakeCall(0x428F70, ai_danger_source_hack, 3);
	}
}

}
}