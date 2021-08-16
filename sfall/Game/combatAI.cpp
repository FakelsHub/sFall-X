/*
 *    sfall
 *    Copyright (C) 2008 - 2021 Timeslip and sfall team
 *
 */

#include "..\FalloutEngine\Fallout2.h"
#include "..\Modules\HookScripts\CombatHs.h"
#include "..\main.h"

#include "items.h"

#include "..\Modules\SubModules\AI.Inventory.h"

#include "combatAI.h"

namespace game
{

namespace sf = sfall;

// Implementation of ai_can_use_weapon_ engine function with the HOOK_CANUSEWEAPON hook
bool CombatAI::ai_can_use_weapon(fo::GameObject* source, fo::GameObject* weapon, long hitMode) {
	bool result = fo::func::ai_can_use_weapon(source, weapon, hitMode);
	return sf::CanUseWeaponHook_Invoke(result, source, weapon, hitMode);
}

static const long aiUseItemAPCost = 2;
static std::vector<long> healingItemPids = { fo::PID_STIMPAK, fo::PID_SUPER_STIMPAK, fo::PID_HEALING_POWDER };

static bool CheckHealingItems(fo::GameObject* item) {
	return (std::find(healingItemPids.cbegin(), healingItemPids.cend(), item->protoId) != healingItemPids.cend());
}

// True - use fail
static bool UseItemDrugFunc(fo::GameObject* source, fo::GameObject* item) {
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

static long drugUsePerfFixMode;

void __stdcall CombatAI::ai_check_drugs(fo::GameObject* source) {
	if (fo::func::critter_body_type(source)) return; // Robotic/Quadruped - they can't use

	DWORD slot = -1;
	long noInvenDrug = 0;
	bool drugWasUse = false;

	fo::GameObject* lastItem = nullptr; // combatAIInfoGetLastItem_(source); unused function, always returns 0
	if (!lastItem) {
		fo::AIcap* cap = fo::func::ai_cap(source);
		if (!cap) return;

		long hpPercent = 50;
		long chance = 0;

		switch ((fo::AIpref::chem_use_mode)cap->chem_use)
		{
			case fo::AIpref::chem_use_mode::stims_when_hurt_little: // use only healing drugs
				hpPercent = 60;
				break;
			case fo::AIpref::chem_use_mode::stims_when_hurt_lots:   // use only healing drugs
				hpPercent = 30;
				break;
			case fo::AIpref::chem_use_mode::sometimes:
				if (!(fo::var::combatNumTurns % 3)) chance = 25; // every three turns
				//hpPercent = 50;
				break;
			case fo::AIpref::chem_use_mode::anytime:
				if (!(fo::var::combatNumTurns % 3)) chance = 75; // every three turns
				//hpPercent = 50;
				break;
			case fo::AIpref::chem_use_mode::always:
				chance = 100; // 99%
				break;
			case fo::AIpref::chem_use_mode::clean:
				return; // exit: don't use drugs
		}

		long minHP = (hpPercent * fo::func::stat_level(source, fo::Stat::STAT_max_hit_points)) / 100;

		// [FIX] Fixed not checking minimum hp properly for using stimpaks (prevents premature fleeing)
		if (cap->min_hp > minHP) minHP = cap->min_hp;

		while (fo::func::stat_level(source, fo::Stat::STAT_current_hp) < minHP && source->critter.getAP() >= aiUseItemAPCost)
		{
			fo::GameObject* itemFind = fo::func::inven_find_type(source, fo::ItemType::item_type_drug, &slot);
			if (!itemFind) {
				noInvenDrug = 2; // healing drugs were not found in the inventory (was 1)
				break;
			}

			if (CheckHealingItems(itemFind) && !fo::func::item_remove_mult(source, itemFind, 1)) {
				if (!UseItemDrugFunc(source, itemFind)) {
					drugWasUse = true;
				}

				if (source->critter.getAP() < aiUseItemAPCost) {
					source->critter.movePoints = 0;
				} else {
					source->critter.movePoints -= aiUseItemAPCost;
				}
				slot = -1;
			}
		}

		// use any drug (exception healing drugs) if there is a chance of using it
		if (!drugWasUse && chance > 0 && fo::func::roll_random(0, 100) < chance) {
			long usedCount = 0;
			while (source->critter.getAP() >= aiUseItemAPCost)
			{
				fo::GameObject* item = fo::func::inven_find_type(source, fo::ItemType::item_type_drug, &slot);
				if (!item) {
					noInvenDrug = 1;
					break;
				}

				long counter = 0;

				if (drugUsePerfFixMode > 0) {
					fo::GameObject* firstFindDrug = item;
					do {
						// [FIX] Only allows the use of drugs items that are listed in the chem_primary_desire list (AIDrugUsePerfFix == 2)
						while (item->protoId != cap->chem_primary_desire[counter]) if (++counter > 2) break;
						if (counter <= 2) break; // there is a match

						// [FIX] Fixes for preference when using drugs
						if (drugUsePerfFixMode == 1) {
							item = fo::func::inven_find_type(source, fo::ItemType::item_type_drug, &slot);
							if (!item) {
								item = firstFindDrug;
								break;
							}
							counter = 0;
						}
					} while (counter < 3);
				} else {
					// if the drug is equal to the preference of the item, then check the following [vanilla bug behavior]
					while (item->protoId == cap->chem_primary_desire[counter]) if (++counter > 2) break;
				}

				// if the preference counter is less than 3, then can use item drug
				if (counter < 3) {
					// if the item does not belong to the healing drugs
					if (!CheckHealingItems(item) && !fo::func::item_remove_mult(source, item, 1)) {
						if (!UseItemDrugFunc(source, item)) {
							drugWasUse = true;
							usedCount++;
						}

						if (source->critter.getAP() < aiUseItemAPCost) {
							source->critter.movePoints = 0;
						} else {
							source->critter.movePoints -= aiUseItemAPCost;
						}
						slot = -1;

						fo::AIpref::chem_use_mode chemUse = (fo::AIpref::chem_use_mode)cap->chem_use;
						if (chemUse == fo::AIpref::chem_use_mode::sometimes ||
						   (chemUse == fo::AIpref::chem_use_mode::anytime && usedCount >= 2))
						{
							break;
						}
					}
				}
			}
		}
	}
	// search drugs on the map environment
	if (lastItem || (!drugWasUse /*&& noInvenDrug*/)) {
		do {
			long result = 0;
			if (!lastItem) {
				lastItem = fo::func::ai_search_environ(source, fo::ItemType::item_type_drug);
				result = sf::AIInventory::ai_search_environ_corpse_drug(source, fo::ItemType::item_type_drug, noInvenDrug, lastItem);
			}
			if (!lastItem) {
				lastItem = fo::func::ai_search_environ(source, fo::ItemType::item_type_misc_item);
				result = sf::AIInventory::ai_search_environ_corpse_drug(source, fo::ItemType::item_type_misc_item, noInvenDrug, lastItem);
			}
			if (!result && lastItem) lastItem = fo::func::ai_retrieve_object(source, lastItem);

			// [FIX] Prevents the use of healing drugs when they are not needed
			// noInvenDrug: is set to 2 that healing is required
			if (lastItem && noInvenDrug != 2 && CheckHealingItems(lastItem)) {
				long maxHP = fo::func::stat_level(source, fo::Stat::STAT_max_hit_points);
				if (10 + source->critter.health >= maxHP) { // quick check current HP
					return; // exit: don't use healing item
				}
			}

			if (lastItem && !fo::func::item_remove_mult(source, lastItem, 1)) {
				if (!UseItemDrugFunc(source, lastItem))	lastItem = nullptr;

				if (source->critter.getAP() < aiUseItemAPCost) {
					source->critter.movePoints = 0;
				} else {
					source->critter.movePoints -= aiUseItemAPCost;
				}
			}
		} while (lastItem && source->critter.getAP() >= aiUseItemAPCost);
	}
}

void CombatAI::init() {

	drugUsePerfFixMode = sf::IniReader::GetConfigInt("Misc", "AIDrugUsePerfFix", 0);
	if (drugUsePerfFixMode > 0) sf::dlogr("Applying AI drug use preference fix.", DL_FIX);
}

}