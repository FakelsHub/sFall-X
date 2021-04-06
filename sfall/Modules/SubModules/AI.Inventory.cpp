/*
 *    sfall
 *    Copyright (C) 2021  The sfall team
 *
 */

#include "..\..\FalloutEngine\Fallout2.h"

#include "..\..\SafeWrite.h"
#include "..\Combat.h"
#include "..\HookScripts\CombatHs.h"

#include "..\..\Game\items.h"

#include "AI.Behavior.h"
#include "AI.FuncHelpers.h"

#include "AI.Inventory.h"

namespace sfall
{

fo::GameObject* AIInventory::BestWeapon(fo::GameObject* source, fo::GameObject* weapon1, fo::GameObject* weapon2, fo::GameObject* target) {
	fo::GameObject* bestWeapon = fo::func::ai_best_weapon(source, weapon1, weapon2, target);
	return BestWeaponHook_Invoke(bestWeapon, source, weapon1, weapon2, target);
}

// Проверяет наличие патронов к оружию на карте или в инвентере криттеров
long AIInventory::AICheckAmmo(fo::GameObject* weapon, fo::GameObject* critter) {
	if (weapon->item.charges > 0 || weapon->item.ammoPid == -1) return 1;
	if (AIInventory::CritterHaveAmmo(critter, weapon)) return 1;

	// check ammo in corpses
	fo::GameObject* ammo = AIInventory::ai_search_environ_corpse(critter, fo::ItemType::item_type_ammo, 0, weapon);
	// check ammo on ground
	if (!ammo) ammo = AIInventory::ai_search_environ_ammo(critter, weapon);
	return (ammo) ? 1 : 0;
}

fo::GameObject* AIInventory::SearchInventoryItemType(fo::GameObject* source, long itemType, fo::GameObject* object, fo::GameObject* weapon) {
	fo::GameObject* item;
	DWORD slot = -1;
	while (true)
	{
		item = fo::func::inven_find_type(object, itemType, &slot);
		if (item) {
			switch (itemType) {
			case fo::ItemType::item_type_weapon:
				if (!fo::func::ai_can_use_weapon(source, item, fo::AttackType::ATKTYPE_RWEAPON_PRIMARY)) continue;
				// проверяем наличее и количество имеющихся патронов
				if (!AICheckAmmo(item, source) && Combat::check_item_ammo_cost(item, fo::AttackType::ATKTYPE_RWEAPON_PRIMARY) <= 0) continue;
				break;
			case fo::ItemType::item_type_ammo:
				if (!fo::func::item_w_can_reload(weapon, item)) continue;
				break;
			case fo::ItemType::item_type_drug:
			case fo::ItemType::item_type_misc_item:
				if (!fo::func::ai_can_use_drug(source, item)) continue;
				break;
			default:
				continue;
			}
		}
		break; // exit while
	}
	return item;
}

static long WeaponScore(fo::GameObject* weapon, fo::AIcap* cap, long &outPrefScore) {
	long score;
	fo::AttackSubType weapType = fo::AttackSubType::NONE;
	if (weapon) {
		fo::Proto* proto;
		if (!fo::GetProto(weapon->protoId, &proto)) return 0;
		if (proto->item.flagsExt & fo::ObjectFlag::HiddenItem) return -1;

		weapType = fo::GetWeaponType(proto->item.flagsExt); // ATKTYPE_RWEAPON_PRIMARY

		long maxDmg = proto->item.weapon.maxDamage;
		long minDmg = proto->item.weapon.minDamage;
		score = (maxDmg - minDmg) / 2;

		// пассивные очки за радиус
		long radius = fo::func::item_w_area_damage_radius(weapon, fo::AttackType::ATKTYPE_RWEAPON_PRIMARY);
		if (radius > 1) score += (2 * radius);

		if (proto->item.weapon.perk) score *= 3; // TODO: add AIBestWeaponFix
	} else {
		weapType = fo::AttackSubType::UNARMED;
	}

	int order = 0;
	outPrefScore = 0;
	while (weapType != fo::var::weapPrefOrderings[cap->best_weapon + 1][order]) //*(&weapPrefOrderings[5 * (cap->best_weapon + 1)] + order * 4)
	{
		outPrefScore++;
		if (++order > 4) break;
	}
	return score;
}

// Облегченная реализация функции ai_best_weapon_ без проверки на target
static fo::GameObject* BestWeaponLite(fo::GameObject* source, fo::GameObject* weaponPrimary, fo::GameObject* weaponSecondary) {
	auto cap = fo::func::ai_cap(source);
	if ((fo::AIpref::weapon_pref)cap->best_weapon == fo::AIpref::weapon_pref::random) {
		return (fo::func::roll_random(1, 100) <= 50) ? weaponPrimary : weaponSecondary;
	}

	long primaryPrefScore = 999;
	long primaryScore = WeaponScore(weaponPrimary, cap, primaryPrefScore);
	if (primaryScore == -1) return weaponPrimary;

	long secondaryPrefScore = 999;
	long secondaryScore = WeaponScore(weaponSecondary, cap, secondaryPrefScore);
	if (secondaryScore == -1) return weaponSecondary;

	if (primaryPrefScore == secondaryPrefScore)	{
		if (primaryPrefScore == 999) return nullptr; // ???

		if (std::abs(secondaryScore - primaryScore) <= 5) {
			return (fo::func::item_cost(weaponSecondary) > fo::func::item_cost(weaponPrimary)) ? weaponSecondary : weaponPrimary;
		}
		if (secondaryScore > primaryScore) return weaponSecondary;
	} else {
		// у оружия разное предпочтение
		// у кого очки предпочтения меньше то лучше

		if (weaponPrimary && weaponPrimary->protoId == fo::PID_FLARE && weaponSecondary) {
			return weaponSecondary;
		}
		if (weaponSecondary && weaponSecondary->protoId == fo::PID_FLARE && weaponPrimary) {
			return weaponPrimary;
		}

		fo::AIpref::weapon_pref pref = (fo::AIpref::weapon_pref)cap->best_weapon;
		if ((pref < fo::AIpref::weapon_pref::no_pref || pref > fo::AIpref::weapon_pref::unarmed) && std::abs(secondaryScore - primaryScore) > 5) {
			return (primaryScore < secondaryScore) ? weaponSecondary : weaponPrimary;
		}
		if (primaryPrefScore > secondaryPrefScore) {
			return weaponSecondary;
		}
	}
	return weaponPrimary;
}

// Альтернативная реализация функции ai_search_inven_weap_
fo::GameObject* AIInventory::GetInventoryWeapon(fo::GameObject* source, bool checkAP, bool useHand) {
	if (fo::func::critter_body_type(source) == fo::BodyType::Quadruped && source->protoId != fo::ProtoID::PID_GORIS) {
		return 0;
	}
	fo::GameObject* bestWeapon = (useHand) ? fo::func::inven_right_hand(source) : nullptr;

	DWORD slot = -1;
	while (true)
	{
		fo::GameObject* item = fo::func::inven_find_type(source, fo::ItemType::item_type_weapon, &slot);
		if (!item) break;

		if ((!bestWeapon || item->protoId != bestWeapon->protoId) &&
			(!checkAP || game::Items::item_weapon_mp_cost(source, item, fo::AttackType::ATKTYPE_RWEAPON_PRIMARY, 0) <= source->critter.getAP()) &&
			(fo::func::ai_can_use_weapon(source, item, fo::AttackType::ATKTYPE_RWEAPON_PRIMARY)) &&
			(item->item.ammoPid == -1 || fo::func::item_w_subtype(item, fo::AttackType::ATKTYPE_RWEAPON_PRIMARY) != fo::AttackSubType::GUNS) ||
			 fo::func::item_w_curr_ammo(item) || fo::func::ai_have_ammo(source, item, 0))
		{
			bestWeapon = BestWeaponHook_Invoke(BestWeaponLite(source, bestWeapon, item), source, bestWeapon, item, 0);
		}
	}
	return bestWeapon;
}

// Возвращает первый наденные патроны к оружию в инвентаре криттера
fo::GameObject* AIInventory::GetInventAmmo(fo::GameObject* critter, fo::GameObject* weapon) {
	DWORD slotNum = -1;
	while (true) {
		fo::GameObject* ammo = fo::func::inven_find_type(critter, fo::item_type_ammo, &slotNum);
		if (!ammo) break;
		if (fo::func::item_w_can_reload(weapon, ammo)) return ammo;
	}
	return nullptr;
}

// Проверяет имеет ли криттер в своем инвентаре патроны к оружию для перезарядки
long AIInventory::CritterHaveAmmo(fo::GameObject* critter, fo::GameObject* weapon) {
	if (weapon->protoId == fo::ProtoID::PID_SOLAR_SCORCHER) return -1;
	fo::GameObject* ammo = GetInventAmmo(critter, weapon);
	return (ammo) ? ammo->item.charges : 0;
}

fo::GameObject* AIInventory::FindSafeWeaponAttack(fo::GameObject* source, fo::GameObject* target, fo::GameObject* hWeapon, fo::AttackType &outHitMode) {
	long distance = fo::func::obj_dist(source, target);

	fo::GameObject* pickWeapon = nullptr;
	DWORD slotNum = -1;

	while (true)
	{
		fo::GameObject* item = fo::func::inven_find_type(source, fo::item_type_weapon, &slotNum);
		if (!item) break;
		if (hWeapon && hWeapon->protoId == item->protoId) continue;

		// проверить дальность оружия до цели
		if (AIHelpers::AttackInRange(source, item, distance) == false) continue;

		outHitMode = (fo::AttackType)fo::func::ai_pick_hit_mode(source, item, target);

		if (game::Items::item_weapon_mp_cost(source, item, outHitMode, 0) <= source->critter.getAP() && fo::func::ai_can_use_weapon(source, item, outHitMode))
		{
			if ((item->item.ammoPid == -1 || // оружие не имеет магазина для патронов
				Combat::check_item_ammo_cost(item, outHitMode)) &&
				!fo::func::combat_safety_invalidate_weapon_func(source, pickWeapon, outHitMode, target, 0, 0)) // weapon is safety
			{
				pickWeapon = AIInventory::BestWeapon(source, pickWeapon, item, target);
			}
		}
	}
	return pickWeapon;
}

bool AIInventory::AITryReloadWeapon(fo::GameObject* critter, fo::GameObject* weapon, fo::GameObject* ammo) {
	if (!weapon) return false;

	long reloadCost = game::Items::item_weapon_mp_cost(critter, weapon, fo::AttackType::ATKTYPE_RWEAPON_RELOAD, 0);
	if (reloadCost > critter->critter.getAP()) return false;

	bool reload = (weapon->protoId == fo::ProtoID::PID_SOLAR_SCORCHER);;

	if (!ammo && weapon->item.ammoPid != -1 || reload) {
		fo::Proto* proto = fo::GetProto(weapon->protoId);
		if (proto->item.type == fo::ItemType::item_type_weapon) {
			if (weapon->item.charges <= 0 || weapon->item.charges < (proto->item.weapon.maxAmmo / 2)) {
				if (!reload) ammo = GetInventAmmo(critter, weapon);
			} else {
				reload = false;
			}
		}
	}
	if (reload || ammo) {
		long result = fo::func::item_w_reload(weapon, ammo);
		if (result != -1) {
			if (!result && ammo) fo::func::obj_destroy(ammo);

			//long volume = fo::func::gsound_compute_relative_volume(critter);
			//const char* sfxName = fo::func::gsnd_build_weapon_sfx_name(0, weapon, fo::AttackType::ATKTYPE_RWEAPON_RELOAD, 0);
			//fo::func::gsound_play_sfx_file_volume(sfxName, volume);

			fo::func::ai_magic_hands(critter, weapon, 5002);

			critter->critter.movePoints -= reloadCost;
			if (critter->critter.getAP() < 0) critter->critter.movePoints = 0;
			return true;
		}
	}
	return false;
}

/////////////////////////////////////////////////////////////////////////////////////////

static fo::GameObject* itemCorpse;

static long __fastcall pickup_item(fo::GameObject* source, fo::GameObject* item) {
	itemCorpse = nullptr;
	long result = fo::func::item_move_force(item->owner, source, item, 1);
	if (result) fo::func::debug_printf("\n[AI] Error pickup item (%s).", fo::func::critter_name(item));
	return result;
}

static void __declspec(naked) pickup_item_fowrap() {
	__asm {
		push ecx
		mov  ecx, eax;
		call pickup_item;
		pop  ecx;
		retn;
	}
}

static long __fastcall check_object_ap(fo::GameObject* source, fo::GameObject* target) {
	return (source->critter.getAP() >= pickupCostAP && fo::func::obj_dist(source, target) <= 1) ? 0 : -1;
}

static void __declspec(naked) check_object_ap_fowrap() {
	__asm {
		push ecx
		mov  ecx, eax;
		call check_object_ap;
		pop  ecx;
		retn;
	}
}

fo::GameObject* AIInventory::AIRetrieveCorpseItem(fo::GameObject* source, fo::GameObject* itemRetrive) {
	if (itemRetrive) itemCorpse = itemRetrive;
	fo::GameObject* object = itemCorpse->owner;

	DEV_PRINTF2("\n[AI] Try RetrieveCorpseItem: %s, Owner: %s", fo::func::critter_name(itemCorpse), fo::func::critter_name(object));

	fo::func::register_begin(fo::AnimCommand::RB_RESERVED);
	fo::func::register_object_move_to_object(source, object, source->critter.getAP(), 0);
	fo::func::register_object_call((long*)source, (long*)object, check_object_ap_fowrap, -1);
	fo::func::register_object_animate(source, fo::Animation::ANIM_magic_hands_ground, 0);

	long artID = fo::func::art_id(fo::ObjType::OBJ_TYPE_CRITTER, source->artFid & 0xFFF, 10, (source->artFid & 0xF000) >> 12, source->frm + 1);

	long delay = -1;
	DWORD frmPtr;
	auto frm = fo::func::art_ptr_lock(artID, &frmPtr);
	if (frm) {
		//delay = fo::func::art_frame_action_frame(frm);
		__asm {
			mov  eax, frm;
			call fo::funcoffs::art_frame_action_frame_;
			mov  delay, eax;
		}
		fo::func::art_ptr_unlock(frmPtr);
	}

	char nameArt[16];
	if (!fo::func::art_get_base_name(fo::ObjType::OBJ_TYPE_ITEM, itemCorpse->artFid & 0xFFF, nameArt)) {
		if (std::strlen(nameArt) >= 4) {
			fo::func::register_object_play_sfx(itemCorpse, nameArt, delay);
			delay = 0;
		}
	}

	fo::func::register_object_call((long*)source, (long*)itemCorpse, pickup_item_fowrap, delay);

	if (!fo::func::register_end()) {
		DEV_PRINTF("\n[AI] register_end OK");
		fo::GameObject* item = itemCorpse;

		__asm call fo::funcoffs::combat_turn_run_;

		source->critter.movePoints -= pickupCostAP;
		if (source->critter.getAP() < 0) source->critter.movePoints = 0;

		if (fo::GetInventItem(source, item->protoId) == item) {
			DEV_PRINTF1("\n[AI] OK RetrieveCorpseItem: %s", fo::func::critter_name(item));
			//fo::func::combatAIInfoSetLastItem(source, corpse); // устанавливaем 0, если предмет был подобран (функция не работает и неиспользуется движком в должном виде)
			return item;
		}
	}
	DEV_PRINTF("\n[AI] Error RetrieveCorpseItem!");
	return nullptr;
}

// Ищет патроны на карте подходящих к указанному оружию
fo::GameObject* AIInventory::ai_search_environ_ammo(fo::GameObject* critter, fo::GameObject* weapon) {
	if (!weapon) {
		weapon = fo::func::inven_right_hand(critter);
		if (!weapon) return nullptr; // ERROR: не назначено или нет оружия для проверки
	}

	fo::GameObject* ammo = nullptr;

	long* objectsList = nullptr;
	long numObjects = fo::func::obj_create_list(-1, critter->elevation, fo::ObjType::OBJ_TYPE_ITEM, &objectsList);
	if (numObjects > 0) {
		fo::var::combat_obj = critter;
		fo::func::qsort(objectsList, numObjects, 4, fo::funcoffs::compare_nearer_);

		long maxDist = fo::func::stat_level(critter, fo::STAT_pe) + 5;

		for (int i = 0; i < numObjects; i++)
		{
			fo::GameObject* item = (fo::GameObject*)objectsList[i];

			if (fo::func::obj_dist(critter, item) > maxDist) break;
			if (fo::GetItemType(item) != fo::ItemType::item_type_ammo) continue;

			if (fo::func::item_w_can_reload(weapon, item)) {
				ammo = item;
				break;
			};
		}
		fo::func::obj_delete_list(objectsList);
	}
	return ammo;
}

/////////////////////////////////////////////////////////////////////////////////////////

// Аналог функции ai_search_environ, только с той разницей, что ищет требуемый предмет в инвентаре убитых криттеров
fo::GameObject* AIInventory::ai_search_environ_corpse(fo::GameObject* source, long itemType, fo::GameObject* itemGround, fo::GameObject* weapon) {
	long* objectsList = nullptr;

	long numObjects = fo::func::obj_create_list(-1, source->elevation, fo::ObjType::OBJ_TYPE_CRITTER, &objectsList);
	if (numObjects > 0) {
		fo::var::combat_obj = source;
		fo::func::qsort(objectsList, numObjects, 4, fo::funcoffs::compare_nearer_);

		long maxDist = fo::func::stat_level(source, fo::Stat::STAT_pe) + 5;
		if (!weapon && itemType == fo::ItemType::item_type_ammo) {
			weapon = fo::func::inven_right_hand(source);
			if (!weapon) return itemGround; // ERROR: не назначено или нет оружия для проверки
		}

		for (int i = 0; i < numObjects; i++)
		{
			fo::GameObject* object = (fo::GameObject*)objectsList[i];
			if (object->critter.IsNotDead() || object->invenSize <= 0) continue;

			if (fo::func::obj_dist(source, object) > maxDist) break;

			fo::GameObject* item = AIInventory::SearchInventoryItemType(source, itemType, object, weapon);
			itemCorpse = item;
			if (item) {
				DEV_PRINTF2("\n[AI] ai_search_environ_corpse: %s, Owner: %s", fo::func::critter_name(item), fo::func::critter_name(item->owner));
				item->owner = object;
				break;
			}
		}
		fo::func::obj_delete_list(objectsList);
	}
	if (itemCorpse) {
		if (itemGround) {
			// check real path distance
			int toDist1 = fo::func::make_path_func(source, source->tile, itemGround->tile, 0, 0, (void*)fo::funcoffs::obj_blocking_at_);
			int toDist2 = fo::func::make_path_func(source, source->tile, itemCorpse->owner->tile, 0, 0, (void*)fo::funcoffs::obj_blocking_at_);
			if (toDist1 < toDist2) {
				itemCorpse = nullptr;
				return itemGround;
			}
		}
		return itemCorpse->owner; // critter
	}
	return itemGround;
}

static long __fastcall AISearchCorpseWeapon(fo::GameObject* target, fo::GameObject* source, fo::GameObject* &weapon, long &hitMode, fo::GameObject* itemEnv) {
	fo::GameObject* object = AIInventory::ai_search_environ_corpse(source, fo::ItemType::item_type_weapon, itemEnv, weapon);
	if (!object || !itemCorpse) return 1; // default

	fo::GameObject* item = AIInventory::AIRetrieveCorpseItem(source, nullptr);
	if (item) {
		fo::func::inven_wield(source, item, fo::InvenType::INVEN_TYPE_RIGHT_HAND);

		__asm call fo::funcoffs::combat_turn_run_;

		weapon = item;
		hitMode = fo::func::ai_pick_hit_mode(source, item, target);

		if (game::Items::item_w_mp_cost(source, hitMode, 0) <= source->critter.getAP()) return 0; // Ok
	}
	return -1; // error (exit ai_try_attack_)
}

static void __declspec(naked) ai_switch_weapons_hook_search() {
	__asm {
		call fo::funcoffs::ai_search_environ_;
		mov  ebx, eax;
		push ecx;
		push eax;      // item
		push ebp;      // hit_mode ref
		push edi;      // weapon ref
		mov  edx, esi; // source
		call AISearchCorpseWeapon;
		pop  ecx;
		test eax, eax;
		jg   default; // > 0
		add  esp, 4;
		pop  ebp;
		pop  edi;
		pop  esi;
		retn;
default:
		mov  eax, ebx;
		retn;
	}
}

static long __fastcall AISearchCorpseAmmo(fo::GameObject* source, fo::GameObject* weapon, fo::GameObject* itemEnv) {
	fo::GameObject* object = AIInventory::ai_search_environ_corpse(source, fo::ItemType::item_type_ammo, itemEnv, weapon);
	if (!object || !itemCorpse) return 0; // default

	fo::GameObject* item = AIInventory::AIRetrieveCorpseItem(source, nullptr);
	if (item) {
		return (AIInventory::AITryReloadWeapon(source, weapon, item)) ? 1 : -1; // 1 - Ok
	}
	return -1; // error
}

static void __declspec(naked) ai_try_attack_hook_search_ammo() {
	static const uint32_t ai_try_attack_hack_GoNext_Ret = 0x42A9F2;
	__asm {
		call fo::funcoffs::ai_search_environ_;
		mov  ebx, eax;
		push eax;      // itemEnv
		mov  edx, [esp + 0x364 - 0x3C + 4]; // weapon
		mov  ecx, esi; // source
		call AISearchCorpseAmmo;
		test eax, eax;
		jz   default;
		add  esp, 4;
		jmp  ai_try_attack_hack_GoNext_Ret;
default:
		mov  eax, ebx;
		retn;
	}
}

static long __fastcall AISearchCorpseDrug(fo::GameObject* source, long addrType, fo::GameObject* &itemEnv) {
	fo::ItemType type = (addrType == 0x4287B2 + 5) ? fo::ItemType::item_type_drug : fo::ItemType::item_type_misc_item;
	fo::GameObject* object = AIInventory::ai_search_environ_corpse(source, type, itemEnv, 0);
	if (!object || !itemCorpse) return 0; // default (в itemEnv ref значение из ai_search_environ_)

	fo::GameObject* item = AIInventory::AIRetrieveCorpseItem(source, nullptr);
	itemEnv = item;
	return (item) ? 1 : -1;
}

static void __declspec(naked) ai_check_drugs_hook_search_drug() {
	static const uint32_t ai_check_drugs_hook_search_drug_UseRet = 0x4287DE;
	__asm {
		call fo::funcoffs::ai_search_environ_;
		mov  edx, [esp]; // called addr
		push eax;        // item
		push esp;        // ref to item
		mov  ecx, esi;   // source
		call AISearchCorpseDrug;
		mov  ebp, [esp];
		test eax, eax;
		jz   default;
		add  esp, 8;
		jmp  ai_check_drugs_hook_search_drug_UseRet;
default:
		mov  eax, ebp;
		add  esp, 4;
		retn;
	}
}

void AIInventory::CorpsesLootingHack() {
	HookCall(0x42A5F6, ai_switch_weapons_hook_search);
	HookCall(0x42AA25, ai_try_attack_hook_search_ammo);
	HookCalls(ai_check_drugs_hook_search_drug, { 0x4287B2, 0x4287C8 });
}

}