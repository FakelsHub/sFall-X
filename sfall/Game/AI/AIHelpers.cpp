/*
 *    sfall
 *    Copyright (C) 2008 - 2021 Timeslip and sfall team
 *
 */

#include "..\..\FalloutEngine\Fallout2.h"
#include "..\..\Modules\AI.h"

#include "..\items.h"

#include "AIHelpers.h"

namespace game
{
namespace ai
{

// Returns the friendly critter or any blocking object that located on the line of fire
fo::GameObject* AIHelpers::CheckShootAndTeamCritterOnLineOfFire(fo::GameObject* object, long targetTile, long team) {
	if (object && object->IsCritter() && object->critter.teamNum != team) { // is not friendly fire
		long objTile = object->tile;
		if (objTile == targetTile) return nullptr;

		if (object->flags & fo::ObjectFlag::MultiHex) {
			long dir = fo::func::tile_dir(objTile, targetTile);
			objTile = fo::func::tile_num_in_direction(objTile, dir, 1);
			if (objTile == targetTile) return nullptr; // just in case
		}
		// continue checking the line of fire from object tile to targetTile
		fo::GameObject* obj = object; // for ignoring the object (multihex) when building the path
		fo::func::make_straight_path_func(object, objTile, targetTile, 0, (DWORD*)&obj, 0x20, (void*)fo::funcoffs::obj_shoot_blocking_at_);

		object = CheckShootAndTeamCritterOnLineOfFire(obj, targetTile, team);
	}
	if (object) fo::func::dev_printf("\n[AI] TeamCritter on LineOfFire: %s (team: %d==%d)", fo::func::critter_name(object), object->critter.teamNum, team);

	return object; // friendly critter, any object or null
}

// Returns the friendly critter that located on the line of fire
fo::GameObject* AIHelpers::CheckFriendlyFire(fo::GameObject* target, fo::GameObject* attacker) {
	fo::GameObject* object = nullptr;
	fo::func::make_straight_path_func(attacker, attacker->tile, target->tile, 0, (DWORD*)&object, 0x20, (void*)fo::funcoffs::obj_shoot_blocking_at_);
	object = CheckShootAndTeamCritterOnLineOfFire(object, target->tile, attacker->critter.teamNum);
	return (object && object->IsCritter()) ? object : nullptr; // 0 - if there are no friendly critters
}

fo::GameObject* AIHelpers::CheckFriendlyFire(fo::GameObject* target, fo::GameObject* attacker, long destTile) {
	fo::GameObject* object = nullptr;
	fo::func::make_straight_path_func(attacker, destTile, target->tile, 0, (DWORD*)&object, 0x20, (void*)fo::funcoffs::obj_shoot_blocking_at_);
	return (object) ? ai::AIHelpers::CheckShootAndTeamCritterOnLineOfFire(object, target->tile, attacker->critter.teamNum) : nullptr; // 0 if there are no object
}

bool AIHelpers::AttackInRange(fo::GameObject* source, fo::GameObject* weapon, long distance) {
	if (Items::item_weapon_range(source, weapon, fo::AttackType::ATKTYPE_RWEAPON_PRIMARY) >= distance) return true;
	return (Items::item_weapon_range(source, weapon, fo::AttackType::ATKTYPE_RWEAPON_SECONDARY) >= distance);
}

bool AIHelpers::AttackInRange(fo::GameObject* source, fo::GameObject* weapon, fo::GameObject* target) {
	return AIHelpers::AttackInRange(source, weapon, fo::func::obj_dist(source, target));
}

}
}