/*
 *    sfall
 *    Copyright (C) 2021  The sfall team
 *
 */

#include "..\..\FalloutEngine\Fallout2.h"
#include "..\..\Utils.h"
#include "..\AI.h"

#include "..\..\Game\items.h"

#include "AI.FuncHelpers.h"

namespace sfall
{

//fo::GameObject* AIHelpers::CheckShootAndTeamCritterOnLineOfFire(fo::GameObject* object, long targetTile, long team) {
//	if (object && object->Type() == fo::ObjType::OBJ_TYPE_CRITTER && object->critter.teamNum != team) { // is not friendly fire critter
//		long objTile = object->tile;
//
//		if (object->flags & fo::ObjectFlag::MultiHex) { // если криттер много гексовый
//			long dir = fo::func::tile_dir(objTile, targetTile);
//			objTile = fo::func::tile_num_in_direction(objTile, dir, 1);
//		}
//		if (objTile == targetTile) return nullptr;
//
//		// continue check the line_of_fire from object tile to targetTile
//		fo::GameObject*	obj = object;
//		fo::func::make_straight_path_func(object, objTile, targetTile, 0, (DWORD*)&obj, 0x20, (void*)fo::funcoffs::obj_shoot_blocking_at_);
//		if (!CheckShootAndTeamCritterOnLineOfFire(obj, targetTile, team)) return nullptr;
//	}
//	return object;
//}
//
//fo::GameObject* AIHelpers::CheckFriendlyFire(fo::GameObject* target, fo::GameObject* attacker) {
//	fo::GameObject* object = nullptr;
//	fo::func::make_straight_path_func(attacker, attacker->tile, target->tile, 0, (DWORD*)&object, 0x20, (void*)fo::funcoffs::obj_shoot_blocking_at_);
//	if (object) object = CheckShootAndTeamCritterOnLineOfFire(object, target->tile, attacker->critter.teamNum);
//	return (!object || object->TypeFid() == fo::ObjType::OBJ_TYPE_CRITTER) ? object : nullptr; // 0 if there are no friendly critters
//}

fo::GameObject* AIHelpers::CheckFriendlyFire(fo::GameObject* target, fo::GameObject* attacker, long destTile) {
	fo::GameObject* object = nullptr;
	fo::func::make_straight_path_func(attacker, destTile, target->tile, 0, (DWORD*)&object, 0x20, (void*)fo::funcoffs::obj_shoot_blocking_at_);
	return (object) ? AI::CheckShootAndTeamCritterOnLineOfFire(object, target->tile, attacker->critter.teamNum) : nullptr; // 0 if there are no object
}

bool AIHelpers::AICanUseWeapon(fo::GameObject* weapon) {
	return (weapon->item.miscFlags & fo::MiscFlags::CantUse) == 0;
}

bool AIHelpers::AttackInRange(fo::GameObject* source, fo::GameObject* weapon, long distance) {
	if (game::Items::item_weapon_range(source, weapon, fo::AttackType::ATKTYPE_RWEAPON_PRIMARY) >= distance) return true;
	return (game::Items::item_weapon_range(source, weapon, fo::AttackType::ATKTYPE_RWEAPON_SECONDARY) >= distance);
}

bool AIHelpers::AttackInRange(fo::GameObject* source, fo::GameObject* weapon, fo::GameObject* target) {
	return AIHelpers::AttackInRange(source, weapon, fo::func::obj_dist(source, target));
}

fo::GameObject* AIHelpers::GetNearestEnemyCritter(fo::GameObject* source) {
	fo::GameObject* enemyCritter = fo::func::ai_find_nearest_team_in_combat(source, source, 2);
	return (enemyCritter && enemyCritter->critter.getHitTarget()->critter.teamNum == source->critter.teamNum) ? enemyCritter : nullptr;
}

long AIHelpers::CombatMoveToObject(fo::GameObject* source, fo::GameObject* target, long dist) {
	fo::func::register_begin(fo::RB_RESERVED);
	if (dist >= 3) {
		fo::func::register_object_run_to_object(source, target, dist, -1);
	} else {
		fo::func::register_object_move_to_object(source, target, dist, -1);
	}
	long result = fo::func::register_end();
	if (!result) {
		__asm call fo::funcoffs::combat_turn_run_;
		if (source == fo::var::obj_dude) {
			if (source->critter.IsDead()) { /*| fo::DamageFlag::DAM_KNOCKED_OUT | fo::DamageFlag::DAM_LOSE_TURN*/
				return -2; // break attack for dude
			}
		} else if (source->critter.IsNotActiveOrDead()) {
			source->critter.movePoints = 0; // для предотвращения дальнейших действий в бою
		}
	}
	return result; // 0 - ok, -1 - error
}

long AIHelpers::CombatMoveToTile(fo::GameObject* source, long tile, long dist) {
	fo::func::register_begin(fo::RB_RESERVED);
	fo::func::register_object_move_to_tile(source, tile, source->elevation, dist, -1);
	long result = fo::func::register_end();
	if (!result) {
		__asm call fo::funcoffs::combat_turn_run_;
		if (source->critter.IsNotActiveOrDead()) {
			source->critter.movePoints = 0; // для предотвращения дальнейших действий в бою
		}
	}
	return result; // 0 - ok, -1 - error
}

long AIHelpers::CombatRunToTile(fo::GameObject* source, long tile, long dist) {
	fo::func::register_begin(fo::RB_RESERVED);
	fo::func::register_object_run_to_tile(source, tile, source->elevation, dist, -1);
	long result = fo::func::register_end();
	if (!result) {
		__asm call fo::funcoffs::combat_turn_run_;
		if (source->critter.IsNotActiveOrDead()) {
			source->critter.movePoints = 0; // для предотвращения дальнейших действий в бою
		}
	}
	return result; // 0 - ok, -1 - error
}

long AIHelpers::ForceMoveToTarget(fo::GameObject* source, fo::GameObject* target, long dist) {
	dist |= 0x02000000; // sfall force flag (stay and stay_close)
	return fo::func::ai_move_steps_closer(source, target, ~dist, 0); // 0 - ok, -1 - don't move
}

long AIHelpers::MoveToTarget(fo::GameObject* source, fo::GameObject* target, long dist) {
	dist |= 0x01000000; // sfall force flag (stay_close)
	return fo::func::ai_move_steps_closer(source, target, ~dist, 0); // 0 - ok, -1 - don't move
}

fo::GameObject* AIHelpers::AICheckWeaponSkill(fo::GameObject* source, fo::GameObject* hWeapon, fo::GameObject* sWeapon) {
	if (!hWeapon) return sWeapon;
	if (!sWeapon) return hWeapon;

	int hSkill = fo::func::item_w_skill(hWeapon, fo::AttackType::ATKTYPE_RWEAPON_PRIMARY);
	int sSkill = fo::func::item_w_skill(sWeapon, fo::AttackType::ATKTYPE_RWEAPON_PRIMARY);

	if (hSkill == sSkill) return sWeapon;

	int hLevel = fo::func::skill_level(source, hSkill);
	int sLevel = fo::func::skill_level(source, sSkill);

	fo::func::dev_printf("\n[AI] Check weapon skill: %d vs %d", hLevel, sLevel);

	// если разница в навыках не большая то отдаем предпочитение стрелковому оружию
	if (std::abs(hLevel - sLevel) <= 10) {
		if (sSkill >= fo::Skill::SKILL_SMALL_GUNS && sSkill <= fo::Skill::SKILL_ENERGY_WEAPONS) return sWeapon;
		if (hSkill >= fo::Skill::SKILL_SMALL_GUNS && hSkill <= fo::Skill::SKILL_ENERGY_WEAPONS) return hWeapon;
	}
	return (hLevel > (sLevel + 10)) ? hWeapon : sWeapon;
}

long AIHelpers::GetCurrenShootAPCost(fo::GameObject* source, long modeHit, long isCalled) {
	fo::GameObject* item = fo::func::inven_right_hand(source);
	if (fo::func::item_get_type(item) != fo::ItemType::item_type_weapon) return -1;
	return game::Items::item_weapon_mp_cost(source, item, modeHit, isCalled);
}

long AIHelpers::GetCurrenShootAPCost(fo::GameObject* source, fo::GameObject* target) {
	fo::GameObject* item = fo::func::inven_right_hand(source);
	if (fo::func::item_get_type(item) != fo::ItemType::item_type_weapon) return -1;
	return GetCurrenShootAPCost(source, target, item);
}

long AIHelpers::GetCurrenShootAPCost(fo::GameObject* source, fo::GameObject* target, fo::GameObject* weapon) {
	long modeHit = fo::func::ai_pick_hit_mode(source, weapon, target);
	return game::Items::item_weapon_mp_cost(source, weapon, modeHit, 0);
}

fo::AttackSubType AIHelpers::GetWeaponSubType(fo::GameObject* item, bool isSecond) {
	fo::Proto* proto;
	fo::util::GetProto(item->protoId, &proto);
	long type = (isSecond) ? proto->item.flagsExt >> 4 : proto->item.flagsExt;
	return fo::util::GetWeaponType(type);
}

fo::AttackSubType AIHelpers::GetWeaponSubType(fo::GameObject* item, fo::AttackType hitMode) {
	bool isSecond = (hitMode == fo::AttackType::ATKTYPE_RWEAPON_SECONDARY || fo::AttackType::ATKTYPE_LWEAPON_SECONDARY);
	return GetWeaponSubType(item, isSecond);
}

// Проверяет относится ли предмет к типу стрелкового или метательному оружию
bool AIHelpers::IsGunOrThrowingWeapon(fo::GameObject* item, long type) {
	fo::Proto* proto = fo::util::GetProto(item->protoId);

	if (type > fo::AttackType::ATKTYPE_LWEAPON_SECONDARY) type--;

	if (type == -1 || type == fo::AttackType::ATKTYPE_LWEAPON_PRIMARY) {
		long typePrimary = fo::util::GetWeaponType(proto->item.flagsExt);
		if (typePrimary >= fo::AttackSubType::THROWING) {
			return true;
		}
	}
	if (type == -1 || type == fo::AttackType::ATKTYPE_LWEAPON_SECONDARY) {
		long typeSecondary = fo::util::GetWeaponType(proto->item.flagsExt >> 4);
		if (typeSecondary >= fo::AttackSubType::THROWING) {
			return true;
		}
	}
	return false;
}

// Получает свободный гекс по направлению и дистанции
long AIHelpers::GetFreeTile(fo::GameObject* source, long tile, long distMax, long dir) {
	// рандомно смежные гексы
	long r = (GetRandom(1, 2) == 2) ? 5 : 1;
	long dirNear0 = (dir + r) % 6;
	r = (r == 1) ? 5 : 1;
	long dirNear1 = (dir + r) % 6;

	long freeTile = -1;
	long dist = 1;

	while (true)
	{
getTile:
		long _tile = fo::func::tile_num_in_direction(tile, dirNear0, 1);
		if (!fo::func::obj_blocking_at(source, _tile, source->elevation)) {
			freeTile = _tile;
			break;
		}
		_tile = fo::func::tile_num_in_direction(tile, dirNear1, 1);
		if (!fo::func::obj_blocking_at(source, _tile, source->elevation)) {
			freeTile = _tile;
			break;
		}
		_tile = fo::func::tile_num_in_direction(tile, dir, 1);
		if (!fo::func::obj_blocking_at(source, _tile, source->elevation)) {
			freeTile = _tile;
		}
		break;
	}

	if (freeTile != -1 && dist < distMax) {
		dist++;
		tile = freeTile;
		goto getTile;
	}
	return freeTile;
}

long AIHelpers::GetDirFreeTile(fo::GameObject* source, long tile, long distMax) {
	long dist = 1;
	do {
		long dir = GetRandom(0, 5);
		for (size_t r = 0; r < 6; r++)
		{
			long _tile = fo::func::tile_num_in_direction(tile, dir, dist);
			if (!fo::func::obj_blocking_at(source, _tile, source->elevation)) {
				return _tile;
			}
			if (++dir > 5) dir = 0;
		}
	} while (++dist <= distMax);
	return -1;
}

long AIHelpers::GetRandomTile(long sourceTile, long minDist, long maxDist) {
	long dist = (maxDist > minDist) ? GetRandom(minDist, maxDist) : minDist;
	if (dist > 0) {
		long dx = GetRandom(-dist, dist) * 32;
		long dy = GetRandom(-dist, dist) * 16;

		long x, y;
		fo::func::tile_coord(sourceTile, &x, &y);
		x += dx + 16;
		y += dy + 8;
		long tile = fo::func::tile_num(x, y);
		if (tile == sourceTile) tile++;
		return tile;
	}
	return -1;
}

long AIHelpers::GetRandomTileToMove(fo::GameObject* source, long minDist, long maxDist) {
	long iteration = (maxDist + minDist) * 10;
	while (true)
	{
		if (--iteration < 0) break;
		long tile = GetRandomTile(source->tile, minDist, maxDist);
		if (tile == -1) break;
		if (fo::func::make_path_func(source, source->tile, tile, 0, 1, (void*)fo::funcoffs::obj_blocking_at_)) {
			return tile;
		}
	}
	return -1;
}

long AIHelpers::GetRandomDistTile(fo::GameObject* source, long tile, long distMax) { // переделать рекурсию с дистанцией
	if (distMax <= 0) return -1;
	long dist = GetRandom(1, distMax);
	long _tile = fo::func::tile_num_in_direction(tile, GetRandom(0, 5), dist);
	fo::GameObject* object;
	fo::func::make_straight_path_func(source, source->tile, _tile, 0, (DWORD*)&object, 0, (void*)fo::funcoffs::obj_blocking_at_);
	return (object) ? GetRandomDistTile(source, tile, --distMax) : _tile;
}

// Проверяет простреливается ли линия от sourceTile до targetTile
// Return result: 0 - блокировано, 1 - простреливается, -1 - простреливается, но целевой гекс занят критером
long CheckLineOfFire(long sourceTile, long targetTile) {
	fo::GameObject* object = nullptr;

	do {
		fo::func::make_straight_path_func(object, sourceTile, targetTile, 0, (DWORD*)&object, 0x20, (void*)fo::funcoffs::obj_shoot_blocking_at_);
		if (!object) return 1;
		if (object->TypeFid() != fo::ObjType::OBJ_TYPE_CRITTER) return 0;

		// линию преграждает криттер
		if (object->flags & fo::ObjectFlag::MultiHex) { // криттер многогексовый
			long dir = fo::func::tile_dir(object->tile, targetTile);
			sourceTile = fo::func::tile_num_in_direction(object->tile, dir, 1);
		} else {
			sourceTile = object->tile;
		}
	} while (sourceTile != targetTile);

	return -1;
}

//
// нужна ли поддержка для многогексовых критеров? если нужна то заменить на combat_is_shot_blocked_
bool AIHelpers::CanSeeObject(fo::GameObject* source, fo::GameObject* target) {
	fo::GameObject* src = source;
	fo::GameObject* object = target;
	do {
		fo::func::make_straight_path_func(src, src->tile, target->tile, 0, (DWORD*)&object, 0x20, (void*)fo::funcoffs::obj_shoot_blocking_at_);
		if (object && object->TypeFid() != fo::ObjType::OBJ_TYPE_CRITTER && target != object) {
			return false;
		}
		src = object;
	} while (object && object->tile != target->tile);

	return true;
}

//bool AIHelpers::IsSeeObject(fo::GameObject* source, fo::GameObject* target) {
//	return fo::func::can_see(source, target) && AIHelpers::CanSeeObject(source, target);
//}

static fo::GameObject* __fastcall obj_ai_move_blocking_at(fo::GameObject* source, long tile, long elev) {
	if (tile < 0 || tile >= 40000) return nullptr;

	fo::ObjectTable* obj = fo::var::objectTable[tile];
	while (obj)
	{
		if (elev == obj->object->elevation) {
			fo::GameObject* object = obj->object;
			long flags = object->flags;
			// не установлены флаги Mouse_3d, NoBlock и WalkThru
			if (!(flags & fo::ObjectFlag::Mouse_3d) && !(flags & fo::ObjectFlag::NoBlock) && !(flags & fo::ObjectFlag::WalkThru) && source != object) {
				if (object->TypeFid() == fo::ObjType::OBJ_TYPE_CRITTER) fo::var::moveBlockObj = object;
				return object;
			}
		}
		obj = obj->nextObject;
	}
	// проверка на наличее мультигексовых объектов
	long direction = 0;
	do {
		long _tile = fo::func::tile_num_in_direction(tile, direction, 1);
		if (_tile >= 0 && _tile < 40000) {
			obj = fo::var::objectTable[_tile];
			while (obj)
			{
				if (elev == obj->object->elevation) {
					fo::GameObject* object = obj->object;
					long flags = object->flags;
					if (flags & fo::ObjectFlag::MultiHex && !(flags & fo::ObjectFlag::Mouse_3d) && !(flags & fo::ObjectFlag::NoBlock) && !(flags & fo::ObjectFlag::WalkThru) && source != object) {
						if (object->TypeFid() == fo::ObjType::OBJ_TYPE_CRITTER) fo::var::moveBlockObj = object;
						return object;
					}
				}
				obj = obj->nextObject;
			}
		}
	} while (++direction < 6);
	return nullptr;
}

void __declspec(naked) AIHelpers::obj_ai_move_blocking_at_() {
	__asm {
		push ecx;
		push ebx;
		mov  ecx, eax;
		call obj_ai_move_blocking_at;
		pop  ecx;
		retn;
	}
}

// TODO: WIP
/*
fo::GameObject* obj_light_blocking_at(fo::GameObject* source, long tile, long elev) {
	if (tile < 0 || tile >= 40000) return nullptr;

	fo::ObjectTable* obj = fo::var::objectTable[tile];
	while (obj)
	{
		if (elev == obj->object->elevation) {
			fo::GameObject* object = obj->object;
			long flags = object->flags;
			// если не установлены флаги: Mouse_3d && (NoBlock || LightThru)
			if (!(flags & fo::ObjectFlag::Mouse_3d) && (!(flags & fo::ObjectFlag::NoBlock) || !(flags & fo::ObjectFlag::LightThru)) && source != object) {
				char type = object->TypeFid();
				if (type == fo::ObjType::OBJ_TYPE_SCENERY || type == fo::ObjType::OBJ_TYPE_WALL || type == fo::ObjType::OBJ_TYPE_CRITTER) {
					return object; //
				}
			}
		}
		obj = obj->nextObject;
	}
	// проверка наличия мультигексовых объектов
	long direction = 0;
	do {
		long _tile = fo::func::tile_num_in_direction(tile, direction, 1);
		if (_tile >= 0 && _tile < 40000) {
			obj = fo::var::objectTable[_tile];
			while (obj)
			{
				if (elev == obj->object->elevation) {
					fo::GameObject* object = obj->object;
					long flags = object->flags;
					// если не установлены флаги: Mouse_3d && (NoBlock || LightThru)
					if (!(flags & fo::ObjectFlag::Mouse_3d) && (!(flags & fo::ObjectFlag::NoBlock) || !(flags & fo::ObjectFlag::LightThru)) && source != object) {
						char type = object->TypeFid();
						if (type == fo::ObjType::OBJ_TYPE_SCENERY || type == fo::ObjType::OBJ_TYPE_WALL || type == fo::ObjType::OBJ_TYPE_CRITTER) {
							return object; //
						}
					}
				}
				obj = obj->nextObject;
			}
		}
	} while (++direction < 6);
	return nullptr;
}
*/
}