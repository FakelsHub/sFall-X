/*
 *    sfall
 *    Copyright (C) 2020  The sfall team
 *
 */

#include "..\..\main.h"
#include "..\..\FalloutEngine\Fallout2.h"

//#include "..\HookScripts\MiscHS.h"
#include "..\HookScripts\CombatHS.h"

#include "..\AI.h"

#include "AI.Behavior.h"

namespace sfall
{

using namespace fo;
using namespace Fields;

#ifndef NDEBUG
#define DEV_PRINTF(info)			fo::func::debug_printf(info)
#define DEV_PRINTF1(info, a)		fo::func::debug_printf(info, a)
#define DEV_PRINTF2(info, a, b)		fo::func::debug_printf(info, a, b)
#else
#define DEV_PRINTF(info)
#define DEV_PRINTF1(info, a)
#define DEV_PRINTF2(info, a, b)
#endif

static void __declspec(naked) ai_search_environ_hook() {
	static const DWORD ai_search_environ_ret = 0x429D3E;
	__asm {
		call fo::funcoffs::obj_dist_;
		cmp  [esp + 0x28 + 0x1C + 4], item_type_ammo;
		je   end;
		//
		push edx;
		push eax;
		mov  edx, STAT_max_move_points;
		mov  eax, esi;
		call fo::funcoffs::stat_level_;
		mov  edx, [esi + movePoints];    // source current ap
		cmp  edx, eax;                   // npc already used their ap?
		pop  eax;
		jge  skip;                       // yes
		// distance & AP check
		sub  edx, 3;                     // pickup cost ap
		cmp  edx, eax;                   // eax - distance to the object
		jl   continue;
skip:
		pop  edx;
end:
		retn;
continue:
		pop  edx;
		add  esp, 4;                     // destroy return
		jmp  ai_search_environ_ret;      // next object
	}
}

static long __fastcall sf_ai_check_weapon_switch(fo::GameObject* target, long &hitMode, fo::GameObject* source, fo::GameObject* weapon) {
	if (source->critter.movePoints <= 0) return -1;
	if (!weapon) return 1; // no weapon in hand slot

	long _hitMode;
	if ((_hitMode = fo::func::ai_pick_hit_mode(source, weapon, target)) != hitMode) {
		hitMode = _hitMode;
		return 0; // сменили режим
	}

	fo::GameObject* item = fo::func::ai_search_inven_weap(source, 1, target);
	if (!item) return 1; // no weapon in inventory, true to allow the to search continue weapon on the map

	long wType = fo::func::item_w_subtype(item, AttackType::ATKTYPE_RWEAPON_PRIMARY);
	if (wType <= AttackSubType::MELEE) { // unarmed and melee weapon, check the distance before switching
		if (fo::func::obj_dist(source, target) > 2) return -1;
	}
	return 1;
}

static void __declspec(naked) ai_try_attack_hook_switch_fix() {
	__asm {
		push edx;
		push [ebx];//push dword ptr [esp + 0x364 - 0x3C + 8]; // weapon
		push esi;                                // source
		call sf_ai_check_weapon_switch;          // edx - hit mode
		pop  edx;
		test eax, eax;
		jle  noSwitch; // <= 0
		mov  ecx, ebp;
		mov  eax, esi;
		jmp  fo::funcoffs::ai_switch_weapons_;
noSwitch:
		retn; // -1 - for exit from ai_try_attack_
	}
}

/////////////////////////////////////////////////////////////////////////////////////////

static long CombatMoveToTile(fo::GameObject* source, long tile, long dist) {
	fo::func::register_begin(fo::RB_RESERVED);
	fo::func::register_object_move_to_tile(source, tile, source->elevation, dist, -1);
	long result = fo::func::register_end();
	if (!result) {
		__asm call fo::funcoffs::combat_turn_run_;
		if (source->critter.damageFlags & (fo::DamageFlag::DAM_DEAD | fo::DamageFlag::DAM_KNOCKED_OUT | fo::DamageFlag::DAM_LOSE_TURN)) {
			source->critter.movePoints = 0; // для предотвращения дальнейших действий в бою
		}
	}
	return result; // 0 - ok, -1 - error
}

static bool sf_critter_have_ammo(fo::GameObject* critter, fo::GameObject* weapon) {
	DWORD slotNum = -1;
	while (true) {
		fo::GameObject* ammo = fo::func::inven_find_type(critter, fo::item_type_ammo, &slotNum);
		if (!ammo) break;
		if (fo::func::item_w_can_reload(weapon, ammo)) return true;
	}
	return false;
}

static uint32_t __fastcall sf_check_ammo(fo::GameObject* weapon, fo::GameObject* critter) {
	if (sf_critter_have_ammo(critter, weapon)) return 1;

	// check on ground
	uint32_t result = 0;
	long maxDist = fo::func::stat_level(critter, STAT_pe) + 5;
	long* objectsList = nullptr;
	long numObjects = fo::func::obj_create_list(-1, critter->elevation, fo::ObjType::OBJ_TYPE_ITEM, &objectsList);
	if (numObjects > 0) {
		fo::var::combat_obj = critter;
		fo::func::qsort(objectsList, numObjects, 4, fo::funcoffs::compare_nearer_);
		for (int i = 0; i < numObjects; i++)
		{
			fo::GameObject* itemGround = (fo::GameObject*)objectsList[i];
			if (fo::func::item_get_type(itemGround) == fo::item_type_ammo) {
				if (fo::func::obj_dist(critter, itemGround) > maxDist) break;
				if (fo::func::item_w_can_reload(weapon, itemGround)) {
					result = 1;
					break;
				}
			}
		}
		fo::func::obj_delete_list(objectsList);
	}
	return result; // 0 - no have ammo
}

static void __declspec(naked) ai_search_environ_hook_weapon() {
	__asm {
		call fo::funcoffs::ai_can_use_weapon_;
		test eax, eax;
		jnz  checkAmmo;
		retn;
checkAmmo:
		mov  edx, [esp + 4]; // base
		mov  eax, [edx + ecx];
		cmp  dword ptr [eax + charges], 0; // ammo count
		jnz  end;
		push ecx;
		mov  ecx, eax;       // weapon
		mov  edx, esi;       // source
		call sf_check_ammo;
		pop  ecx;
end:
		retn;
	}
}
/*
static fo::GameObject* sf_check_block_line_of_fire(fo::GameObject* source, long destTile) {
	fo::GameObject* object = nullptr; // check the line of fire from target to checkTile

	fo::func::make_straight_path_func(source, source->tile, destTile, 0, (DWORD*)&object, 32, (void*)fo::funcoffs::obj_shoot_blocking_at_);
	if (object) {
		//if (object->tile == destTile) return nullptr; // объект расположен на гексе назначения, это может быть дверь или другой проходимый объект
		if (object->TypeFid() == fo::OBJ_TYPE_CRITTER) object = sf_check_block_line_of_fire(object, destTile);
	}
	return object;
}
*/
// Функция попытается найти свободный гекс для совершения выстрела AI по цели, в случаях когда цель для AI заблокирована для выстрела каким либо объектом
// если таковой гекс не будет найден то выполнится действия по умолчанию в функции ai_move_steps_closer_
// TODO: Необходимо улучшить алгоритм для поиска гекса для совершения выстрела, для снайперов должна быть применена другая тактика
// Добавить учет открывание дверь при построении пути.
static int32_t __fastcall sf_ai_move_steps_tile(fo::GameObject* source, fo::GameObject* target, int32_t &hitMode) {
	long distance, shotTile = 0;

	fo::GameObject* itemHand = fo::func::inven_right_hand(source);
	if (!itemHand || fo::func::item_w_subtype(itemHand, hitMode) <= fo::MELEE) {
		return 0;
	}

	long ap = source->critter.movePoints;
	long cost = sf_item_w_mp_cost(source, hitMode, 0);
	ap -= cost; // left ap for distance move
	if (ap <= 0) return 0;

	char rotationData[800];
	long pathLength = fo::func::make_path_func(source, source->tile, target->tile, rotationData, 0, (void*)fo::funcoffs::obj_blocking_at_);
	if (pathLength > ap) pathLength = ap;

	long checkTile = source->tile;
	for (int i = 0; i < pathLength; i++)
	{
		checkTile = fo::func::tile_num_in_direction(checkTile, rotationData[i], 1);
		DEV_PRINTF1("\n[AI] sf_ai_move_steps_tile: path tile %d", checkTile);

		fo::GameObject* object = nullptr; // check the line of fire from target to checkTile
		fo::func::make_straight_path_func(target, target->tile, checkTile, 0, (DWORD*)&object, 32, (void*)fo::funcoffs::obj_shoot_blocking_at_);

		DEV_PRINTF1("\n[AI] Object Type on fire line: %d", (object) ? object->Type() : -1);

		object = AI::CheckShootAndTeamCritterOnLineOfFire(object, checkTile, source->critter.teamNum);
		if (!object) { // if there are no friendly critters
			shotTile = checkTile;
			distance = i + 1;
			DEV_PRINTF2("\n[AI] Get shot tile:%d, dist:%d", checkTile, distance);
			break;
		}
		// запоминаем первый простреливаемый гекс, с которого имеются дружественные NPC на линии огня (будем проверять следующие гексы)
		if (!shotTile) {                                       // проверяем простреливается ли путь за критером
			if (object->TypeFid() != ObjType::OBJ_TYPE_CRITTER || fo::func::combat_is_shot_blocked(object, object->tile, checkTile, 0, 0)) continue;
			shotTile = checkTile;
			distance = i + 1;
			DEV_PRINTF2("\n[AI] Get friendly fire shot tile:%d, Dist:%d", checkTile, distance);
		}
	}
	if (shotTile && ap > distance) {
		fo::AIcap* cap = fo::func::ai_cap(source);
		if (cap->distance != AIpref::distance::snipe) { // оставляем AP для поведения "Snipe"
			long leftAP = (ap - distance) % cost;       // оставшиеся свободные AP после подхода и выстрела
			// spend left APs
			long newTile = checkTile = shotTile;
			long dist;
			for (int i = distance; i < pathLength; i++) // начинаем со следующего тайла и идем до конца пути
			{
				checkTile = fo::func::tile_num_in_direction(checkTile, rotationData[i], 1);
				DEV_PRINTF1("\n[AI] sf_ai_move_steps_tile: path extra tile %d", checkTile);

				fo::GameObject* object = nullptr; // check the line of fire from target to checkTile
				fo::func::make_straight_path_func(target, target->tile, checkTile, 0, (DWORD*)&object, 32, (void*)fo::funcoffs::obj_shoot_blocking_at_);
				if (!AI::CheckShootAndTeamCritterOnLineOfFire(object, checkTile, source->critter.teamNum)) { // if there are no friendly critters
					newTile = checkTile;
					dist = i;
				}
				if (!--leftAP) break;
			}
			if (newTile != shotTile) {
				distance = dist + 1;
				shotTile = newTile;
				DEV_PRINTF2("\n[AI] Get extra tile:%d, dist:%d", shotTile, distance);
			}
		}
	}
	if (shotTile && isDebug) fo::func::debug_printf("\n[AI] %s: Move to tile for shot.", fo::func::critter_name(source));

	int result = (shotTile && CombatMoveToTile(source, shotTile, distance) == 0) ? 1 : 0;
	if (result) hitMode = fo::func::ai_pick_hit_mode(source, itemHand, target); // try pick new weapon mode after step move

	return result;
}

static void __declspec(naked) ai_try_attack_hook_shot_blocked() {
	__asm {
		pushadc;
		mov  ecx, eax;                      // source
		lea  eax, [esp + 0x364 - 0x38 + 12 + 4];
		push eax;                           // hit mode
		call sf_ai_move_steps_tile;         // edx - target
		test eax, eax;
		jz   defaultMove;
		lea  esp, [esp + 12];
		retn;
defaultMove:
		popadc;
		jmp  fo::funcoffs::ai_move_steps_closer_;
	}
}

/////////////////////////////////////////////////////////////////////////////////////////

static fo::GameObject* __stdcall sf_ai_search_weapon_environ(fo::GameObject* source, fo::GameObject* item, fo::GameObject* target) {
	long* objectsList = nullptr;

	long numObjects = fo::func::obj_create_list(-1, source->elevation, fo::ObjType::OBJ_TYPE_ITEM, &objectsList);
	if (numObjects > 0) {
		fo::var::combat_obj = source;
		fo::func::qsort(objectsList, numObjects, 4, fo::funcoffs::compare_nearer_);

		for (int i = 0; i < numObjects; i++)
		{
			fo::GameObject* itemGround = (fo::GameObject*)objectsList[i];
			if (item && item->protoId == itemGround->protoId) continue;

			if (fo::func::item_get_type(itemGround) == fo::item_type_weapon) {
				if (fo::func::obj_dist(source, itemGround) > source->critter.movePoints + 1) break;
				// check real path distance
				int toDistObject = fo::func::make_path_func(source, source->tile, itemGround->tile, 0, 0, (void*)fo::funcoffs::obj_blocking_at_);
				if (toDistObject > source->critter.movePoints + 1) continue;

				if (fo::func::ai_can_use_weapon(source, itemGround, fo::AttackType::ATKTYPE_RWEAPON_PRIMARY)) {
					if (fo::func::ai_best_weapon(source, item, itemGround, target) == itemGround) {
						item = itemGround;
					}
				}
			}
		}
		fo::func::obj_delete_list(objectsList);
	}
	return item;
}

static fo::GameObject* sf_ai_skill_weapon(fo::GameObject* source, fo::GameObject* hWeapon, fo::GameObject* sWeapon) {
	if (!hWeapon) return sWeapon;
	if (!sWeapon) return hWeapon;

	int hSkill = fo::func::item_w_skill(hWeapon, fo::AttackType::ATKTYPE_RWEAPON_PRIMARY);
	int sSkill = fo::func::item_w_skill(sWeapon, fo::AttackType::ATKTYPE_RWEAPON_PRIMARY);

	if (hSkill == sSkill) return sWeapon;

	int hLevel = fo::func::skill_level(source, hSkill);
	int sLevel = fo::func::skill_level(source, sSkill) + 10;

	return (hLevel > sLevel) ? hWeapon : sWeapon;
}

static bool LookupOnGround = false;

// Атакующий попытается найти лучшее оружие в своем инвентаре перед совершением атаки или подобрать близлежащее на земле оружие
// Executed once when the NPC starts attacking
static int32_t __fastcall sf_ai_search_weapon(fo::GameObject* source, fo::GameObject* target, fo::GameObject* &weapon, uint32_t &hitMode) {

	fo::GameObject* itemHand   = fo::func::inven_right_hand(source); // current item
	fo::GameObject* bestWeapon = itemHand;

	DEV_PRINTF1("\n[AI] HandPid: %d", (itemHand) ? itemHand->protoId : -1);

	DWORD slotNum = -1;
	while (true)
	{
		fo::GameObject* item = fo::func::inven_find_type(source, fo::item_type_weapon, &slotNum);
		if (!item) break;
		if (itemHand && itemHand->protoId == item->protoId) continue;

		if ((source->critter.movePoints >= fo::func::item_w_primary_mp_cost(item)) &&
			fo::func::ai_can_use_weapon(source, item, AttackType::ATKTYPE_RWEAPON_PRIMARY))
		{
			if (item->item.ammoPid == -1 || fo::func::item_w_subtype(item, AttackType::ATKTYPE_RWEAPON_PRIMARY) == fo::AttackSubType::THROWING ||
				(fo::func::item_w_curr_ammo(item) || sf_critter_have_ammo(source, item)))
			{
				if (!fo::func::combat_safety_invalidate_weapon_func(source, item, AttackType::ATKTYPE_RWEAPON_PRIMARY, target, 0, 0)) { // weapon safety
					bestWeapon = fo::func::ai_best_weapon(source, bestWeapon, item, target);
				}
			}
		}
	}
	DEV_PRINTF1("\n[AI] BestWeaponPid: %d", (bestWeapon) ? bestWeapon->protoId : -1);

	// выбрать лучшее на основе навыка
	if (itemHand != bestWeapon)	bestWeapon = sf_ai_skill_weapon(source, itemHand, bestWeapon);

	if ((LookupOnGround && !fo::func::critterIsOverloaded(source)) && source->critter.movePoints >= 3 && fo::func::critter_body_type(source) == BodyType::Biped) {

		// построить путь до цели (зачем?)
		int toDistTarget = fo::func::make_path_func(source, source->tile, target->tile, 0, 0, (void*)fo::funcoffs::obj_blocking_at_);
		if ((source->critter.movePoints - 3) >= toDistTarget) goto notRetrieve; // не поднимать, если у NPC хватает очков сделать удар по цели

		fo::GameObject* itemGround = sf_ai_search_weapon_environ(source, bestWeapon, target);

		DEV_PRINTF1("\n[AI] OnGroundPid: %d", (itemGround) ? itemGround->protoId : -1);

		if (itemGround != bestWeapon) {
			if (itemGround && (!bestWeapon || itemGround->protoId != bestWeapon->protoId)) {

				if (bestWeapon && fo::func::item_cost(itemGround) < fo::func::item_cost(bestWeapon) + 50) goto notRetrieve;

				fo::GameObject* item = sf_ai_skill_weapon(source, bestWeapon, itemGround);
				if (item != itemGround) goto notRetrieve;

				fo::func::dev_printf("\n[AI] TryRetrievePid: %d AP: %d", itemGround->protoId, source->critter.movePoints);

				fo::GameObject* itemRetrieve = fo::func::ai_retrieve_object(source, itemGround); // pickup item

				DEV_PRINTF2("\n[AI] PickupPid: %d AP: %d", (itemRetrieve) ? itemRetrieve->protoId : -1, source->critter.movePoints);

				if (itemRetrieve && itemRetrieve->protoId == itemGround->protoId) {
					// if there is not enough action points to use the weapon, then just pick up this item
					bestWeapon = (source->critter.movePoints >= fo::func::item_w_primary_mp_cost(itemRetrieve)) ? itemRetrieve : nullptr;
				}
			}
		}
	}
notRetrieve:
	DEV_PRINTF2("\n[AI] BestWeaponPid: %d AP: %d", ((bestWeapon) ? bestWeapon->protoId : 0), source->critter.movePoints);

	int32_t _hitMode = -1;
	if (bestWeapon && (!itemHand || itemHand->protoId != bestWeapon->protoId)) {
		weapon = bestWeapon;
		hitMode = _hitMode = fo::func::ai_pick_hit_mode(source, bestWeapon, target);
		fo::func::inven_wield(source, bestWeapon, fo::InvenType::INVEN_TYPE_RIGHT_HAND);
		__asm call fo::funcoffs::combat_turn_run_;
		if (isDebug) fo::func::debug_printf("\n[AI] Wield best weapon pid: %d AP: %d", bestWeapon->protoId, source->critter.movePoints);
	}
	return _hitMode;
}

static bool weaponIsSwitch = 0;

static void __declspec(naked) ai_try_attack_hook() {
	__asm {
		test edi, edi;                        // first attack loop ?
		jnz  end;
		test weaponIsSwitch, 1;               // оружие уже было сменено кодом в движке (ai_try_attack_hook_switch)
		jnz  end;
		cmp  [esp + 0x364 - 0x44 + 4], 0;     // check safety_range
		jnz  end;
		//
		lea  eax, [esp + 0x364 - 0x38 + 4];   // hit_mode
		push eax;
		lea  eax, [esp + 0x364 - 0x3C + 8];   // right_weapon
		push eax;
		mov  ecx, esi;                        // source
		call sf_ai_search_weapon;             // edx - target
		test eax, eax;
		cmovge ebx, eax;                      // >= 0
		// restore value reg.
		mov  eax, esi;
		mov  edx, ebp;
		xor  ecx, ecx;
end:
		mov  weaponIsSwitch, 0;
		jmp  fo::funcoffs::combat_check_bad_shot_;
		// TODO: Сделать поиск новой цели если combat_check_bad_shot_ возвращает результат 4 (когда криттер умирает в цикле хода)
	}
}

static void __declspec(naked) ai_try_attack_hook_switch() {
	__asm {
		mov weaponIsSwitch, 1;
		jmp fo::funcoffs::ai_switch_weapons_;
	}
}

// Анализирует ситуацию для текущей выбранной цели атакующего, и если ситуация неблагоприятная для атакующего
// то будет совершена попытка сменить текущую цель на альтернативную. Поиск цели осуществляется в коде движка
static bool __fastcall sf_ai_check_target(fo::GameObject* source, fo::GameObject* target) {

	int distance = fo::func::obj_dist(source, target);
	if (distance <= 1) return false;

	bool shotIsBlock = fo::func::combat_is_shot_blocked(source, source->tile, target->tile, target, 0);

	int pathToTarget = fo::func::make_path_func(source, source->tile, target->tile, 0, 0, (void*)fo::funcoffs::obj_blocking_at_);
	if (shotIsBlock && pathToTarget == 0) { // shot and move block to target
		return true;                        // picking alternate target
	}

	fo::AIcap* cap = fo::func::ai_cap(source);
	if (shotIsBlock && pathToTarget > 1) { // shot block to target, can move
		long dist_disposition = distance;
		switch (cap->disposition) {
		case AIpref::defensive:
			pathToTarget += 5;
			dist_disposition += 5;
			break;
		case AIpref::aggressive: // ai aggressive never does not change its target if the move-path to the target is not blocked
			pathToTarget = 1;
			break;
		case AIpref::berserk:
			pathToTarget /= 2;
			dist_disposition -= 5;
			break;
		}
		if (dist_disposition >= 10 && pathToTarget > (source->critter.movePoints * 2)) {
			return true; // target is far -> picking alternate target
		}
	}
	else if (shotIsBlock == false) { // can shot to target
		fo::GameObject* itemHand = fo::func::inven_right_hand(source); // current item
		if (!itemHand && pathToTarget == 0) return true; // no item and move block to target -> picking alternate target
		if (!itemHand) return false;

		fo::Proto* proto = GetProto(itemHand->protoId);
		if (proto && proto->item.type == ItemType::item_type_weapon) {
			int hitMode = fo::func::ai_pick_hit_mode(source, itemHand, target);
			int maxRange = fo::func::item_w_range(source, hitMode);
			int diff = distance - maxRange;
			if (diff > 0) { // shot out of range (положительное число не хватает дистанции для оружия)
				/*if (!pathToTarget) return true; // move block to target and shot out of range -> picking alternate target (это больше не нужно т.к. есть твик к подходу)*/
				if (cap->disposition == AIpref::coward && diff > fo::func::roll_random(8, 12)) return true;

				long cost = sf_item_w_mp_cost(source, hitMode, 0);
				if (diff > (source->critter.movePoints - cost)) return true; // не хватит очков действия для подхода и выстрела -> picking alternate target
			}
		} // can shot or move, and item not weapon
	} // can shot and move / can move and block shot / can shot and block move
	return false;
}

static const char* reTargetMsg = "\n[AI] I can't get at my target. Try picking alternate.";

static void __declspec(naked) ai_danger_source_hack_find() {
	static const uint32_t ai_danger_source_hack_find_Pick = 0x42908C;
	static const uint32_t ai_danger_source_hack_find_Ret  = 0x4290BB;
	__asm {
		push eax;
		push edx;
		mov  edx, eax; // source.who_hit_me target
		mov  ecx, esi; // source
		call sf_ai_check_target;
		pop  edx;
		test al, al;
		pop  eax;
		jnz  reTarget;
		add  esp, 0x1C;
		pop  ebp;
		pop  edi;
		jmp  ai_danger_source_hack_find_Ret;
reTarget:
		push reTargetMsg;
		call fo::funcoffs::debug_printf_;
		add  esp, 4;
		jmp  ai_danger_source_hack_find_Pick;
	}
}

static unsigned long GetTargetDistance(fo::GameObject* source, fo::GameObject* &target) {
	unsigned long distanceLast = -1; // inactive

	fo::GameObject* lastAttacker = AI::AIGetLastAttacker(source);
	if (lastAttacker && lastAttacker != target && !(lastAttacker->critter.damageFlags & (DamageFlag::DAM_DEAD | DamageFlag::DAM_KNOCKED_OUT | DamageFlag::DAM_LOSE_TURN))) {
		distanceLast = fo::func::obj_dist(source, lastAttacker);
	}
	// target is active?
	unsigned long distance = (!(target->critter.damageFlags & (DamageFlag::DAM_DEAD | DamageFlag::DAM_KNOCKED_OUT | DamageFlag::DAM_LOSE_TURN)))
							 ? fo::func::obj_dist(source, target)
							 : -1; // inactive

	if (distance >= 3 && distanceLast >= 3) return -1;   // also distances == -1
	if (distance >= distanceLast) target = lastAttacker; // replace target (attacker critter has priority)

	return (distance >= distanceLast) ? distanceLast : distance;
}

// Функция анализирует используемое оружие у цели, и если цель использует оружие ближнего действия то атакующий AI
// по завершению хода попытается отойти от атакующей его цели на небольшое расстояние
// Executed after the NPC attack
static void __fastcall sf_ai_move_away_from_target(fo::GameObject* source, fo::GameObject* target, fo::GameObject* sWeapon, long hit) {
	///if (target->critter.damageFlags & fo::DamageFlag::DAM_DEAD || target->critter.health <= 0) return;

	fo::AIcap* cap = fo::func::ai_cap(source);
	if (cap->disposition == AIpref::disposition::berserk ||
		///cap->distance == AIpref::distance::stay || // stay в ai_move_away запрещает движение
		cap->distance == AIpref::distance::charge) // charge в движке используется для сближения с целью
	{
		return;
	}

	if (fo::GetCritterKillType(source) > KillType::KILL_TYPE_women) return; // critter is not men & women

	unsigned long distance = source->critter.movePoints; // coward: отойдет на максимально возможную дистанцию

	if (cap->disposition != AIpref::disposition::coward) {
		if (distance >= 3) return; // source still has a lot of action points

		long wTypeR = fo::func::item_w_subtype(sWeapon, hit);
		if (wTypeR <= AttackSubType::MELEE) return; // source has a melee weapon or unarmed

		if ((distance = GetTargetDistance(source, target)) > 2) return;

		fo::Proto* protoR = nullptr;
		fo::Proto* protoL = nullptr;
		AttackSubType wTypeRs = AttackSubType::NONE;
		AttackSubType wTypeL  = AttackSubType::NONE;
		AttackSubType wTypeLs = AttackSubType::NONE;

		fo::GameObject* itemHandR = fo::func::inven_right_hand(target);
		if (!itemHandR && target != fo::var::obj_dude) { // target is unarmed
			long damage = fo::func::stat_level(target, Stat::STAT_melee_dmg);
			if (damage * 2 < source->critter.health / 2) return;
			goto moveAway;
		}
		if (itemHandR) {
			protoR = fo::GetProto(itemHandR->protoId);
			long weaponFlags = protoR->item.flagsExt;

			wTypeR = fo::GetWeaponType(weaponFlags);
			if (wTypeR == AttackSubType::GUNS) return; // the attacker **not move away** if the target has a firearm
			wTypeRs = fo::GetWeaponType(weaponFlags >> 4);
		}
		if (target == fo::var::obj_dude) {
			fo::GameObject* itemHandL = fo::func::inven_left_hand(target);
			if (itemHandL) {
				protoL = fo::GetProto(itemHandL->protoId);
				wTypeL = fo::GetWeaponType(protoL->item.flagsExt);
				if (wTypeL == AttackSubType::GUNS) return; // the attacker **not move away** if the target(dude) has a firearm
				wTypeLs = fo::GetWeaponType(protoL->item.flagsExt >> 4);
			} else if (!itemHandR) {
				// dude is unarmed
				long damage = fo::func::stat_level(target, Stat::STAT_melee_dmg);
				if (damage * 4 < source->critter.health / 2) return;
			}
		}
moveAway:
		// if attacker is aggressive then **not move away** from any throwing weapons (include grenades)
		if (cap->disposition == AIpref::aggressive) {
			if (wTypeRs == AttackSubType::THROWING || wTypeLs == AttackSubType::THROWING) return;
			if (wTypeR  == AttackSubType::THROWING || wTypeL  == AttackSubType::THROWING) return;
		} else {
			// the attacker **not move away** if the target has a throwing weapon and it is a grenade
			if (protoR && wTypeR == AttackSubType::THROWING && protoR->item.weapon.damageType != DamageType::DMG_normal) return;
			if (protoL && wTypeL == AttackSubType::THROWING && protoL->item.weapon.damageType != DamageType::DMG_normal) return;
		}
		distance -= 3; // всегда держаться на дистанции в 2 гекса от цели
	}
	else if (GetTargetDistance(source, target) == -1) return;

	if (isDebug) {
		#ifdef NDEBUG
		fo::func::debug_printf("\n[AI] %s: Away from my target!", fo::func::critter_name(source));
		#else
		fo::func::debug_printf("\n[AI] %s: Away from: %s, Dist: %d, MP: %d.", fo::func::critter_name(source), fo::func::critter_name(target), distance, source->critter.movePoints);
		#endif
	}
	fo::func::ai_move_away(source, target, distance); // функция принимает отрицательный аргумент дистанции для того чтобы держаться на определенной дистанции
	// если останутся неиспользованные AP то включенная опция NPCsTryToSpendExtraAP попытается их потратить и снова приблизит npc к цели
}

static void __declspec(naked) ai_try_attack_hack_move() {
	__asm {
		mov  eax, [esi + movePoints];
		test eax, eax;
		jz   noMovePoint;
		mov  eax, dword ptr [esp + 0x364 - 0x3C + 4]; // right_weapon
		push [esp + 0x364 - 0x38 + 4]; // hit_mode
		mov  edx, ebp;
		mov  ecx, esi;
		push eax;
		call sf_ai_move_away_from_target;
noMovePoint:
		mov  eax, -1;
		retn;
	}
}

///////////////////////////////////////////////////////////////////////////////

static int32_t __fastcall sf_combat_check_bad_shot(fo::GameObject* source, fo::GameObject* target) {
	long distance = 1, tile = -1;
	long hitMode = fo::ATKTYPE_RWEAPON_PRIMARY;

	if (target && target->critter.damageFlags & fo::DAM_DEAD) return 4; // target is dead

	fo::GameObject* item = fo::func::inven_right_hand(source);
	if (!item) return 0; // unarmed

	if (target) {
		tile = target->tile;
		distance = fo::func::obj_dist(source, target);
		hitMode = fo::func::ai_pick_hit_mode(source, item, target);
	}

	long flags = source->critter.damageFlags;
	if (flags & fo::DAM_CRIP_ARM_LEFT && flags & fo::DAM_CRIP_ARM_RIGHT) {
		return 3; // crippled both hands
	}
	///if (flags & (fo::DAM_CRIP_ARM_RIGHT | fo::DAM_CRIP_ARM_LEFT) && fo::func::item_w_is_2handed(item)) {
	///	return 3; // one of the hands is crippled, can't use a two-handed weapon
	///}

	long attackRange = fo::func::item_w_range(source, hitMode);
	if (attackRange > 1 && fo::func::combat_is_shot_blocked(source, source->tile, tile, target, 0)) {
		return 2; // shot to target is blocked
	}
	return (attackRange < distance); // 1 - target is out of range of the attack
}

fo::GameObject* rememberTarget = nullptr;

// Sets a target for the AI from whoHitMe if an alternative target was not found
// or chooses a near target between the currently find target and rememberTarget
static void __declspec(naked) combat_ai_hook_revert_target() {
	__asm {
		cmp   rememberTarget, 0;
		jnz   pickNearTarget;
		test  edi, edi; // find target?
		cmovz edi, [esi + whoHitMe];
		mov   edx, edi;
		jmp   fo::funcoffs::cai_perform_distance_prefs_;

pickNearTarget:
		test  edi, edi; // find target?
		jz    pickRemember;
		call  fo::funcoffs::obj_dist_; // dist1: source & target
		push  eax;
		mov   eax, esi;
		mov   edx, rememberTarget
		call  fo::funcoffs::obj_dist_; // dist2: source & rememberTarget
		pop   edx;
		cmp   eax, edx;             // compare distance
		cmovbe edi, rememberTarget; // dist2 <= dist1
		mov   edx, edi;
		mov   eax, esi; // restore source
		mov   rememberTarget, 0;
		jmp   fo::funcoffs::cai_perform_distance_prefs_;

pickRemember:
		mov   edi, rememberTarget;
		mov   edx, edi;
		mov   rememberTarget, 0;
		jmp   fo::funcoffs::cai_perform_distance_prefs_;
	}
}

static void __declspec(naked) ai_danger_source_hook() {
	__asm {
		cmp  dword ptr [esp + 56], 0x42B235 + 5; // called fr. combat_ai_
		je   fix;
		jmp  fo::funcoffs::combat_check_bad_shot_;
fix:
		mov  ecx, eax; // source
		call sf_combat_check_bad_shot;
		cmp  eax, 1;   // check result
		jne  skip;
		// weapon out of range
		cmp  rememberTarget, 0;
		jnz  skip;
		mov  edx, [esp + edi + 4]; // offset from target1
		mov  rememberTarget, edx;  // remember the target to return to it later
skip:
		retn;
	}
}

static void __declspec(naked) ai_danger_source_hook_party_member() {
	__asm {
		cmp  dword ptr [esp + 56], 0x42B235 + 5; // called fr. combat_ai_
		je   fix;
		jmp  fo::funcoffs::combat_check_bad_shot_;
fix:
		mov  ecx, eax; // source
		call sf_combat_check_bad_shot;
		cmp  eax, 1;   // check result
		setg al;       // set 0 for result OK
		retn;
	}
}

static int32_t __fastcall ai_try_move_steps_closer(fo::GameObject* source, fo::GameObject* target, int32_t &outHitMode) {
	long getTile = -1, dist = -1;

	fo::GameObject* itemHand = fo::func::inven_right_hand(source);
	if (itemHand) {
		long mode = fo::func::ai_pick_hit_mode(source, itemHand, target);

		// check the distance and number of remaining AP's
		long weaponRange = fo::func::item_w_range(source, mode);
		if (weaponRange <= 2) {
			return 1;
		}
		long cost = sf_item_w_mp_cost(source, mode, 0);
		dist = fo::func::obj_dist(source, target) - weaponRange; // required approach distance
		long ap = source->critter.movePoints - dist; // subtract the number of action points to the move, leaving the number for the shot
		long remainingAP = ap - cost;

		bool notEnoughAP = (cost > ap); // check whether the critter has enough AP to perform the attack

		char rotationData[800];
		long pathLength = fo::func::make_path_func(source, source->tile, target->tile, rotationData, 0, (void*)fo::funcoffs::obj_blocking_at_);

		if (pathLength > 0) {
			if (notEnoughAP) return 1;

			dist += remainingAP; // add remaining AP's to distance
			if (dist > pathLength) dist = pathLength;

			getTile = source->tile;

			// get tile to perform an attack
			for (long i = 0; i < dist; i++)	{
				getTile = fo::func::tile_num_in_direction(getTile, rotationData[i], 1);
			}
		}
		else if (!notEnoughAP) {
			long dir = fo::func::tile_dir(source->tile, target->tile);
			getTile = fo::func::tile_num_in_direction(source->tile, dir, dist); // get tile to move to

			// make a path and check the actual distance of the path
			pathLength = fo::func::make_path_func(source, source->tile, getTile, 0, 0, (void*)fo::funcoffs::obj_blocking_at_);
			if (pathLength > dist) {
				long diff = pathLength - dist;
				if (diff > remainingAP) return 1;
			}

			long _dir = dir, _getTile = getTile;
			long i = 0;
			while (true)
			{
				// check the tile is blocked
				if (!fo::func::obj_blocking_at(source, _getTile, source->elevation)) {
					getTile = _getTile;
					break; // OK, tile is free
				}
				if (++i > 2) return 1; // neighboring tiles are also blocked
				if (i == 1) {
					if (++_dir > 5) _dir = 0;
				} else {
					_dir = dir - 1;
					if (_dir < 0) _dir = 5;
				}
				if (remainingAP < 1) _dir = (_dir + 3) % 6; // invert direction, if there is no AP's in reserve
				_getTile = fo::func::tile_num_in_direction(getTile, _dir, 1);
			}
			// Note: here the value of dist and the distance between getTile and source tile may not match by 1 unit
		}
		// make sure that the distance is within the range of the weapon and the attack is not blocked
		if (getTile != -1 && (fo::func::obj_dist_with_tile(source, getTile, target, target->tile) > weaponRange ||
			fo::func::combat_is_shot_blocked(source, getTile, target->tile, target, 0)))
		{
			return 1;
		}
	}
	//if (dist == -1) dist = source->critter.movePoints; // in dist - the distance to move
	if (getTile != -1 && isDebug) fo::func::debug_printf("\n[AI] %s: Weapon out of range. Move to tile for shot.", fo::func::critter_name(source));

	int result = (getTile != -1) ? CombatMoveToTile(source, getTile, dist) : 1;
	if (!result) outHitMode = fo::func::ai_pick_hit_mode(source, itemHand, target); // try pick new weapon mode after step move

	return result;
}

static void __declspec(naked) ai_try_attack_hook_out_of_range() {
	__asm {
		pushadc;
		mov  ecx, eax;
		lea  eax, [esp + 0x364 - 0x38 + 12 + 4];
		push eax;
		call ai_try_move_steps_closer;
		test eax, eax;
		popadc;
		jnz  defaultMove;
		retn;
defaultMove:
		jmp fo::funcoffs::ai_move_steps_closer_; // default behavior
	}
}

void CombatAIBehaviorInit() { /////////////////// Combat AI improve behavior //////////////////////////

	// Changes the behavior of the AI so that the AI moves to its target to perform an attack/shot when the range of its weapon is less than
	// the distance to the target or the AI will choose the nearest target if any other targets are available
	HookCall(0x42918A, ai_danger_source_hook);
	HookCall(0x42903A, ai_danger_source_hook_party_member);
	HookCall(0x42B240, combat_ai_hook_revert_target); // also need for TryToFindTargets option
	// Forces the AI to move to target closer to make an attack on the target when the distance exceeds the range of the weapon
	HookCall(0x42ABD7, ai_try_attack_hook_out_of_range);

	// Before starting his turn npc will always check if it has better weapons in inventory, than there is a current weapon
	int BetterWeapons = GetConfigInt("CombatAI", "TakeBetterWeapons", 0);
	if (BetterWeapons) {
		HookCall(0x42A92F, ai_try_attack_hook);
		HookCall(0x42A905, ai_try_attack_hook_switch);
		LookupOnGround = (BetterWeapons > 1); // always check the items available on the ground
	}

	switch (GetConfigInt("CombatAI", "TryToFindTargets", 0)) {
	case 1:
		MakeJump(0x4290B6, ai_danger_source_hack_find);
		break;
	case 2: // w/o logic
		SafeWrite16(0x4290B3, 0xDFEB); // jmp 0x429094
		SafeWrite8(0x4290B5, 0x90);
	}

	if (GetConfigInt("CombatAI", "SmartBehavior", 0) > 0) {
		// Checks the movement path for the possibility а shot, if the shot to the target is blocked
		HookCall(0x42AC55, ai_try_attack_hook_shot_blocked);

		// Don't pickup a weapon if its magazine is empty and there are no ammo for it
		HookCall(0x429CF2, ai_search_environ_hook_weapon);

		// Мove away from the target if the target is near
		MakeCalls(ai_try_attack_hack_move, {0x42AE40, 0x42AE7F});
	}

	// When npc does not have enough AP to use the weapon, it begin looking in the inventory another weapon to use,
	// if no suitable weapon is found, then are search the nearby objects(weapons) on the ground to pick-up them
	// This fix prevents pick-up of the object located on the ground, if npc does not have the full amount of AP (ie, the action does occur at not the beginning of its turn)
	// or if there is not enough AP to pick up the object on the ground. Npc will not spend its AP for inappropriate use
	if (GetConfigInt("CombatAI", "ItemPickUpFix", 0)) {
		HookCall(0x429CAF, ai_search_environ_hook);
	}

	// Fixed switching weapons when action points is not enough
	if (GetConfigInt("CombatAI", "NPCSwitchingWeaponFix", 1)) {
		HookCall(0x42AB57, ai_try_attack_hook_switch_fix);
	}
}

}
