/*
 *    sfall
 *    Copyright (C) 2008 - 2021  Timeslip and sfall team
 *
 */

#include "..\FalloutEngine\Fallout2.h"

#include "..\Modules\\HookScripts\CombatHs.h"

#include "items.h"

namespace game
{

namespace sf = sfall;

// Implementing the item_w_primary_mp_cost and item_w_secondary_mp_cost engine functions in single function with the HOOK_CALCAPCOST hook
// returns -1 in case of an error
long Items::item_weapon_mp_cost(fo::GameObject* source, fo::GameObject* weapon, long hitMode, long isCalled) {
	long cost = -1;

	switch (hitMode) {
	case fo::AttackType::ATKTYPE_LWEAPON_PRIMARY:
	case fo::AttackType::ATKTYPE_RWEAPON_PRIMARY:
		cost = fo::func::item_w_primary_mp_cost(weapon);
		if (isCalled && cost != -1) cost++;
		break;
	case fo::AttackType::ATKTYPE_LWEAPON_SECONDARY:
	case fo::AttackType::ATKTYPE_RWEAPON_SECONDARY:
		cost = fo::func::item_w_secondary_mp_cost(weapon);
		if (isCalled && cost != -1) cost++;
		break;
	case fo::AttackType::ATKTYPE_LWEAPON_RELOAD:
	case fo::AttackType::ATKTYPE_RWEAPON_RELOAD:
		if (weapon) cost = 2; // default reload AP cost
	}

	return (cost != -1) ? sf::CalcAPCostHook_CheckScript(source, hitMode, isCalled, cost, weapon) : cost;
}

// Implementation of item_w_mp_cost_ engine function with the HOOK_CALCAPCOST hook
long __fastcall Items::item_w_mp_cost(fo::GameObject* source, long hitMode, long isCalled) {
	long cost = fo::func::item_w_mp_cost(source, hitMode, isCalled);
	return sf::CalcAPCostHook_CheckScript(source, hitMode, isCalled, cost, nullptr);
}

void Items::init() {

}

}