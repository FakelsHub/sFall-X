/*
 *    sfall
 *    Copyright (C) 2008 - 2021  Timeslip and sfall team
 *
 */
	
#include <array>

#include "..\FalloutEngine\Fallout2.h"
#include "..\main.h"
#include "..\Modules\HookScripts\CombatHs.h"
#include "..\Modules\HookScripts\ObjectHs.h"
#include "..\Modules\Perks.h"

#include "..\Game\stats.h"

#include "items.h"

namespace game
{

namespace sf = sfall;

constexpr int reloadCostAP = 2; // engine default reload AP cost

static std::array<long, 3> healingItemPids = { fo::PID_STIMPAK, fo::PID_SUPER_STIMPAK, fo::PID_HEALING_POWDER };

void Items::SetHealingPID(long index, long pid) {
	/*if (index < 0 && index < 3)*/ healingItemPids[index] = pid;
}

bool Items::IsHealingItem(fo::GameObject* item) {
	 if (std::find(healingItemPids.cbegin(), healingItemPids.cend(), item->protoId) != healingItemPids.cend()) return true;
	 
	 fo::Proto* proto; 
	 if (fo::GetProto(item->protoId, &proto)) {
		return (proto->item.flagsExt & fo::ItemFlags::HealingItem); // TODO check asm code
	 }
	 return false;
}

bool Items::UseDrugItemFunc(fo::GameObject* source, fo::GameObject* item) {
	bool result = (game::Items::item_d_take_drug(source, item) == -1);
	if (result) {
		fo::func::item_add_force(source, item, 1);
	} else {
		fo::func::ai_magic_hands(source, item, 5000);
		fo::func::obj_connect(item, source->tile, source->elevation, 0);
		fo::func::obj_destroy(item);
	}
	return result;
}

// Implementation of item_d_take_ engine function with the HOOK_USEOBJON hook
long Items::item_d_take_drug(fo::GameObject* source, fo::GameObject* item) {
	if (sf::UseObjOnHook_Invoke(source, item, source) == -1) return -1;
	return fo::func::item_d_take_drug(source, item);
}

long Items::item_count(fo::GameObject* who, fo::GameObject* item) {
	for (int i = 0; i < who->invenSize; i++)
	{
		auto tableItem = &who->invenTable[i];
		if (tableItem->object == item) {
			return tableItem->count; // fix
		} else if (fo::func::item_get_type(tableItem->object) == fo::item_type_container) {
			int count = item_count(tableItem->object, item);
			if (count > 0) return count;
		}
	}
	return 0;
}

long Items::item_weapon_range(fo::GameObject* source, fo::GameObject* weapon, long hitMode) {
	fo::Proto* wProto;
	if (!GetProto(weapon->protoId, &wProto)) return 0;

	long isSecondMode = (hitMode && hitMode != fo::AttackType::ATKTYPE_RWEAPON_PRIMARY) ? 1 : 0;
	long range = wProto->item.weapon.maxRange[isSecondMode];

	long flagExt = wProto->item.flagsExt;
	if (isSecondMode) flagExt = (flagExt >> 4);
	long type = fo::GetWeaponType(flagExt);

	if (type == fo::AttackSubType::THROWING) {
		long heaveHoMod = Stats::perk_level(source, fo::Perk::PERK_heave_ho);
		long stRange = fo::func::stat_level(source, fo::Stat::STAT_st);

		if (sf::Perks::perkHeaveHoModTweak) {
			stRange *= 3;
			if (stRange > range) stRange = range;
			return stRange + (heaveHoMod * 6);
		}

		// vanilla
		stRange += (heaveHoMod * 2);
		if (stRange > 10) stRange = 10; // fix for Heave Ho!
		stRange *= 3;
		if (stRange < range) range = stRange;
	}
	return range;
}

// TODO
//long Items::item_w_range(fo::GameObject* source, long hitMode) {
//	return item_weapon_range(source, fo::func::item_hit_with(source, hitMode), hitMode);
//}

static bool fastShotTweak = false;

// Implementing the item_w_primary_mp_cost and item_w_secondary_mp_cost engine functions in single function with the HOOK_CALCAPCOST hook
long __fastcall Items::item_weapon_mp_cost(fo::GameObject* source, fo::GameObject* weapon, long hitMode, long isCalled) {
	long cost = 0;

	switch (hitMode) {
	case fo::AttackType::ATKTYPE_LWEAPON_PRIMARY:
	case fo::AttackType::ATKTYPE_RWEAPON_PRIMARY:
		cost = fo::func::item_w_primary_mp_cost(weapon);
		break;
	case fo::AttackType::ATKTYPE_LWEAPON_SECONDARY:
	case fo::AttackType::ATKTYPE_RWEAPON_SECONDARY:
		cost = fo::func::item_w_secondary_mp_cost(weapon);
		break;
	case fo::AttackType::ATKTYPE_LWEAPON_RELOAD:
	case fo::AttackType::ATKTYPE_RWEAPON_RELOAD:
		if (weapon && weapon->protoId != fo::ProtoID::PID_SOLAR_SCORCHER) { // Solar Scorcher has no reload AP cost
			cost = reloadCostAP;
			if (fo::GetProto(weapon->protoId)->item.weapon.perk == fo::Perk::PERK_weapon_fast_reload) {
				cost--;
			}
		}
	}
	if (hitMode < fo::AttackType::ATKTYPE_LWEAPON_RELOAD) {
		if (isCalled) cost++;
		if (cost < 0) cost = 0;

		long type = fo::func::item_w_subtype(weapon, hitMode);

		if (source->protoId == fo::ProtoID::PID_Player && sf::Perks::DudeHasTrait(fo::Trait::TRAIT_fast_shot)) {
			if (fastShotTweak || // Fallout 1 behavior and Alternative behavior (allowed for all weapons)
			   (fo::func::item_w_range(source, hitMode) >= 2 && type > fo::AttackSubType::MELEE)) // Fallout 2 behavior (with fix) and Haenlomal's fix
			{
				cost--;
			}
		}
		if ((type == fo::AttackSubType::MELEE || type == fo::AttackSubType::UNARMED) && Stats::perk_level(source, fo::Perk::PERK_bonus_hth_attacks)) {
			cost--;
		}
		if (type == fo::AttackSubType::GUNS && Stats::perk_level(source, fo::Perk::PERK_bonus_rate_of_fire)) {
			cost--;
		}
		if (cost < 1) cost = 1;
	}
	return sf::CalcAPCostHook_Invoke(source, hitMode, isCalled, cost, weapon);
}

// Implementation of item_w_mp_cost_ engine function with the HOOK_CALCAPCOST hook
long Items::item_w_mp_cost(fo::GameObject* source, long hitMode, long isCalled) {
	long cost = fo::func::item_w_mp_cost(source, hitMode, isCalled);
	return sf::CalcAPCostHook_Invoke(source, hitMode, isCalled, cost, nullptr);
}

static void __declspec(naked) ai_search_inven_weap_hook() {
	using namespace fo;
	__asm {
		push 0;        // no called
		push ATKTYPE_RWEAPON_PRIMARY;
		mov  edx, esi; // find weapon
		mov  ecx, edi; // source
		call Items::item_weapon_mp_cost;
		retn;
	}
}

void Items::init() {
	// Replacement the item_w_primary_mp_cost_ function to the sfall implementation with the HOOK_CALCAPCOST hook
	sf::HookCall(0x429A08, ai_search_inven_weap_hook);

	int fastShotFix = sf::IniReader::GetConfigInt("Misc", "FastShotFix", 0);
	fastShotTweak = (fastShotFix > 0 && fastShotFix <= 3);
}

}