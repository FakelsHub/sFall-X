/*
 *    sfall
 *    Copyright (C) 2008 - 2021 Timeslip and sfall team
 *
 */

#include "..\FalloutEngine\Fallout2.h"

#include "..\Modules\HookScripts\CombatHs.h"

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
static std::vector<long> healingItemsPids = { fo::PID_STIMPAK, fo::PID_SUPER_STIMPAK, fo::PID_HEALING_POWDER };

static bool CheckHealingItems(fo::GameObject* item) {
	return (std::find(healingItemsPids.cbegin(), healingItemsPids.cend(), item->protoId) != healingItemsPids.cend());
}

// True - use fail
static bool UseItemDrugFunc(fo::GameObject* source, fo::GameObject* item) {
	bool result = (fo::func::item_d_take_drug(source, item) == -1);
	if (result) {
		fo::func::item_add_force(source, item, 1);
	} else {
		fo::func::ai_magic_hands(source, item, 5000);
		fo::func::obj_connect(item, source->tile, source->elevation, 0);
		fo::func::obj_destroy(item);
	}
	return result;
}

void CombatAI::ai_check_drugs(fo::GameObject* source) {
	if (fo::func::critter_body_type(source)) return; // не могут использовать Robotic/Quadruped

	DWORD slot = -1;
	long noInvenItem = 0;
	bool drugIsUse = false;

	fo::GameObject* lastItem = nullptr; // combatAIInfoGetLastItem_(source); неиспользуемая функция, всегда возвращает 0
	if (!lastItem) {
		fo::AIcap* cap = fo::func::ai_cap(source);
		if (!cap) return;

		long hp_percent = 50;
		long chance = 0;

		switch ((fo::AIpref::chem_use_mode)cap->chem_use)
		{
			case fo::AIpref::chem_use_mode::stims_when_hurt_little: // "stims_when_hurt_little" (использовать только лечилки)
				hp_percent = 60;
				break;
			case fo::AIpref::chem_use_mode::stims_when_hurt_lots:   // "stims_when_hurt_lots" (использовать только лечилки)
				hp_percent = 30;
				break;
			case fo::AIpref::chem_use_mode::sometimes:
				if (!(fo::var::combatNumTurns % 3)) chance = 25;
				//hp_percent = 50;
				break;
			case fo::AIpref::chem_use_mode::anytime:
				if (!(fo::var::combatNumTurns % 3)) chance = 75;
				//hp_percent = 50;
				break;
			case fo::AIpref::chem_use_mode::always:
				chance = 100;
				break;
			case fo::AIpref::chem_use_mode::clean: // "clean" (неупотреблять химию)
				return;
		}
		long min_hp = hp_percent * fo::func::stat_level(source, fo::Stat::STAT_max_hit_points) / 100;

		while (fo::func::stat_level(source, fo::Stat::STAT_current_hp) < min_hp && source->critter.getAP() >= aiUseItemAPCost) // проверяем если текущие HP меньше порогового то лечимся
		{
			fo::GameObject* itemFind = fo::func::inven_find_type(source, fo::ItemType::item_type_drug, &slot);
			if (!itemFind) {
				noInvenItem = 1;
				break;
			}
			// если это стимпаки то принимаем
			if (CheckHealingItems(itemFind) && !fo::func::item_remove_mult(source, itemFind, 1)) {
				if (!UseItemDrugFunc(source, itemFind)) {
					drugIsUse = true;
				}

				if (source->critter.getAP() < aiUseItemAPCost) {
					source->critter.movePoints = 0;
				} else {
					source->critter.movePoints -= aiUseItemAPCost;
				}
				slot = -1;
			}
		}

		// принимаем любой наркотик кроме лечилок если имеется шанс использования
		if (!drugIsUse && chance > 0 && fo::func::roll_random(0, 100) < chance) {
			long useCounter = 0;
			while (source->critter.getAP() >= aiUseItemAPCost)
			{
				fo::GameObject* itemObj = fo::func::inven_find_type(source, fo::ItemType::item_type_drug, &slot);
				if (!itemObj) {
					noInvenItem = 1;
					break;
				}

				long counter = 0;
				// если преопарат (не)равен предпочтению первого предмета то проверить следующие два
				while (itemObj->protoId == cap->chem_primary_desire[counter]); // выполнять цикл пока счетчие меньше 3 и предмет (не)равен проверяемого предмета предпочнения
				{
					if (++counter > 2) break; // next chem_primary_desire
				}

				// если счетчик предпочтения мешьше 3х то можно использовать
				if (counter < 3) {
					// если это не "Личилки" то принимаем химию
					if (!CheckHealingItems(itemObj) && !fo::func::item_remove_mult(source, itemObj, 1)) {
						if (!UseItemDrugFunc(source, itemObj)) {
							drugIsUse = true;
							useCounter++;
						}

						if (source->critter.getAP() < aiUseItemAPCost) {
							source->critter.movePoints = 0;
						} else {
							source->critter.movePoints -= aiUseItemAPCost;
						}
						slot = -1;

						fo::AIpref::chem_use_mode chemUse = (fo::AIpref::chem_use_mode)cap->chem_use;
						if (chemUse == fo::AIpref::chem_use_mode::sometimes ||
						   (chemUse == fo::AIpref::chem_use_mode::anytime && useCounter >= 2))
						{
							break;
						}
					}
				}
			}
		}
	}
	// искать Drugs на карте
	if (lastItem || !drugIsUse && noInvenItem == 1) {
		do {
			if (!lastItem) lastItem = fo::func::ai_search_environ(source, fo::ItemType::item_type_drug);
			if (!lastItem) lastItem = fo::func::ai_search_environ(source, fo::ItemType::item_type_misc_item);
			if (lastItem) lastItem = fo::func::ai_retrieve_object(source, lastItem);

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

void CombatAI::init() { // TODO: add to main.cpp
}

}