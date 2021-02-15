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

long AIHelpers::CombatMoveToTile(fo::GameObject* source, long tile, long dist) {
	fo::func::register_begin(fo::RB_RESERVED);
	fo::func::register_object_move_to_tile(source, tile, source->elevation, dist, -1);
	long result = fo::func::register_end();
	if (!result) {
		__asm call fo::funcoffs::combat_turn_run_;
		if (source->critter.IsNotActiveAndDead()) {
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
		if (source->critter.IsNotActiveAndDead()) {
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
	dist |= 0x01000000; // sfall force flag (stay)
	return fo::func::ai_move_steps_closer(source, target, ~dist, 0); // 0 - ok, -1 - don't move
}

fo::GameObject* AIHelpers::AICheckWeaponSkill(fo::GameObject* source, fo::GameObject* hWeapon, fo::GameObject* sWeapon) {
	if (!hWeapon) return sWeapon;
	if (!sWeapon) return hWeapon;

	int hSkill = fo::func::item_w_skill(hWeapon, fo::AttackType::ATKTYPE_RWEAPON_PRIMARY);
	int sSkill = fo::func::item_w_skill(sWeapon, fo::AttackType::ATKTYPE_RWEAPON_PRIMARY);

	if (hSkill == sSkill) return sWeapon;

	int hLevel = fo::func::skill_level(source, hSkill);
	int sLevel = fo::func::skill_level(source, sSkill) + 10;

	return (hLevel > sLevel) ? hWeapon : sWeapon;
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
	fo::GetProto(item->protoId, &proto);
	long type = (isSecond) ? proto->item.flagsExt >> 4 : proto->item.flagsExt;
	return fo::GetWeaponType(type);
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

// Проверяет относится ли предмет к типу стрелкового или метательному оружию
bool AIHelpers::IsGunOrThrowingWeapon(fo::GameObject* item) {
	fo::Proto* proto;
	fo::GetProto(item->protoId, &proto);

	long typePrimary = fo::GetWeaponType(proto->item.flagsExt);
	if (typePrimary >= fo::AttackSubType::THROWING) {
		return true;
	}
	long typeSecondary = fo::GetWeaponType(proto->item.flagsExt >> 4);
	if (typeSecondary >= fo::AttackSubType::THROWING) {
		return true;
	}
	return false;
}

// Проверяет имеет ли криттер в своем инвентаре патроны к оружию в слоте для перезарядки
bool AIHelpers::CritterHaveAmmo(fo::GameObject* critter, fo::GameObject* weapon) {
	DWORD slotNum = -1;
	while (true) {
		fo::GameObject* ammo = fo::func::inven_find_type(critter, fo::item_type_ammo, &slotNum);
		if (!ammo) break;
		if (fo::func::item_w_can_reload(weapon, ammo)) return true; // можно перезарядить?
	}
	return false;
}

long AIHelpers::GetFreeTile(fo::GameObject* source, long tile, long distMax) {
	long dist = 1;
	do {
		for (size_t r = 0; r < 6; r++)
		{
			long _tile = fo::func::tile_num_in_direction(tile, r, dist);
			if (!fo::func::obj_blocking_at(source, _tile, source->elevation)) {
				return _tile;
			}
		}
	} while (++dist <= distMax);
	return -1;
}

long AIHelpers::GetRandomTile(fo::GameObject* source, long min, long max) {
	long dist = GetRandom(min, max);
	if (dist > 0) {
		long tile = fo::func::tile_num_in_direction(source->tile, GetRandom(0, 5), dist);
		fo::GameObject* object;
		fo::func::make_straight_path_func(source, source->tile, tile, 0, (DWORD*)&object, 0, (void*)fo::funcoffs::obj_blocking_at_);
		return (object) ? GetRandomTile(source, min, max) : tile;
	}
	return -1;
}

long AIHelpers::GetRandomDistTile(fo::GameObject* source, long tile, long distMax) {
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

// TODO
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

}