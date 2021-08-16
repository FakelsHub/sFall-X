/*
 *    sfall
 *    Copyright (C) 2021  The sfall team
 *
 */

//#include <map>

#include "..\..\FalloutEngine\Fallout2.h"

#include "..\..\main.h"
#include "..\..\Utils.h"
//#include "..\LoadGameHook.h"
#include "..\Combat.h"
#include "..\PartyControl.h"

#include "..\..\Game\combatAI.h"
#include "..\..\Game\items.h"

#include "..\AI.h"
#include "AI.Behavior.h"
#include "AI.SearchTarget.h"
#include "AI.Inventory.h"
#include "AI.FuncHelpers.h"

#include "AI.Combat.h"

namespace sfall
{

// Реализация движковой функции combat_check_bad_shot_ для функции ai_try_attack_
// В данной реализаци изменен порядок проверки результатов:
//	"NoAmmo" проверяется перед "NotEnoughAPs" и "OutOfRange"
//	проверка "OutOfRange" помещена перед "NotEnoughAPs"
//	дополнительно добавлен результат "NoActionPoint", когда когда у атакующего нет очков действий
CombatShootResult AICombat::combat_check_bad_shot(fo::GameObject* source, fo::GameObject* target, fo::AttackType hitMode, long isCalled) {
	if (source->critter.getAP() <= 0) return CombatShootResult::NoActionPoint;
	if (target && target->critter.damageFlags & fo::DAM_DEAD) return CombatShootResult::TargetDead; // target is dead

	fo::GameObject* item = fo::func::inven_right_hand(source);
	if (item) {
		long flags = source->critter.damageFlags;
		if (flags & (fo::DAM_CRIP_ARM_RIGHT | fo::DAM_CRIP_ARM_LEFT) && (fo::GetProto(item->protoId)->item.flagsExt & fo::ItemFlags::TwoHand)) { // item_w_is_2handed_
			return CombatShootResult::CrippledHand; // one of the hands is crippled, can't use a two-handed weapon
		}
		if ((flags & fo::DAM_CRIP_ARM_LEFT) && (flags & fo::DAM_CRIP_ARM_RIGHT)) {
			return CombatShootResult::CrippledHands; // crippled both hands
		}
		if (fo::func::item_w_max_ammo(item) > 0 && Combat::check_item_ammo_cost(item, hitMode) <= 0) return CombatShootResult::NoAmmo;
	}
	long distance = (target) ? fo::func::obj_dist(source, target) : 1;
	long attackRange = fo::func::item_w_range(source, hitMode);
	if (distance > attackRange) return CombatShootResult::OutOfRange; // target is out of range of the attack

	if (game::Items::item_w_mp_cost(source, hitMode, isCalled) > source->critter.getAP()) return CombatShootResult::NotEnoughAPs;

	if (target && attackRange > 1 && fo::func::combat_is_shot_blocked(source, source->tile, target->tile, target, 0)) {
		return CombatShootResult::ShootBlock; // shot to target is blocked
	}
	return CombatShootResult::Ok;
}

static CombatShootResult CheckShotBeforeAttack(fo::GameObject* source, fo::GameObject* target, fo::AttackType hitMode) {
	fo::GameObject* item = fo::func::inven_right_hand(source);
	if (item) {
		long flags = source->critter.damageFlags;
		if (flags & (fo::DAM_CRIP_ARM_RIGHT | fo::DAM_CRIP_ARM_LEFT) && (fo::GetProto(item->protoId)->item.flagsExt & fo::ItemFlags::TwoHand)) { // item_w_is_2handed_
			return CombatShootResult::CrippledHand; // one of the hands is crippled, can't use a two-handed weapon
		}
		if ((flags & fo::DAM_CRIP_ARM_LEFT) && (flags & fo::DAM_CRIP_ARM_RIGHT)) {
			return CombatShootResult::CrippledHands; // crippled both hands
		}
		if (fo::func::item_w_max_ammo(item) > 0 && Combat::check_item_ammo_cost(item, hitMode) <= 0) return CombatShootResult::NoAmmo;
	}
	long distance = (target) ? fo::func::obj_dist(source, target) : 1;
	long attackRange = fo::func::item_w_range(source, hitMode);
	if (distance > attackRange) return CombatShootResult::OutOfRange; // target is out of range of the attack

	if (target && attackRange > 1 && fo::func::combat_is_shot_blocked(source, source->tile, target->tile, target, 0)) {
		return CombatShootResult::ShootBlock; // shot to target is blocked
	}
	return CombatShootResult::Ok;
}

/////////////////////////////////////////////////////////////////////////////////////////

// Карта должна хранить номер тайла где криттер последний раз видел его атакующего криттера
// используется в тех случая когда не была найдена цель
//std::unordered_map<fo::GameObject*, long> lastAttackerTile;

CombatDifficulty AICombat::combatDifficulty = CombatDifficulty::Normal;
bool AICombat::npcPercentMinHP = false;
static bool useCombatDifficulty = true;

static struct AttackerData {
	char name[35];
	fo::AIcap* cap;

	long killType;
	long bodyType;
	bool InDudeParty;

	long maxAP; // количество AP у NPC в начале хода (без учета бонусов)
	long totalBonusAP;
	long moveBonusAP;

	fo::AttackType currentHitMode; // текущий режим атаки для напаюдающего

	struct CoverTile {
		long tile;
		long distance;
	} cover;

	void setData(fo::GameObject* _attacker) {
		strncpy_s(name, fo::func::critter_name(_attacker), _TRUNCATE);
		InDudeParty = (fo::func::isPartyMember(_attacker) > 0);
		cap = fo::func::ai_cap(_attacker);
		bodyType = fo::func::critter_body_type(_attacker);
		killType = GetCritterKillType(_attacker);
		maxAP = _attacker->critter.getAP();
		totalBonusAP = 0;
		moveBonusAP = 0;
		currentHitMode = fo::AttackType::ATKTYPE_PUNCH;
		cover.tile = -1;
	}

	bool IsHumanoid() {
		return (bodyType == fo::BodyType::Biped && killType != fo::KillType::KILL_TYPE_gecko);
	}

	void SetMoveBonusAP(fo::GameObject* source) {
		long bonusAP;
		if (IsHumanoid()) {
			switch (AICombat::combatDifficulty) {
				case CombatDifficulty::Easy:
					bonusAP = 2;
					break;
				case CombatDifficulty::Normal:
					bonusAP = 4;
					break;
				case CombatDifficulty::Hard:
					bonusAP = 6;
					break;
			}
			source->critter.movePoints += bonusAP;
			totalBonusAP += bonusAP;
			moveBonusAP = bonusAP;
		}
	}

	void RemoveMoveBonusAP(fo::GameObject* source) {
		if (moveBonusAP == 0) return;
		totalBonusAP -= moveBonusAP;
		if (source->critter.getAP() > 0) {
			if (source->critter.getAP() <= moveBonusAP) {
				source->critter.movePoints = 0;
			} else {
				source->critter.movePoints -= moveBonusAP;
			}
		}
		moveBonusAP = 0;
	}
} attacker;

void AICombat::AttackerSetHitMode(fo::AttackType mode) {
	attacker.currentHitMode = mode;
}

fo::AttackType AICombat::AttackerHitMode() {
	return attacker.currentHitMode;
}

long AICombat::AttackerBonusAP() {
	return attacker.totalBonusAP;
}

long AICombat::AttackerBodyType() {
	return attacker.bodyType;
}

bool AICombat::AttackerIsHumanoid() {
	return attacker.IsHumanoid();
}

fo::AIcap* AICombat::AttackerAI() {
	return attacker.cap;
}

// Расстояния для напарников игрока когда они остаются без найденной цели
static inline long getAIPartyMemberDistances(long aiDistance) {
	static long aiPartyMemberDistances[5] = {
		5,   // stay_close
		10,  // charge
		12,  // snipe
		8,   // on_your_own
		5000 // stay
	};
	return (aiDistance >= 0 && aiDistance < 5) ? aiPartyMemberDistances[aiDistance] : 6;
}

/////////////////////////////////////////////////////////////////////////////////////////

static bool CheckCoverTile(std::vector<long> &v, long tile) {
	return (std::find(v.cbegin(), v.cend(), tile) != v.cend());
}

// Поиск близлежащего гекса объекта за которым NPC может укрыться от стрелкового оружия нападающего
static long GetCoverBehindObjectTile(fo::GameObject* source, fo::GameObject* target, long inRadius, long allowMoveDistance) {
	std::multimap<long, fo::GameObject*> objects;
	std::vector<long> checkTiles;

	const unsigned long mask = 0xFFFF0000 | (fo::OBJ_TYPE_WALL << 8) | fo::OBJ_TYPE_SCENERY;
	GetObjectsTileRadius(objects, source->tile, inRadius, source->elevation, mask); // объекты должны быть отсортированы по дальности расположения

	//long dir = fo::func::tile_dir(target->tile, source->tile); // направление цели к source

	long distToTarget = fo::func::obj_dist(source, target); // расстояние до цели
	long addDist = (distToTarget <= 3) ? 3 : 0;             // если цель расположена близко то сначала будем искать дальнее укрытие

reTryFindCoverTile:

	long tile = -1;
	long objectTile = tile;
	for (const auto &obj_pair : objects)
	{
		fo::GameObject* obj = obj_pair.second;
		if (obj->tile == objectTile) continue;
		objectTile = obj->tile; // запоминаем проверяемый гекс на котором расположен объект, для того чтобы не проверять другие объекты на гексе

		//DEV_PRINTF2("\nCover object: %s tile:%d", fo::func::critter_name(obj), objectTile);

		// получить гекс за объектом, направление расположения цели к объекту
		long dirCentre = fo::func::tile_dir(target->tile, obj->tile);

		// смежные направления гексов
		long r = (GetRandom(1, 2) == 2) ? 5 : 1;
		long dirNear0 = (dirCentre + r) % 6;
		r = (r == 1) ? 5 : 1;
		long dirNear1 = (dirCentre + r) % 6;

		// берем первый не заблокированный гекс
		for (size_t i = 1; i < 3; i++) // максимальный радиус 2 в гекса
		{
			long _tile = fo::func::tile_num_in_direction(obj->tile, dirCentre, i);
			if (fo::func::obj_blocking_at(nullptr, _tile, obj->elevation))
			{
				_tile = fo::func::tile_num_in_direction(obj->tile, dirNear0, i);
				if (fo::func::obj_blocking_at(nullptr, _tile, obj->elevation))
				{
					_tile = fo::func::tile_num_in_direction(obj->tile, dirNear1, i);
					if (fo::func::obj_blocking_at(nullptr, _tile, obj->elevation))
					{
						continue; // все гексы заблокированы
					}
				}
			}
			if (CheckCoverTile(checkTiles, _tile)) continue;

			// оптимальное ли расстояние до укрываемого тайла?
			long distSource = fo::func::tile_dist(source->tile, _tile);
			long distTarget = fo::func::tile_dist(target->tile, _tile);
			if (distSource + addDist >= distTarget) {
				//DEV_PRINTF3("\nCover no optimal distance: %d | s:%d >= t:%d", _tile, distSource + addDist, distTarget);
				if (!addDist) checkTiles.push_back(_tile);
				continue; // не оптимальное
			}

			// проверить не простреливается ли данный гекс (здесь видимо нужно дополнительно проверять всех враждебных криттеров)
			if (fo::func::combat_is_shot_blocked(target, target->tile, _tile, 0, 0) == false) {
				//DEV_PRINTF1("\nCover tile is shooting:%d", _tile);
				checkTiles.push_back(_tile);
				continue; // простреливается
			}

			// проверить не заблокирован ли путь к гексу
			long pathLength = fo::func::make_path_func(source, source->tile, _tile, 0, 0, (void*)fo::funcoffs::obj_blocking_at_);
			if (pathLength > 0) {
				// хватает ли очков действия чтобы переместиться к гексу
				if (allowMoveDistance >= pathLength) {
					tile = _tile;
					break; // хватает, выходим из цикла и возвращаем гекс
				}
				else if (attacker.cover.tile == -1) { // запоминаем самый ближе-расположенный гекс для отхода в укрытие (для поведения отступления)
					attacker.cover.tile = _tile;
					attacker.cover.distance = pathLength;
					DEV_PRINTF2("\nCover: %s set moveback tile: %d\n", fo::func::critter_name(obj), _tile);
				}
			} else {
				//DEV_PRINTF2("\nCover: %s I can't move to tile: %d\n", fo::func::critter_name(obj), _tile);
			}
			checkTiles.push_back(_tile);
		}
		if (tile > -1) {
			if (isDebug) fo::func::debug_printf("\n[AI] Cover: %s move to tile %d\n", fo::func::critter_name(obj), tile);
			attacker.cover.tile = -1;
			return tile;
		}
	}
	if (addDist) {
		addDist = 0;
		goto reTryFindCoverTile; // повторяем
	}
	return -1;
}

// Проверяет условия при которых будет доступно укрытие для NPC
static long CheckCoverConditionAndGetTile(fo::GameObject* source, fo::GameObject* target) {
	if (target->critter.IsNotActive()) return -1;

	// выход если NPC безоружен или у атакующего не стрелковое оружие
	fo::GameObject* item = fo::func::inven_right_hand(source);
	if (!item || AIHelpers::GetWeaponSubType(item, false) != fo::AttackSubType::GUNS) {
		return -1;
	}

	// если цель вооружена и простреливается то переместиться за укрытие
	bool isRangeAttack = false;
	if (target == fo::var::obj_dude) {
		fo::GameObject* lItem = fo::func::inven_left_hand(target);
		if (lItem && AIHelpers::IsGunOrThrowingWeapon(lItem)) isRangeAttack = true;
	}
	if (!isRangeAttack) {
		fo::GameObject* rItem = fo::func::inven_right_hand(target);
		if (!rItem || !AIHelpers::IsGunOrThrowingWeapon(rItem)) return -1;
		isRangeAttack = true;
	}
	if (isRangeAttack && fo::func::combat_is_shot_blocked(target, target->tile, source->tile, source, 0)) return -1; // может ли цель стрелять по цели

	return GetCoverBehindObjectTile(source, target, source->critter.getAP() + 1, source->critter.getMoveAP());
}

/////////////////////////////////////////////////////////////////////////////////////////

// Получает цель и дистанцию до нее
static unsigned long GetTargetDistance(fo::GameObject* source, fo::GameObject* &target) {
	unsigned long distanceLast = -1; // inactive

	fo::GameObject* lastAttacker = AI::AIGetLastAttacker(source);
	if (lastAttacker && lastAttacker != target && lastAttacker->critter.IsActiveNotDead()) {
		distanceLast = fo::func::obj_dist(source, lastAttacker);
	}
	// target is active?
	unsigned long distance = (target && target->critter.IsActiveNotDead())
							 ? fo::func::obj_dist(source, target)
							 : -1; // inactive

	if (distance >= 3 && distanceLast >= 3) return -1;   // also distances == -1
	if (distance >= distanceLast) target = lastAttacker; // replace target (attacker critter has priority)

	return (distance >= distanceLast) ? distanceLast : distance;
}

// Функция анализирует используемое оружие у цели, и если цель использует оружие ближнего действия то атакующий NPC
// по завершению хода попытается отойти от атакующей его цели на небольшое расстояние
// Executed after the NPC attack
static long GetMoveAwayDistaceFromTarget(fo::GameObject* source, fo::GameObject* &target) {

	if (attacker.killType > fo::KillType::KILL_TYPE_women ||    // critter is not men & women
		attacker.cap->disposition == fo::AIpref::disposition::berserk ||
		///cap->distance == AIpref::distance::stay || // stay в ai_move_away запрещает движение
		attacker.cap->distance == fo::AIpref::distance::charge) // charge в используется для сближения с целью
	{
		return 0;
	}

	unsigned long distance = source->critter.getAP(); // для coward: отойдет на максимально возможную дистанцию

	if (attacker.cap->disposition != fo::AIpref::disposition::coward) {
		if (distance >= 3) return 0; // source still has a lot of action points

		distance = GetTargetDistance(source, target);
		if (distance > 2) return 0; // цели далеко, или неактивны

		fo::GameObject* sWeapon = fo::func::inven_right_hand(source);
		long wTypeR = fo::func::item_w_subtype(sWeapon, fo::func::ai_pick_hit_mode(source, sWeapon, target)); // возможно тут надо использовать последний HitMode
		if (wTypeR <= fo::AttackSubType::MELEE) return 0; // source has a melee weapon or unarmed

		fo::Proto* protoR = nullptr;
		fo::Proto* protoL = nullptr;
		fo::AttackSubType wTypeRs = fo::AttackSubType::NONE;
		fo::AttackSubType wTypeL  = fo::AttackSubType::NONE;
		fo::AttackSubType wTypeLs = fo::AttackSubType::NONE;

		fo::GameObject* itemHandR = fo::func::inven_right_hand(target);
		if (!itemHandR && target != fo::var::obj_dude) { // target is unarmed
			long damage = fo::func::stat_level(target, fo::Stat::STAT_melee_dmg);
			if (damage * 2 < source->critter.health / 2) return 0;
			goto moveAway;
		}
		if (itemHandR) {
			fo::GetProto(itemHandR->protoId, &protoR);
			long weaponFlags = protoR->item.flagsExt;

			wTypeR = fo::GetWeaponType(weaponFlags);
			if (wTypeR == fo::AttackSubType::GUNS) return 0; // the attacker **not move away** if the target has a firearm
			wTypeRs = fo::GetWeaponType(weaponFlags >> 4);
		}
		if (target == fo::var::obj_dude) {
			fo::GameObject* itemHandL = fo::func::inven_left_hand(target);
			if (itemHandL) {
				fo::GetProto(itemHandL->protoId, &protoL);
				wTypeL = fo::GetWeaponType(protoL->item.flagsExt);
				if (wTypeL == fo::AttackSubType::GUNS) return 0; // the attacker **not move away** if the target(dude) has a firearm
				wTypeLs = fo::GetWeaponType(protoL->item.flagsExt >> 4);
			} else if (!itemHandR) {
				// dude is unarmed
				long damage = fo::func::stat_level(target, fo::Stat::STAT_melee_dmg);
				if (damage * 4 < source->critter.health / 2) return 0;
			}
		}
moveAway:
		// if attacker is aggressive then **not move away** from any throwing weapons (include grenades)
		if (attacker.cap->disposition == fo::AIpref::aggressive) {
			if (wTypeRs == fo::AttackSubType::THROWING || wTypeLs == fo::AttackSubType::THROWING) return 0;
			if (wTypeR  == fo::AttackSubType::THROWING || wTypeL  == fo::AttackSubType::THROWING) return 0;
		} else {
			// the attacker **not move away** if the target has a throwing weapon and it is a grenade
			if (protoR && wTypeR == fo::AttackSubType::THROWING && protoR->item.weapon.damageType != fo::DamageType::DMG_normal) return 0;
			if (protoL && wTypeL == fo::AttackSubType::THROWING && protoL->item.weapon.damageType != fo::DamageType::DMG_normal) return 0;
		}
		distance = 3 - distance; // всегда держаться на дистанции в 2 гекса от цели (не учитывать AP)
	}
	else if (GetTargetDistance(source, target) > 5) return 0; // цели неактивны

	return distance;
}

static void MoveAwayFromTarget(fo::GameObject* source, fo::GameObject* target, long distance) {
	#ifndef NDEBUG
	fo::func::debug_printf("\n[AI] %s: Away from: %s, Dist:%d, AP:%d, HP:%d", attacker.name, fo::func::critter_name(target), distance, source->critter.getAP(), source->critter.health);
	#endif
	// функция принимает отрицательный аргумент дистанции для того чтобы держаться на определенной дистанции от указанной цели при этом поведение stay будет игнорироваться
	fo::func::ai_move_away(source, target, distance);
}

/////////////////////////////////////////////////////////////////////////////////////////

// Проверяет наличие дружественных NPC на линии перед атакой для отхода в сторону на 1-3 гекса
// Имеет в себе функцию cai_retargetTileFromFriendlyFire для ретаргетинга гекса
static void ReTargetTileFromFriendlyFire(fo::GameObject* source, fo::GameObject* target) {
	long reTargetTile = source->tile;

	DEV_PRINTF("\n[AI] ReTarget tile before attack.");

	if (fo::func::item_w_range(source, fo::AttackType::ATKTYPE_RWEAPON_PRIMARY) >= 2) {

		long cost = AIHelpers::GetCurrenShootAPCost(source, target);
		if (cost > (source->critter.getAP() - 1)) return; // предварительная проверка

		fo::GameObject* friendNPC = AI::CheckFriendlyFire(target, source);
		if (friendNPC) {
			long distToTarget = fo::func::obj_dist(friendNPC, target);
			if (distToTarget >= 2 || fo::func::obj_dist(friendNPC, source) <= 2) {

				long shotDir = fo::func::tile_dir(source->tile, target->tile);
				long range = 1;
				char check = 0;

				long r = GetRandom(1, 2);
				if (r > 1) r = 5;

				while (true)
				{
					long dir = (shotDir + r) % 6;
					long tile = fo::func::tile_num_in_direction(source->tile, dir, range);
					long pathLen = fo::func::make_path_func(source, source->tile, tile, 0, 1, (void*)fo::funcoffs::obj_blocking_at_);
					if (pathLen > 0 && pathLen < (source->critter.getMoveAP() - cost)) {
						if (!AIHelpers::CheckFriendlyFire(target, source, tile)) {
							reTargetTile = tile;
							DEV_PRINTF("\n[AI] -> pick tile to retarget.");
							break;
						}
					}
					if (++check == 2) {
						if (++range > 3) break; // max range distance 3
						if (cost > (source->critter.getMoveAP() - range)) break;
						check = 0;
					}
					// меняем направление
					r = (r == 5) ? 1 : 5;
				}
			}
		}
	}
	// cai_retargetTileFromFriendlyFire здесь не имеет приоритет т.к. проверяет линию огня для других атакующих NPC
	if (reTargetTile == source->tile) {
		if (fo::func::cai_retargetTileFromFriendlyFire(source, target, &reTargetTile) == -1) return;
		if (reTargetTile != source->tile) DEV_PRINTF("\n[AI] -> cai_retargetTileFromFriendlyFire");
	}
	if (reTargetTile != source->tile) {
		DEV_PRINTF("\n[AI] -> move tile before attack");
		AIHelpers::CombatMoveToTile(source, reTargetTile, source->critter.getAP());
	}
}

// Реализация движковой функции cai_perform_distance_prefs с измененным и дополнительным функционалом
static void DistancePrefBeforeAttack(fo::GameObject* source, fo::GameObject* target) {
	long distance = 0;

	/* Distance: Charge behavior */
	if (attacker.cap->distance == fo::AIpref::distance::charge && fo::func::obj_dist(source, target) > fo::func::item_w_range(source, fo::AttackType::ATKTYPE_RWEAPON_PRIMARY) / 2) {
		DEV_PRINTF1("\n[AI] AIpref::distance::charge: %s", fo::func::critter_name(target));
		// приблизиться на расстояние для совершения одной атаки
		distance = source->critter.getAP();
		long cost = AIHelpers::GetCurrenShootAPCost(source, fo::AttackType::ATKTYPE_RWEAPON_PRIMARY, 0);
		if (cost != -1 && distance > cost) distance -= cost;
		if (source->critter.getAP(distance) < cost) return;
		fo::func::ai_move_steps_closer(source, target, distance, 1);
	}
	/* Distance: Snipe behavior */
	else if (attacker.cap->distance == fo::AIpref::distance::snipe && (distance = fo::func::obj_dist(source, target)) < 10) {
		DEV_PRINTF1("\n[AI] AIpref::distance::snipe: %s", fo::func::critter_name(target));
		if (AI::AIGetLastTarget(target) == source) { // target атакует source target->critter.getHitTarget()
			// атакующий отойдет на расстояние в 10 гексов от своей цели если она на него нападает
			bool shouldMove = ((fo::func::combatai_rating(source) + 10) < fo::func::combatai_rating(target));
			if (shouldMove) {
				long costAP = AIHelpers::GetCurrenShootAPCost(source, fo::AttackType::ATKTYPE_RWEAPON_PRIMARY, 0);
				if (costAP != -1) {
					long shotCount = source->critter.getAP() / costAP;
					long freeAPs = source->critter.getAP() - (costAP * shotCount); // положительное число если останутся AP после атаки
					if (freeAPs > 0) {
						long dist = distance - 1;
						if (freeAPs + dist >= 5) shouldMove = false;
					}
				}
			}
			if (shouldMove) fo::func::ai_move_away(source, target, 10);
		}
	}
	/* Distance: Stay Close behavior */
	// Данное поведение здесь удалено, оно используется только после атаки в функции cai_perform_distance_prefs_
}

static void ReTargetTileFromFriendlyFire(fo::GameObject* source, fo::GameObject* target, bool сheckAP) {
	if (сheckAP) {
		long cost = AIHelpers::GetCurrenShootAPCost(source, target);
		if (cost == -1 || cost >= source->critter.getAP()) return;
	}
	ReTargetTileFromFriendlyFire(source, target);
}

static bool CheckEnemyCritters(fo::GameObject* source) {
	for (size_t i = 0; i < fo::var::list_com; i++)
	{
		fo::GameObject* obj = fo::var::combat_list[i];

		// Проверки на всякий случай
		if (obj->critter.IsDead() || obj->critter.combatState & fo::CombatStateFlag::EnemyOutOfRange) continue;
		if (obj->critter.combatState & fo::CombatStateFlag::InFlee && !(obj->critter.combatState & fo::CombatStateFlag::ReTarget)) continue;

		fo::GameObject* target = obj->critter.getHitTarget();
		if (target && target->critter.teamNum == source->critter.teamNum) {
			return true;
		}
	}
	return false;
}

/////////////////////////////////////////////////////////////////////////////////////////

// Реализация движковой функции combat_ai_
static void CombatAI_Extended(fo::GameObject* source, fo::GameObject* target) {
	fo::GameObject* lastTarget = source->critter.getHitTarget();;
	long lastCombatAP = 0;

	/**************************************************************************
		Рассчет минимального значения HP при котором NPC начет убегать, когда
		включена опция NPCRunAwayMode и атакующий не является постоянным партийцем игрока
		(для постоянных партийцев min_hp рассчитывается при выборе предпочтений в панели управления)
	**************************************************************************/
	if (AICombat::npcPercentMinHP && attacker.cap->getRunAwayMode() != fo::AIpref::run_away_mode::none && !fo::IsPartyMember(source))
	{
		long caiMinHp = fo::func::cai_get_min_hp(attacker.cap);
		long maxHP = fo::func::stat_level(source, fo::STAT_max_hit_points);
		long minHpPercent = maxHP * caiMinHp / 100;
		long fleeMinHP = maxHP - minHpPercent;
		attacker.cap->min_hp = fleeMinHP; // должно устанавливаться перед выполнением ai_check_drugs
		fo::func::debug_printf("\n[AI] Calculated flee minHP for NPC");
	}
	fo::func::debug_printf("\n[AI] %s: Flee MinHP: %d, CurHP: %d, AP: %d", attacker.name, attacker.cap->min_hp, fo::func::stat_level(source, fo::STAT_current_hp), source->critter.getAP());

	/**************************************************************************
		Выполняем алгорим побега с поля боя	если атакующий имеет установленны флаг 'Flee'
		или если атакующий имеет повреждение каких-либо частей тела
	**************************************************************************/
	if ((source->critter.IsFleeing()) || (source->critter.damageFlags & attacker.cap->hurt_too_much)) {
		// fix for flee from sfall
		if (!(source->critter.combatState & fo::CombatStateFlag::ReTarget))
		{
			fo::func::debug_printf("\n[AI] %s FLEEING: I'm Hurt!", attacker.name);

			if (!target) {
				target = AIHelpers::GetNearestEnemyCritter(source); // получить самого ближнего криттера не из своей команды
			}
			if (fo::func::obj_dist(source, fo::var::obj_dude) <= attacker.cap->max_dist * 2) {
				fo::func::ai_run_away(source, target);              // убегаем от цели или от игрока если цель не была назначена
			}
			return;
		}

		{	// fix for flee from sfall
			source->critter.combatState ^= (fo::CombatStateFlag::ReTarget | fo::CombatStateFlag::InFlee);
			source->critter.whoHitMe = target = 0;
		}
	}

	bool trySpendExtraAP = true;
TrySpendExtraAP:
	/**************************************************************************
		Фаза лечения если атакующий ранен или использования каких-либо наркотических средств перед атакой
	**************************************************************************/
	if (fo::var::combatNumTurns == 0 || CheckEnemyCritters(source)) { // Проверяет имеются ли враждебные криттеры перед употреблением (NPC может в конце боя принять)
		DEV_PRINTF("\n[AI] Check drugs...");
		game::CombatAI::ai_check_drugs(source); //fo::func::ai_check_drugs(source);
	}

	// текущие очки жизней меньше чем значение min_hp
	// определяет поведение, когда нет медикаментов для лечения
	if (fo::func::stat_level(source, fo::STAT_current_hp) < attacker.cap->min_hp) {
		fo::func::debug_printf("\n[AI] %s FLEEING: I need DRUGS!", attacker.name);

		if (attacker.InDudeParty) { // партийцы бегут к игроку
			fo::func::ai_move_steps_closer(source, fo::var::obj_dude, source->critter.getAP(), 0); // !!! здесь диспозиция stay не позволяет бежать к игроку !!!
		} else {
			if (!lastTarget) lastTarget = AIHelpers::GetNearestEnemyCritter(source); // получить самого ближнего криттера не из своей команды
			if (lastTarget && fo::func::obj_dist(source, lastTarget) <= 1) fo::func::ai_try_attack(source, lastTarget);

			fo::func::ai_run_away(source, lastTarget);                               // убегаем от потенцеально опасного криттера (или от игрока)
			source->critter.combatState &= ~(fo::CombatStateFlag::EnemyOutOfRange | fo::CombatStateFlag::InFlee); // снять флаги после ai_run_away
		}
		return;
	}
	if (source->critter.getAP() <= 0) return; // выход - закончились очки действия

	/**************************************************************************
		Фаза поиска цели для атакующего если она не была задана для атаки
	**************************************************************************/
	bool findTargetAfterKill = false;
	fo::GameObject* findTarget;

	if (!target) {
		PartyControl::SetOrderTarget(source); // Party order attack feature

ReFindNewTarget:
		DEV_PRINTF("\n[AI] Find targets...");
		target = findTarget = AISearchTarget::AIDangerSource_Extended(source, 1);

		if (!target) DEV_PRINTF("\n[AI] No find target!"); else DEV_PRINTF1("\n[AI] Pick target: %s", fo::func::critter_name(target));

		if (lastCombatAP && lastTarget == target) {
			DEV_PRINTF("\n[AI] TrySpendAP: No find new target! (Skip Attack)");
			//goto skipAttack; // нужны дополнительные проверки перед тем как пропускать фазу атаки
		}
		target = AISearchTarget::RevertTarget(source, target);
	} else {
		DEV_PRINTF2("\n[AI] Attacker has target: %s ID: %d", fo::func::critter_name(target), target->id);
	}

	/**************************************************************************
		Фаза атаки если цель для атакующего была найдена
	**************************************************************************/
	if (target && target->critter.IsNotDead()) {
		// Перестроится перед атакой при проверки дружественного огня или от установленных предпочтений дистанции
		if (!CheckShotBeforeAttack(source, target, fo::AttackType::ATKTYPE_RWEAPON_PRIMARY)) {
			DistancePrefBeforeAttack(source, target); // используется вместо функции cai_perform_distance_prefs
			ReTargetTileFromFriendlyFire(source, target, findTargetAfterKill);
		}

		//long apBeforeAttack = source->critter.getAP();

		DEV_PRINTF("\n[AI] Try attack.");
		switch ((AIBehavior::AttackResult)fo::func::ai_try_attack(source, target)) // при TrySpendExtraAP атакующие не должны бежать к новой цели
		{
		case AIBehavior::AttackResult::TargetDead:
			DEV_PRINTF("\n[AI] Attack result: TARGET DEAD!\n");
			if (target == fo::var::obj_dude) return;
			findTargetAfterKill = true;
			goto ReFindNewTarget; // поиск новой цели, текущая была убита

		case AIBehavior::AttackResult::BadToHit:
			break;

		case AIBehavior::AttackResult::NoMovePoints:
			break;
		}
		DEV_PRINTF("\n[AI] End attack.");

		//trySpendExtraAP = (apBeforeAttack != source->critter.getAP()); // не были потрачены AP в фазе атаки

		if (source->critter.IsDead()) return; // атакующий мертв? (после ai_try_attack)

		findTargetAfterKill = false;
	} else {
		DEV_PRINTF1("\n[AI] Attack skip: %s no have target or dead.", attacker.name);
		if (target && target->critter.IsDead()) {
			target = nullptr;
		}
	}
//skipAttack:

	/**************************************************************************
		Фаза действий после выполненной атаки
	**************************************************************************/
	DEV_PRINTF1("\n[AI] Have %d move points.", source->critter.getAP());

	/**************************************************************************
		Поведение: Отход от цели [Доступно только для типа Human]
		Атакующий отходит на незначительное расстояние если цель атакует source ближними ударами
	**************************************************************************/
	long moveAwayDistance = 0;
	fo::GameObject* moveAwayTarget = target; // moveAwayTarget - может измениться на последнего атакующего криттера, если текущая цель оказалась неактивна
	if (source->critter.getAP() > 0) {
		moveAwayDistance = GetMoveAwayDistaceFromTarget(source, moveAwayTarget);

		DEV_PRINTF1("\n[AI] Try move away distance: %d", moveAwayDistance);
		if (target != moveAwayTarget) {
			DEV_PRINTF("\n[AI] (target in not moveAwayTarget)");
		}
		// выполняется только в том случае если изначально цель не была найдена
		// т.е. отход происходит от другого NPC который атаковал source
		if (!target && moveAwayTarget && moveAwayDistance) MoveAwayFromTarget(source, moveAwayTarget, moveAwayDistance);
	}

	/***************************************************************************************
		Поведение: Враг вне зоны. [для всех]
		Атакующий все еще имеет очки действия, его цель(обидчик) не мертва
		и дистанция до цели превышает дистанцию заданную параметром max_dist в AI.txt
		* Данное поведение было определено в ванильной функции
	***************************************************************************************/
	if (source->critter.getAP() > 0 && target && target->critter.IsNotDead() && fo::func::obj_dist(source, target) > attacker.cap->max_dist * 2) { // дистанция max_dist была удвоенна
		// проверить дистанцию стрелкового оружия

		DEV_PRINTF("\n[AI] My target is over max distance!");
		long peRange = fo::func::stat_level(source, fo::STAT_pe) * 5; // x5 множитель по умолчанию в is_within_perception (было x2)

		// найти ближайшего сокомандника и направиться к нему
		if (!fo::func::ai_find_friend(source, peRange, 5) && !attacker.InDudeParty) {
			// ближайший сокомандник не был найден в радиусе зрения атакующего
			// для не партийцев игрока
			fo::GameObject* dead_critter = fo::func::combatAIInfoGetFriendlyDead(source); // получить труп криттера которого кто-то недавно убил
			if (dead_critter) {
				fo::func::ai_move_away(source, dead_critter, source->critter.getAP()); // отойти от убитого криттера
				fo::func::combatAIInfoSetFriendlyDead(source, nullptr);                // очистка
			} else {
				// определить поведение когда не было убитых криттеров
				if (attacker.cap->getDistance() != fo::AIpref::distance::stay) {

					/***** пока нет ни каких действий *****/
				}
				source->critter.combatState |= fo::CombatStateFlag::EnemyOutOfRange; // установить флаг, для перемещения NPC из боевого списка в не боевой
			}
			DEV_PRINTF("\n[AI] My target is over max distance. I can't find my friends!");
			return;
		}
		// напарники игрока продолжат выполнять весь алгоритм ниже
		///return;
	}

	/************************************************************************************
		Поведение: Для случаев когда цель для атакующего не была найдена.
		1. Если атакующий принадлежит к партийцам игрока, то в случае если цель не была найдена в функции ai_danger_source
		   партиец должен направиться к игроку, если он находится на расстоянии превышающее установленную дистанцию в aiPartyMemberDistances.
		2. Если атакующий не принадлежит к партийцам игрока, то он должен найти своего ближайшего со-командника
		   у которого есть цель и двигаться к нему.
	************************************************************************************/
	if (!target) {
		if (!CheckEnemyCritters(source)) {
			DEV_PRINTF1("\n[AI] %s: No enemy critters...", attacker.name);
			return;
		}
		DEV_PRINTF1("\n[AI] %s: I no have target! Try find ally critters.", attacker.name);

		// найти ближайшего со-комадника у которого есть цель (проверить цели)
		fo::GameObject* ally_Critter = fo::func::ai_find_nearest_team_in_combat(source, source, 1);

		// если critter не найден и атакующий из команды игрока, то присвоить в качестве critter obj_dude
		if (!ally_Critter && source->critter.teamNum == 0) ally_Critter = fo::var::obj_dude;

		long distance = (attacker.InDudeParty) ? getAIPartyMemberDistances(attacker.cap->distance) : 6; // default (было 5)

		DEV_PRINTF1("\n[AI] Find team critter: %s", (ally_Critter) ? fo::func::critter_name(ally_Critter) : "<None>");
		if (ally_Critter) {
			long dist = fo::func::obj_dist(source, ally_Critter);
			if (dist > distance) // дистанция больше чем определено по умолчанию, идем к криттеру из своей команды который имеет цель
			{
				dist -= distance; // | 7-6=1
				dist = GetRandom(dist, dist + 3);
				fo::func::ai_move_steps_closer(source, ally_Critter, dist, 0);
				DEV_PRINTF1("\n[AI] Move close to: %s", fo::func::critter_name(ally_Critter));
			}
			else if (!attacker.InDudeParty && attacker.cap->getDistance() != fo::AIpref::distance::stay)
			{	// поведение не для партийцев игрока
				// если атакующий уже находится в радиусе, рандомное перемещение вокруг ally_Critter
				long tile = AIHelpers::GetRandomDistTile(source, ally_Critter->tile, 5);
				if (tile != -1) {
					DEV_PRINTF("\n[AI] Random move tile.");
					AIHelpers::CombatMoveToTile(source, tile, source->critter.getAP());
				}
			}
			/* Со следующего хода (если закончились AP) возможно, что атакующий получит цель которую он заметит */
		} else {
			/************************************************************************************
				Поведение: Для атакующих не состоящих в партии игрока.
				У атакующего нет цели (атакующий не видит целей), он получил повреждения в предыдущем ходе
				и кто-то продолжает стрелять по нему, при этом его обидчик не мертв, и его ближайшие со-командники не найдены
			************************************************************************************/

			// У атакующего нет цели, ближайшие сокомадники не были найдены
			if (!attacker.InDudeParty && source->critter.damageLastTurn > 0 &&                           // атакующий получил повреждения в предыдущем ходе
				source->critter.getHitTarget() && source->critter.getHitTarget()->critter.IsNotDead()) { // и тот кто его атаковал не мертв
				// попытаться спрятаться за ближайшее укрытие (только для типа Biped)

				// если рядом нет укрытий
				// были ли убитые криттеры до вступления в бой
				fo::GameObject* dead_critter = fo::func::combatAIInfoGetFriendlyDead(source); // получить труп криттера которого кто-то убил
				if (dead_critter) {
					long result = fo::func::ai_move_away(source, dead_critter, source->critter.getAP()); // отойти от убитого криттера
					if (result == -1 || fo::func::obj_dist(source, dead_critter) >= 10) fo::func::combatAIInfoSetFriendlyDead(source, nullptr); // очистка
				} else {
					fo::func::debug_printf("\n[AI] %s: FLEEING: Somebody is shooting at me that I can't see!", attacker.name); // Бегство: кто-то стреляет в меня, но я этого не вижу!
					// рандомное перемещение
					long max = source->critter.getMoveAP();
					long tile = AIHelpers::GetRandomTileToMove(source, max, max);
					if (tile != -1) AIHelpers::CombatRunToTile(source, tile, max);
				}
				///return; // обязательно иначе возникнет конфликт поведения
			}
		}
	}

	/************************************************************************************
		Поведение: Не были израсходованы очки действий.
	************************************************************************************/
	if (source->critter.getAP() == attacker.maxAP && attacker.cap->getDistance() != fo::AIpref::distance::stay) {
		if (!findTarget) { // не было найдено доступных целей
			// найти ближайшего со-комадника у которого есть цель (проверить цели)
			fo::GameObject* ally_Critter = fo::func::ai_find_nearest_team_in_combat(source, source, 1);

			// если critter не найден и атакующий из команды игрока, то присвоить в качестве critter obj_dude
			if (!ally_Critter && source->critter.teamNum == 0) ally_Critter = fo::var::obj_dude;

			DEV_PRINTF1("\n[AI] Find team critter: %s", (ally_Critter) ? fo::func::critter_name(ally_Critter) : "<None>");
			if (ally_Critter) {
				long dist = fo::func::obj_dist(source, ally_Critter);
				fo::func::ai_move_steps_closer(source, ally_Critter, dist, 0);
				DEV_PRINTF1("\n[AI] Move close to: %s", fo::func::critter_name(ally_Critter));
			}
		} else if (attacker.InDudeParty == false) {
			// рандомное перемещение
			long max = source->critter.getMoveAP();
			long tile = AIHelpers::GetRandomTileToMove(source, max / 2, max);
			if (tile != -1) AIHelpers::CombatMoveToTile(source, tile, max);
			DEV_PRINTF("\n[AI] End Random move tile.");
		}
	}

	/************************************************************************************
		Поведение: Когда все еще остались очки действия у атакующего
	************************************************************************************/
	if (source->critter.getAP() > 0) {
		fo::func::debug_printf("\n[AI] %s had extra %d AP's.", attacker.name, source->critter.getAP());

		if (trySpendExtraAP && !findTargetAfterKill && source->critter.getAP() >= game::Items::item_w_mp_cost(source, fo::AttackType::ATKTYPE_PUNCH, 0)) { // !target && !moveAwayTarget
			if (lastCombatAP != source->critter.getAP()) {
				lastCombatAP = source->critter.getAP(); // для того чтобы не было зависания в цикле
				lastTarget = target;
				fo::func::debug_printf("\n[AI] try to spend extra %d AP's.", source->critter.getAP());
				target = nullptr; // нет цели, попробовать найти другую цель (аналог опции TrySpendAPs)
				goto TrySpendExtraAP;
			}
		}
	}

	if (moveAwayTarget) { // у атакующего есть цель
		// добавить AP атакующему только для перемещения, этот бонус можно использовать совместно со сложностью боя (только для типа Biped)
		if (!attacker.InDudeParty) attacker.SetMoveBonusAP(source);

		/*
			Тактическое укрытие: определено для всех типов Biped кроме Gecko
			для charge - недоступно, если цель находится на дистанции больше, чем атакующий может иметь очков действий (атакующий будет приближаться к цели)
			для stay   - недоступно (можно позволить в пределах 3 гексов)
		*/
		long coverTile = (attacker.cap->getDistance() == fo::AIpref::distance::stay || !attacker.IsHumanoid() ||
						 (attacker.cap->getDistance() == fo::AIpref::distance::charge && fo::func::obj_dist(source, moveAwayTarget) > attacker.maxAP))
						? -1 // не использовать укрытие
						: CheckCoverConditionAndGetTile(source, moveAwayTarget);

		// условия для отхода в укрытие
		if (attacker.cover.tile != -1 && source->critter.damageLastTurn > 0) {                      // атакующий получил повреждения в прошлом ходе
			if (/*(attacker.cover.distance / 2) <= source->critter.getAP() && */                    // дистанция до плитки укрытия меньше, чем очков действия
				(fo::func::stat_level(source, fo::STAT_current_hp) < (attacker.cap->min_hp * 2) ||  // текущее HP меньше, чем определено в min_hp x 2
				fo::func::combatai_rating(source) * 2 < fo::func::combatai_rating(moveAwayTarget))) // боевой рейтинг цели, больше чем рейтинг атакующего
			{
				DEV_PRINTF("\n[AI] Move back to cover tile.");
				coverTile = attacker.cover.tile;
			}
		}
		// отход в укрытие имеет приоритет над другими функциями перестраивания
		if (coverTile != -1) {
			if (AIHelpers::CombatMoveToTile(source, coverTile, source->critter.getAP()) != 0) coverTile = -1;
			DEV_PRINTF1("\n[AI] AP's after move to cover tile: %d", source->critter.getAP());
		}
		attacker.RemoveMoveBonusAP(source);  // удалить полученные бонусные очки передвижения

		if (coverTile == -1) {
			if (moveAwayDistance) MoveAwayFromTarget(source, moveAwayTarget, moveAwayDistance);
			DEV_PRINTF1("\n[AI] AP's before distance prefs: %d.", source->critter.getAP());
			fo::func::cai_perform_distance_prefs(source, moveAwayTarget);
		}
	}

	if (source->critter.getAP() > 0) {
		// перезарядить оружие
		if (AIInventory::AITryReloadWeapon(source, fo::func::inven_right_hand(source), nullptr)) DEV_PRINTF("\n[AI] Reload weapon.");
	}
	fo::func::debug_printf("\n[AI] left extra %d AP's", source->critter.getAP());
}

static bool combatDebug;

static void __fastcall combat_ai_hook(fo::GameObject* source, fo::GameObject* target) {
	//if (!source) return;

	AICombat::combatDifficulty = (CombatDifficulty)IniReader::GetInt("preferences", "combat_difficulty", (int)AICombat::combatDifficulty, (const char*)FO_VAR_game_config);
	attacker.setData(source);

	// добавить очки действия атакующему для увеличении сложности боя [не для партийцев игрока]
	if (useCombatDifficulty && AICombat::combatDifficulty != CombatDifficulty::Easy  && !attacker.InDudeParty) {
		attacker.totalBonusAP = (long)AICombat::combatDifficulty * 2; // +0 - easy, +2 - normal, +4 - hard
		source->critter.movePoints += attacker.totalBonusAP;
	}

	DEV_PRINTF3("\n\n[AI] Begin combat: %s ID: %d. CombatState: %d", attacker.name, source->id, source->critter.combatState);

	CombatAI_Extended(source, target);

	DEV_PRINTF2("\n[AI] End combat: %s. CombatState: %d\n", attacker.name, source->critter.combatState);

	// debugging
	#ifndef NDEBUG
	if (combatDebug) while (fo::func::get_input() != 27 && fo::var::mouse_buttons == 0) fo::func::process_bk();
	#endif
}

/////////////////////////////////////////////////////////////////////////////////////////
/*
static void MoveToLastAttackTile(fo::GameObject* source, fo::GameObject* critter) {
	long tile = -1;
	auto it = lastAttackerTile.find(critter);
	if (it != lastAttackerTile.cend()) {
		tile = it->second;
		if (fo::func::obj_blocking_at(source, tile, source->elevation)) {
			// гекс заблокирован, берем смежный свободный гекс
			tile = AIHelpers::GetFreeTile(source, tile, 2);
			if (tile != -1) DEV_PRINTF("\n[AI] Pick alternate move tile from near team critter.");
		} else {
			DEV_PRINTF("\n[AI] Move to tile from near team critter.");
		}
	}

	// Проверить построение пути
	if (tile != -1) {
		fo::GameObject* object;
		fo::func::make_straight_path_func(source, source->tile, tile, 0, (DWORD*)&object, 0, (void*)fo::funcoffs::obj_blocking_at_);
		if (object) { // путь заблокирован
			critter = object; // тогда движемся к объекту который блокирует путь (make_straight_path_func функция возвращает первый блокируемый объект)
			tile = -1;
		}
	} else {
		// Определить поведение если свободный гекс не был найден
		if (it != lastAttackerTile.cend()) {
			// движемся в сторону где последний раз был враг
			long dir = fo::func::tile_dir(source->tile, it->second);
			tile = fo::func::tile_num_in_direction(source->tile, dir, source->critter.getAP());
			tile = AIHelpers::GetFreeTile(source, tile, 3);
		}
	}

	if (tile != -1) {
		AIHelpers::CombatMoveToTile(source, tile, source->critter.getAP());
	} else {
		// если движение не возможно, пробуем приблизится к со-комаднику (дефолтовое поведение в F2)
		DEV_PRINTF1("\n[AI] Fail move to tile. Try move close to: %s", fo::func::critter_name(critter));
		fo::func::ai_move_steps_closer(source, critter, source->critter.getAP(), 0);
	}
}

static fo::GameObject* __fastcall SetLastAttacker(fo::GameObject* attacker, fo::GameObject* target) {
	lastAttackerTile[target] = attacker->tile;
	return attacker;
}

static void __declspec(naked) combat_attack_hook() {
	__asm {
		push ecx;
		mov  ecx, eax;
		call SetLastAttacker;
		pop  ecx;
		mov  edx, edi;
		jmp  fo::funcoffs::combatAIInfoSetLastTarget_;
	}
}
*/
/////////////////////////////////////////////////////////////////////////////////////////

#ifndef NDEBUG
static void __declspec(naked) ai_move_steps_closer_debug() {
	static const char* move_steps_closer_fail = "\nERROR: ai_move_steps_closer.";
	__asm {
		test eax, eax;
		jns  skip;
		push move_steps_closer_fail;
		call fo::funcoffs::debug_printf_;
		add  esp, 4;
skip:
		add  esp, 0x10;
		pop  ebp;
		pop  edi;
		pop  esi;
		retn;
	}
}
#endif

void AICombat::init(bool smartBehavior) {

	// Enables the use of the RunAwayMode value from the AI-packet for the NPC
	// the min_hp value will be calculated as a percentage of the maximum number of NPC health points, instead of using fixed min_hp values
	npcPercentMinHP = (IniReader::GetConfigInt("CombatAI", "NPCRunAwayMode", 0) > 0);

	if (smartBehavior) {
		// Override combat_ai_ engine function
		HookCall(0x422B94, combat_ai_hook);
		SafeWrite8(0x422B91, 0xF1); // mov  eax, esi > mov ecx, esi
		// swap ASM codes
		SafeWrite32(0x422B89, 0x8904518B);
		SafeWrite8(0x422B8D, 0xF1); // mov  eax, esi > mov ecx, esi

		//HookCall(0X4230E8, combat_attack_hook);
		//LoadGameHook::OnCombatStart() += []() { lastAttackerTile.clear(); };

		// Добавить дополнительные очки действий, взависимомти от настройки сложности боя
		useCombatDifficulty = (IniReader::GetConfigInt("CombatAI", "DifficultyMode", 1) != 0);

		#ifndef NDEBUG
		combatDebug = (IniReader::GetConfigInt("CombatAI", "Debug", 0) != 0);
		MakeJump(0x42A1B6, ai_move_steps_closer_debug);
		#endif
	}
}

}