﻿/*
 *    sfall
 *    Copyright (C) 2020  The sfall team
 *
 */

#include "..\..\FalloutEngine\Fallout2.h"

#include "..\..\main.h"
#include "..\..\Utils.h"

//#include "..\..\Modules\AI.h"
#include "..\..\Modules\Combat.h"

#include "..\AI\AIHelpers.h"

#include "..\combatAI.h"
#include "..\items.h"
#include "..\objects.h"
#include "..\tilemap.h"

#include "AI.Combat.h"
#include "AI.SearchTarget.h"
#include "AI.Inventory.h"
#include "AI.FuncHelpers.h"

#include "AI.Behavior.h"

/*
	Distance:
	stay        - ограниченное передвижение в бою, атакующий будет по возможности оставаться на том гексе где начал бой, все передвижения запрещены, кроме побега с поле боя
	stay_close  - держаться рядом, атакующий будет держаться на дистанции не превышающей 5 гексов от игрока (применяется только для напарников).
	charge      - атакующее поведение, AI будет пытаться всегда приблизиться к своей цели [перед или же] после атаки.
	snipe       - атака с расстояния, атакующий займет выжидающую позицию с которой будет атаковать, при сокращении дистанции между атакующим и целью, атакующий попытается отойти от цели на дистанцию до 10 гексов.
	on_your_own - специального поведение для этого не определено.

	Disposition: Шаблоны предустановленных настроек для партийцев игрока. Специальных поведений в движке игры для них неопределено.
	coward      - трусливое   (distance=snipe,       run_away_mode=not_feeling_good, chem_use=stims_when_hurt_little, area_attack_mode=sometimes,  AttackWho=closest)
	defensive   - осторожное  (distance=stay_close,  run_away_mode=not_feeling_good, chem_use=stims_when_hurt_lots,   area_attack_mode=be_careful, AttackWho=whomever_attacking_me, )
	aggressive  - агрессивное (distance=on_your_own, run_away_mode=tourniquet,       chem_use=stims_when_hurt_lots,   area_attack_mode=be_careful, AttackWho=whomever)
	berserk     - напролом    (distance=on_your_own, run_away_mode=never,            chem_use=stims_when_hurt_lots,   area_attack_mode=sometimes,  AttackWho=whomever)
*/

namespace game
{
namespace imp_ai
{

namespace sf = sfall;

static const char* retargetTileMsg = "\nI'm in the way of friendly fire! I'll try to move to the nearest tile.";

/////////////////////////////////////////////////////////////////////////////////////////

// Исправляет редкую ситуацию для NPC, когда атакующий NPC (безоружный или вооруженный ближнем оружием)
// захочет подобрать оружие лежащее на карте, но для совершения действий у него не будет хватать AP чтобы подобрать этот предмет,
// после чего на следующем ходу он будет атаковать игрока.
// Это предотвратит в начале атаки попытку поднять оружие при недостаточном количестве AP.
static long  __fastcall AIPickupWeaponFix(fo::GameObject* critter, long distance) {
	long maxAP = fo::func::stat_level(critter, fo::STAT_max_move_points);
	if (critter->critter.getAP() != (maxAP + AICombat::AttackerBonusAP())) return 1; // NPC already used their AP?

	bool allowPickUp = ((critter->critter.getAP(distance) - pickupCostAP) >= 0); // distance & AP check
	if (!allowPickUp) {
		// если NPC имеет стрелковое или метательное оружие, тогда позволено подойти к предмету
		fo::GameObject* item = fo::func::inven_right_hand(critter);
		if (item && AIHelpersExt::WeaponIsGunOrThrowing(item)) return 1; // allow pickup
	}
	DEV_PRINTF1("\nai_search_environ: ItemPickUpFix is allow: %d\n", allowPickUp);
	return 0 | allowPickUp; // false - next item
}

static void __declspec(naked) ai_search_environ_hook() {
	static const DWORD ai_search_environ_Ret = 0x429D3E;
	using namespace fo;
	__asm {
		call fo::funcoffs::obj_dist_;
		cmp  eax, [esp + 0x28 - 0x20 + 4]; // max_dist (PE + 5)
		jle  fix;
		retn;
fix:
		cmp  [esp + 0x28 - 0x1C + 4], item_type_weapon;
		jne  skip;
		push ecx;
		mov  edx, eax; // distance
		mov  ecx, esi; // critter
		call AIPickupWeaponFix;
		pop  ecx;
		test eax, eax;
		jz   continue;
skip:
		retn;
continue:
		add  esp, 4;                // destroy return
		jmp  ai_search_environ_Ret; // next item
	}
}

/////////////////////////////////////////////////////////////////////////////////////////

// Неподбирать оружие если у него пустой магазин, и к нему нет патронов в инвентаре или на карте
static long __fastcall AICheckWeaponAmmo(fo::GameObject* weapon, fo::GameObject* critter) {
	if (game::CombatAI::ai_can_use_weapon(critter, weapon, fo::AttackType::ATKTYPE_RWEAPON_PRIMARY)) {
		return AIInventory::AICheckAmmo(weapon, critter);
	}
	return 0; // 0 - critter no have ammo or can't use weapon
}

static void __declspec(naked) ai_search_environ_hook_weapon() {
	__asm {
		push ecx;
		mov  ecx, esi;          // source
		call AICheckWeaponAmmo; // edx - weapon
		pop  ecx;
		retn;
	}
}

/////////////////////////////////////////////////////////////////////////////////////////

// Проверки при попытке сменить оружие, если у NPC не хватает очков действия для атаки
// Предотвращает не нужный поиск оружия на карте и перемещение к цели для совершения безоружной атаки (если не включена опция NPCsTryToSpendExtraAP)
long __fastcall AIBehavior::AICheckBeforeWeaponSwitch(fo::GameObject* target, long &hitMode, fo::GameObject* source, fo::GameObject* weapon) {
	DEV_PRINTF1("\n[AI] ai_try_attack: Not enough APs for shoot. (curr: %d)", source->critter.getAP());

	if (!weapon) return 1; // no weapon in inventory or hand slot, continue to search weapons on the map (call ai_switch_weapons_)

	long _hitMode = fo::func::ai_pick_hit_mode(source, weapon, target);
	if (_hitMode != hitMode && _hitMode != fo::AttackType::ATKTYPE_PUNCH) {
		if (game::Items::item_weapon_mp_cost(source, weapon, _hitMode, 0) <= source->critter.getAP()) {
			DEV_PRINTF("\n[AI] -> weapon switch hit mode.");
			hitMode = _hitMode;
			return 0; // change hit mode, continue attack cycle
		}
	}

	// does the NPC have other weapons in inventory?
	fo::GameObject* item = fo::func::ai_search_inven_weap(source, 1, target); // search based on AP
	if (item) {
		// is using a close range weapon?
		long wType = fo::func::item_w_subtype(item, fo::AttackType::ATKTYPE_RWEAPON_PRIMARY);
		if (wType <= fo::AttackSubType::MELEE) { // unarmed and melee weapons, check the distance before switching
			if (ai::AIHelpers::AttackInRange(source, item, target) == false) return -1; // target out of range, exit ai_try_attack_
		}
		return 1; // all good, execute vanilla behavior of ai_switch_weapons_ function
	}

	// no other weapon in inventory
	if (fo::func::item_w_range(source, fo::AttackType::ATKTYPE_PUNCH) >= fo::func::obj_dist(source, target)) {
		hitMode = fo::AttackType::ATKTYPE_PUNCH;
		return 0; // change hit mode, continue attack cycle
	}
	return -1; // exit, NPC has a weapon in hand slot, so we are no search another weapon on the map
}

static void __declspec(naked) ai_try_attack_hook_switch_weapon() {
	__asm {
		push edx;
		push [ebx]; // weapon
		push esi;   // source
		call AIBehavior::AICheckBeforeWeaponSwitch; // ecx - target; edx - hit mode
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

// [Test Done]
// Функция попытается найти свободный гекс для совершения выстрела AI по цели, в случаях когда цель для AI заблокирована для выстрела каким либо объектом
// если свободный гекс для встрела не будет найден то выполнится действие функции ai_move_steps_closer
// TODO: Необходимо улучшить алгоритм для поиска гекса для совершения выстрела, для снайперов должна быть применена другая тактика
static long __fastcall SearchTileForShoot(fo::GameObject* source, fo::GameObject* target, fo::AttackType hitMode, long shotCost, long length, long moveAP, char* rotationData) {
	long distance = 0,
		 shotTile = 0;

	long pathLength = length;
	if (pathLength > moveAP) pathLength = moveAP;
	else if (moveAP > pathLength) moveAP = pathLength;

	DEV_PRINTF2("\n[AI] Search tile for shoot: make path distance: %d (set to: %d)", length, pathLength);

	long checkTile = source->tile;
	for (int i = 0; i < pathLength; i++)
	{
		checkTile = fo::func::tile_num_in_direction(checkTile, rotationData[i], 1);
		DEV_PRINTF1("\n[AI] Search tile for shoot: check path tile %d", checkTile);

		// check the line of fire from target to checkTile
		fo::GameObject* object = nullptr;
		fo::func::make_straight_path_func(target, target->tile, checkTile, 0, (DWORD*)&object, 0x20, (void*)fo::funcoffs::obj_shoot_blocking_at_);

		DEV_PRINTF2("\n[AI] Object %s (type: %d) on fire line.", (object) ? fo::func::critter_name(object) : "<None>", (object) ? object->Type() : -1);

		// если object равен null значит линия от цели до checkTile свободно простреливается
		// проверяем есть ли на линии огня от object до цели дружественные криттеры
		object = ai::AIHelpers::CheckShootAndTeamCritterOnLineOfFire(object, checkTile, source->critter.teamNum);
		if (!object) { // if there are no friendly critters
			shotTile = checkTile;
			distance = i + 1;
			DEV_PRINTF2("\n[AI] Get shot tile:%d, Move dist:%d", checkTile, distance);
			break; // взяли [первый] простреливаемый гекс
		}

		if (object->TypeFid() != fo::ObjType::OBJ_TYPE_CRITTER ||                      // объект на линии не криттер -> продолжить
			fo::func::combat_is_shot_blocked(object, object->tile, checkTile, 0, 0)) { // проверяем простреливается ли путь за криттером до checkTile
			continue;
		}
		// запоминаем простреливаемый гекс с которого имеются дружественные NPC на линии огня
		shotTile = checkTile;
		distance = i + 1;
		DEV_PRINTF2("\n[AI] Get friendly fire shot tile:%d, Move dist:%d", checkTile, distance);
		// проверяем следующие гексы пути
	}

	// взяли гекс, но еще остались свободные AP
	if (shotTile && moveAP > distance) {
		if (AICombat::AttackerAI()->distance != fo::AIpref::Distance::snipe && AICombat::AttackerAI()->disposition != fo::AIpref::Disposition::coward &&  // оставляем AP для поведения "Snipe"
			fo::func::determine_to_hit_from_tile(source, shotTile, target, fo::BodyPart::Uncalled, hitMode) < 50) { // проверить шанс попадания по цели

			moveAP -= distance;
			long countAttack = moveAP / shotCost; // количество атак после подхода
			long leftAP = moveAP % shotCost;      // свободные AP для подхода [оставляем ходы на последующие атаки]

			long newTile = checkTile = shotTile;
			long dist;

			for (int i = distance; (leftAP > 0 && i < pathLength); i++, leftAP--) // начинаем со следующего тайла и идем до конца пути
			{
				checkTile = fo::func::tile_num_in_direction(checkTile, rotationData[i], 1);
				DEV_PRINTF2("\n[AI] Search tile for shoot: check extra path tile %d [leftAP: %d]", checkTile, leftAP);

				fo::GameObject* object = nullptr;
				// check the line of fire from target to checkTile
				fo::func::make_straight_path_func(target, target->tile, checkTile, 0, (DWORD*)&object, 0x20, (void*)fo::funcoffs::obj_shoot_blocking_at_);

				if (!ai::AIHelpers::CheckShootAndTeamCritterOnLineOfFire(object, checkTile, source->critter.teamNum)) { // if there are no friendly critters
					newTile = checkTile;
					dist = i;

					// проверить шанс попадания по цели если он все еще плохой, добавить AP к передвижению
					if (countAttack > 0 && fo::func::determine_to_hit_from_tile(source, newTile, target, fo::BodyPart::Uncalled, hitMode) < 25) {
						countAttack--;
						leftAP += shotCost;
					}
				}
			}
			if (newTile != shotTile) {
				distance = dist + 1;
				shotTile = newTile;
				DEV_PRINTF2("\n[AI] Get extra tile:%d, Move dist:%d", shotTile, distance);
			}
		}
	}
	return (distance << 16) | shotTile;
}

static long __fastcall GetTileForThruShoot(fo::GameObject* source, fo::GameObject* target, long length, char* rotationData, long moveAP) {
	long tile = source->tile;

	//long sx, sy; // x432 / y350
	//long tx, ty; // x704 / y506
	//fo::func::tile_coord(source->tile, &sx, &sy);
	//fo::func::tile_coord(target->tile, &tx, &ty);

	// x = 704 (tx)
	// y = 350 (sy)
	// x 9 гексов разницы
	// y 7

	for (int i = 0; i < length; i++)
	{
		tile = fo::func::tile_num_in_direction(tile, rotationData[i], 1);
		//if (i == 0) continue; // пропускаем первый гекс

		fo::GameObject* obj = fo::func::obj_blocking_at(source, tile, source->elevation);
		if (obj && obj->flags & fo::ObjectFlag::ShootThru) {

			// проверить простреливается ли линия к цели через объект
			fo::GameObject* outObj = obj; // игнорируем obj
			fo::func::make_straight_path_func(target, target->tile, tile, 0, (DWORD*)&outObj, 0x20, (void*)fo::funcoffs::obj_shoot_blocking_at_);
			if (outObj && !outObj->IsCritter()) continue; // не простреливается, следующий объект

			// построить путь до объекта
			// определить гекс линии прострела
			long sTile = 0;
			long dist = fo::func::tile_dist(target->tile, obj->tile) + 1;
			for (long i = dist; i < dist + 2; i++) // максимальная дистанция за объектом +3
			{
				sTile = Tilemap::tile_num_beyond(target->tile, obj->tile, i);
				if (!fo::func::obj_blocking_at(source, sTile, source->elevation)) break;
				sTile =  0;
			}
			if (sTile == 0) continue;

			char path[800];
			long len = fo::func::make_path_func(source, source->tile, sTile, path, 0, (void*)fo::funcoffs::obj_blocking_at_);
			if (len == 0) continue; // невозможно построить путь
			//if (len > length) return -1;

			// путь до гекса построен, определить с какого гекса пути можно сделать выстрел
			if (len > moveAP) len = moveAP;
			tile = source->tile;
			dist = AIHelpersExt::SearchTileShoot(target, tile, path, len);
			if (dist) return (dist << 16) | tile; // гекс для выстрела
		}
	}
	return 0;
}

static long __fastcall AISearchTileForShoot(fo::GameObject* source, fo::GameObject* target, fo::AttackType &hitMode) {

	if (AICombat::AttackerAI()->distance == fo::AIpref::Distance::stay) return 0;

	fo::GameObject* itemHand = fo::func::inven_right_hand(source);
	if (!itemHand || fo::func::item_w_subtype(itemHand, hitMode) <= fo::AttackSubType::MELEE) {
		return 0;
	}

	long shotCost = game::Items::item_w_mp_cost(source, hitMode, 0);
	long moveAP = source->critter.getMoveAP() - shotCost; // left ap for distance move
	if (moveAP <= 0) return 0;

	DEV_PRINTF1("\n[AI] Search tile for shoot: max AP distance %d", moveAP);

	long shotTile1 = 0;
	long shotTile2 = 0;

	char rotationData[800];
	long length = fo::func::make_path_func(source, source->tile, target->tile, rotationData, 0, (void*)fo::funcoffs::obj_blocking_at_);
	if (length != 0) {
		shotTile1 = SearchTileForShoot(source, target, hitMode, shotCost, length, moveAP, rotationData);
	} else {
		DEV_PRINTF("\n[AI] Search tile for shoot: Nove path blocked.");
	}

	long length2 = fo::func::make_path_func(source, source->tile, target->tile, rotationData, 0, AIHelpersExt::obj_ai_shoot_blocking_at_);
	if (length2 > 0) {
		if (length2 != length) shotTile2 = GetTileForThruShoot(source, target, length2, rotationData, moveAP);
	} else {
		DEV_PRINTF("\n[AI] Search tile for shoot: Thru path blocked.");
	}

	if ((shotTile2 >> 16) > (shotTile1 >> 16)) shotTile1 = shotTile2;
	long shotTile = shotTile1 & 0xFFFF;

	if (shotTile && sf::isDebug) fo::func::debug_printf("\n[AI] %s: Move to tile for shot: %d", fo::func::critter_name(source), shotTile);

	int result = (shotTile && AIHelpersExt::CombatMoveToTile(source, shotTile, shotTile1 >> 16) == 0) ? 1 : 0;
	if (result) hitMode = (fo::AttackType)fo::func::ai_pick_hit_mode(source, itemHand, target); // try pick new weapon mode after move

	return result;
}

static void __declspec(naked) ai_try_attack_hook_shot_blocked() {
	__asm {
		push ecx;
		lea  eax, [esp + 0x364 - 0x38 + 4 + 4];
		push eax;                  // hit mode ref
		mov  ecx, esi;             // source
		call AISearchTileForShoot; // edx - target
		test eax, eax;
		jz   default;
		add  esp, 4;
		retn;
default:
		pop  ecx;
		mov  edx, ebp; // target
		mov  eax, esi; // source
		jmp  fo::funcoffs::ai_move_steps_closer_;
	}
}

/////////////////////////////////////////////////////////////////////////////////////////

static bool LookupOnGround = false;
static long LootingCorpses = 0;

// [Test Done]
static fo::GameObject* AISearchBestWeaponInCorpses(fo::GameObject* source, fo::GameObject* itemEnv, fo::GameObject* target, long &inCorpse, long &distToObject) {
	long* objectsList = nullptr;
	inCorpse = 0;

	fo::GameObject* owner = nullptr;
	fo::GameObject* item = nullptr;
	long dist = 0;

	DEV_PRINTF("\n[AI] Try SearchBestWeaponInCorpses:");

	long numObjects = fo::func::obj_create_list(-1, source->elevation, fo::ObjType::OBJ_TYPE_CRITTER, &objectsList);
	if (numObjects > 0) {
		fo::var::combat_obj = source;
		fo::func::qsort(objectsList, numObjects, 4, fo::funcoffs::compare_nearer_);

		for (int i = 0; i < numObjects; i++)
		{
			fo::GameObject* object = (fo::GameObject*)objectsList[i];
			if (object->critter.IsNotDead() || object->invenSize <= 0) continue;

			DEV_PRINTF1("\n[AI] Find corpse: %s", fo::func::critter_name(object));

			if (fo::func::obj_dist(source, object) > source->critter.getAP() + 1) break;

			if (fo::util::GetProto(object->protoId)->critter.critterFlags & fo::CritterFlags::NoSteal) continue;

			// check block and distance path
			int toDistObject = 0;
			if (source->tile != object->tile) {
				toDistObject = fo::func::make_path_func(source, source->tile, object->tile, 0, 0, (void*)fo::funcoffs::obj_blocking_at_);
				if (toDistObject == 0 || toDistObject > source->critter.getMoveAP() + 1) continue;
			}
			DWORD slot = -1;
			while (true)
			{
				fo::GameObject* itemGround = fo::func::inven_find_type(object, fo::item_type_weapon, &slot);
				if (!itemGround) break;

				if (itemEnv && itemEnv->protoId == itemGround->protoId) {
					// проверить расстояние до предмета лежащего на земле
					if (fo::func::obj_dist(source, object) >= fo::func::obj_dist(source, itemEnv)) continue;
					itemEnv = nullptr;
				}

				if (item && item->protoId == itemGround->protoId) continue;
				if (!game::CombatAI::ai_can_use_weapon(source, itemGround, fo::AttackType::ATKTYPE_RWEAPON_PRIMARY)) continue;

				// проверяем наличее и количество имеющихся патронов
				if (itemGround->item.ammoPid != -1 && !AIInventory::AICheckAmmo(itemGround, source) && sf::Combat::check_item_ammo_cost(itemGround, fo::AttackType::ATKTYPE_RWEAPON_PRIMARY) <= 0) {
					continue;
				}

				DEV_PRINTF1("\n[AI] Find pid: %d", itemGround->protoId);

				if (AIInventory::BestWeapon(source, item, itemGround, target) == itemGround) {
					item = itemGround;
					owner = object;
					dist = toDistObject;
				}
			}
			// next corpse
		}
		fo::func::obj_delete_list(objectsList);
	}
	if (owner) {
		if (itemEnv && AIInventory::BestWeapon(source, itemEnv, item, target) == itemEnv) return itemEnv;

		DEV_PRINTF3("\n[AI] SearchBestWeaponInCorpses: %d (%s), Owner: %s", item->protoId, fo::func::critter_name(item), fo::func::critter_name(item->owner));
		item->owner = owner;
		inCorpse = 1;
		distToObject = dist;
		return item;
	}
	return itemEnv;
}

// [Test Done]
static fo::GameObject* AISearchBestWeaponOnGround(fo::GameObject* source, fo::GameObject* item, fo::GameObject* target, long &inCorpse, long &distToObject) {
	long* objectsList = nullptr;

	DEV_PRINTF("\n[AI] Try SearchBestWeaponOnGround:");

	long numObjects = fo::func::obj_create_list(-1, source->elevation, fo::ObjType::OBJ_TYPE_ITEM, &objectsList);
	if (numObjects > 0) {
		fo::var::combat_obj = source;
		fo::func::qsort(objectsList, numObjects, 4, fo::funcoffs::compare_nearer_);

		for (int i = 0; i < numObjects; i++)
		{
			fo::GameObject* itemGround = (fo::GameObject*)objectsList[i];
			if (item && item->protoId == itemGround->protoId) continue;

			if (fo::func::item_get_type(itemGround) == fo::item_type_weapon) {
				if (fo::func::obj_dist(source, itemGround) > source->critter.getMoveAP() + 1) break;

				DEV_PRINTF2("\n[AI] Find item: %d (%s)", itemGround->protoId, fo::func::critter_name(itemGround));

				// check block and distance path
				int toDistObject = 0;
				if (source->tile != itemGround->tile) {
					toDistObject = fo::func::make_path_func(source, source->tile, itemGround->tile, 0, 0, (void*)fo::funcoffs::obj_blocking_at_);
					if (toDistObject == 0 || toDistObject > source->critter.getMoveAP() + 1) continue;
				}

				if (game::CombatAI::ai_can_use_weapon(source, itemGround, fo::AttackType::ATKTYPE_RWEAPON_PRIMARY) &&
					// проверяем наличее и количество имеющихся патронов
					itemGround->item.ammoPid == -1 || AIInventory::AICheckAmmo(itemGround, source) && sf::Combat::check_item_ammo_cost(itemGround, fo::AttackType::ATKTYPE_RWEAPON_PRIMARY) > 0)
				{
					if (AIInventory::BestWeapon(source, item, itemGround, target) == itemGround) {
						DEV_PRINTF2("\n[AI] SearchBestWeaponOnGround: %d (%s)", itemGround->protoId, fo::func::critter_name(itemGround));
						item = itemGround;
						distToObject = toDistObject;
					}
				}
			}
		}
		fo::func::obj_delete_list(objectsList);
	}

	// Члены партии не осматривают трупы в поисках лучшего оружия
	return ((LootingCorpses == 1 && !AICombat::AttackerInParty()) || LootingCorpses == 2)
		? AISearchBestWeaponInCorpses(source, item, target, inCorpse, distToObject)
		: item;
}

// Проверяет сможет ли атакующий атаковать цель с текущей позиции используя лучшее оружие
// если лучшее оружие является оружием ближнего действия, а текущее оружие является метательным или стрелковым
static bool CheckCanAttackTarget(fo::GameObject* bestWeapon, fo::GameObject* itemHand, fo::GameObject* source, fo::GameObject* target) {
	if (AIHelpersExt::GetWeaponSubType(bestWeapon, 0) <= fo::AttackSubType::MELEE) {
		DEV_PRINTF("\n[AI] BestWeapon is MELEE.");
		if (AIHelpersExt::WeaponIsGunOrThrowing(itemHand, fo::AttackType::ATKTYPE_RWEAPON_PRIMARY)) {
			DEV_PRINTF("\n[AI] Hand Weapon is ranged.");
			if (ai::AIHelpers::AttackInRange(source, bestWeapon, target) == false) {
				return false;
			}
		}
		DEV_PRINTF("\n[AI] Can Attack Target.");
	}
	return true;
}

// Поиск наилучшего оружия перед совершением атаки (в первом цикле ai_try_attack_)
// Атакующий попытается найти лучшее оружие в своем инвентаре или подобрать близлежащее оружие на карте
// Executed once when the NPC starts attacking
static fo::AttackType AISearchBestWeaponOnBeginAttack(fo::GameObject* source, fo::GameObject* target, fo::GameObject* &weapon, fo::AttackType hitMode) {

	fo::GameObject* itemHand   = weapon; // current item
	fo::GameObject* bestWeapon = weapon;

	DEV_PRINTF2("\n[AI] HandPid: %d (%s)", ((itemHand) ? itemHand->protoId : -1), ((itemHand) ? fo::func::critter_name(itemHand) : "-"));

	DWORD slotNum = -1;
	while (true)
	{
		fo::GameObject* item = fo::func::inven_find_type(source, fo::item_type_weapon, &slotNum);
		if (!item) break;
		if (itemHand && itemHand->protoId == item->protoId) continue;

		if ((source->critter.getAP() >= game::Items::item_weapon_mp_cost(source, item, fo::AttackType::ATKTYPE_RWEAPON_PRIMARY, 0)) &&
			game::CombatAI::ai_can_use_weapon(source, item, fo::AttackType::ATKTYPE_RWEAPON_PRIMARY))
		{
			if (item->item.ammoPid == -1 || // оружие не имеет патронов
				fo::func::item_w_subtype(item, fo::AttackType::ATKTYPE_RWEAPON_PRIMARY) == fo::AttackSubType::THROWING || // Зачем здесь метательные?
				(sf::Combat::check_item_ammo_cost(item, fo::AttackType::ATKTYPE_RWEAPON_PRIMARY) || AIInventory::CritterHaveAmmo(source, item)))
			{
				if (!fo::func::combat_safety_invalidate_weapon_func(source, item, fo::AttackType::ATKTYPE_RWEAPON_PRIMARY, target, 0, 0)) { // weapon safety
					bestWeapon = AIInventory::BestWeapon(source, bestWeapon, item, target);
				}
			}
		}
	}

	if (itemHand != bestWeapon)	{
		DEV_PRINTF2("\n[AI] Find best weapon Pid: %d (%s)", (bestWeapon) ? bestWeapon->protoId : -1, (bestWeapon) ? fo::func::critter_name(bestWeapon) : "");
		bestWeapon = AIHelpersExt::AICheckWeaponSkill(source, itemHand, bestWeapon); // выбрать лучшее на основе навыка
		DEV_PRINTF1("\n[AI] Skill best weapon pid: %d", (bestWeapon) ? bestWeapon->protoId : -1);
		// проверить сможет ли атакующий сразу атаковать свою цель
		if (itemHand && (itemHand != bestWeapon) && CheckCanAttackTarget(bestWeapon, itemHand, source, target) == false) bestWeapon = itemHand; // оружие неподходит в текущей ситуации
		DEV_PRINTF1("\n[AI] Select best weapon pid: %d", (bestWeapon) ? bestWeapon->protoId : -1);
	}

	if (source->critter.getAP() >= pickupCostAP && fo::func::critter_body_type(source) == fo::BodyType::Biped && !fo::func::critterIsOverloaded(source)) {

		long itemHandType = (itemHand) ? AIHelpersExt::GetWeaponSubType(itemHand, hitMode) : fo::AttackSubType::NONE;

		// построить путь до цели
		if (itemHand && itemHandType <= fo::AttackSubType::MELEE) {
			int toDistTarget = fo::func::make_path_func(source, source->tile, target->tile, 0, 0, (void*)fo::funcoffs::obj_blocking_at_);
			// не поднимать, если у атакующего хватает очков сделать атаку по цели
			if (toDistTarget && source->critter.getAP(toDistTarget) >= game::Items::item_w_mp_cost(source, hitMode, 0)) goto notRetrieve;
		}

		long inCorpse, distToObject = -1;
		fo::GameObject* itemGround = AISearchBestWeaponOnGround(source, bestWeapon, target, inCorpse, distToObject);

		if (itemGround != bestWeapon) {
			DEV_PRINTF3("\n[AI] BestWeapon find on ground. Pid: %d (%s), dist: %d", ((itemGround) ? itemGround->protoId : -1), ((itemGround) ? fo::func::critter_name(itemGround) : ""), distToObject);

			if (LookupOnGround == false && distToObject > 1) goto notRetrieve;

			if (itemGround && (!bestWeapon || itemGround->protoId != bestWeapon->protoId)) {
				DEV_PRINTF("\n[AI] Evaluation weapon...");

				// не поднимать если у атакующего стреляющее оружие и нехватате очков подобрать оружие и сделать выстрел
				if (itemHandType == fo::AttackSubType::GUNS) {
					long leftAp = source->critter.getAP(distToObject) - pickupCostAP;
					if (leftAp <= 0) goto notRetrieve;

					long apShoot = game::Items::item_weapon_mp_cost(source, itemHand, hitMode, 0);
					if (leftAp < apShoot) goto notRetrieve;
				}

				fo::GameObject* item = AIHelpersExt::AICheckWeaponSkill(source, bestWeapon, itemGround);
				DEV_PRINTF1("\n[AI] Skill best weapon pid: %d", (item) ? item->protoId : -1);
				if (item != itemGround) goto notRetrieve;

				fo::func::dev_printf("\n[AI] Try pickup item pid: %d AP: %d", itemGround->protoId, source->critter.getAP());

				// pickup item
				fo::GameObject* itemRetrieve = (inCorpse)
				                             ? AIInventory::AIRetrieveCorpseItem(source, itemGround)
				                             : fo::func::ai_retrieve_object(source, itemGround);

				DEV_PRINTF2("\n[AI] Pickup pid: %d AP: %d", (itemRetrieve) ? itemRetrieve->protoId : -1, source->critter.getAP());

				if (itemRetrieve && itemRetrieve->protoId == itemGround->protoId) {
					// if there is not enough action points to use the weapon, then just pick up this item
					bestWeapon = (!itemHand || source->critter.getAP() >= game::Items::item_weapon_mp_cost(source, itemRetrieve, fo::AttackType::ATKTYPE_RWEAPON_PRIMARY, 0))
					           ? itemRetrieve
					           : nullptr;
				}
			}
		}
	}
notRetrieve:
	DEV_PRINTF2("\n[AI] BestWeapon Pid: %d AP: %d", ((bestWeapon) ? bestWeapon->protoId : -1), source->critter.getAP());

	if (bestWeapon && (!itemHand || itemHand->protoId != bestWeapon->protoId)) {
		if (sf::isDebug) fo::func::debug_printf("\n[AI] Wield best weapon pid: %d AP: %d", bestWeapon->protoId, source->critter.getAP());
		fo::func::inven_wield(source, bestWeapon, fo::InvenType::INVEN_TYPE_RIGHT_HAND);
		__asm call fo::funcoffs::combat_turn_run_;
		if (fo::func::inven_right_hand(source) == bestWeapon) { // check
			weapon = bestWeapon;
			hitMode = (fo::AttackType)fo::func::ai_pick_hit_mode(source, bestWeapon, target);
		}
	}
	return hitMode;
}

#ifndef NDEBUG
static void PrintShootResult(long result) {
	const char* type;
	switch (result) {
		case 0: type = "0 [OK]"; break;
		case 1: type = "1 [No Ammo]"; break;
		case 2: type = "2 [Out of Range]"; break;
		case 3: type = "3 [Not Enough APs]"; break;
		case 4: type = "4 [Target dead]"; break;
		case 5: type = "5 [Shot Blocked]"; break;
		default:
			fo::func::debug_printf("\n[AI] Try Attack: Check bad shot result: %d", result);
			return;
	}
	fo::func::debug_printf("\n[AI] Try Attack: Check bad shot result: %s", type);
}
#else
static void PrintShootResult(long result) {}
#endif

static bool weaponIsSwitched = false; // true - смена оружия уже была произведена

static CombatShootResult __fastcall AICheckAttack(fo::GameObject* &weapon, fo::GameObject* target, fo::GameObject* source, fo::AttackType &hitMode, long attackCounter, long safetyRange) {
	// safetyRange: значение дистанции для перестройки во время выполнения атаки для оружия по площади

	if (attackCounter == 0 && safetyRange == 0 && !weaponIsSwitched) {
		hitMode = AISearchBestWeaponOnBeginAttack(source, target, weapon, hitMode);
	}
	weaponIsSwitched = false;

	long dist = fo::func::obj_dist(source, target) - 1;
	long statIQ = fo::func::stat_level(source, fo::Stat::STAT_iq);

	bool forceBurst = false;
	if (attackCounter == 0 && AICombat::combatDifficulty != CombatDifficulty::Easy && hitMode == fo::AttackType::ATKTYPE_RWEAPON_PRIMARY && dist <= 3) {
		// атакующий расположен достаточно близко к цели, его оружие имеет стрельбу очередью (burst attack)
		// принудительно использовать втоичный режим стрельбы если атака по цели будет безопасной
		if (!sf::Combat::IsBurstDisabled(source) && fo::func::item_w_anim_weap(weapon, fo::AttackType::ATKTYPE_RWEAPON_SECONDARY) == fo::Animation::ANIM_fire_burst) {
			if (statIQ <= 3 || !fo::func::combat_safety_invalidate_weapon_func(source, weapon, fo::AttackType::ATKTYPE_RWEAPON_SECONDARY, target, 0, 0)) // weapon is safety
			{
				forceBurst = true;
				hitMode = fo::AttackType::ATKTYPE_RWEAPON_SECONDARY;
				DEV_PRINTF("\n[AI] Force use burst attack.");
			}
		}
	}

	// проверить достаточно ли имеется запасов патронов для выстрела очередью
	if (!forceBurst && hitMode == fo::AttackType::ATKTYPE_RWEAPON_SECONDARY && dist >= 10 && statIQ > 5 && fo::func::item_w_anim_weap(weapon, fo::AttackType::ATKTYPE_RWEAPON_SECONDARY) == fo::Animation::ANIM_fire_burst &&
		(weapon->item.charges - fo::func::item_w_rounds(weapon) <= 5 || AIInventory::CritterHaveAmmo(source, weapon) <= 5)) {
		hitMode = fo::AttackType::ATKTYPE_RWEAPON_PRIMARY; // сохраняем количество патронов
	}

	CombatShootResult result = AICombat::combat_check_bad_shot(source, target, hitMode, 0);

	// for debug
	PrintShootResult((long)result); // "Try Attack: Check bad shot result:"

	switch (result)
	{
		case CombatShootResult::Ok:
			if (hitMode == fo::AttackType::ATKTYPE_RWEAPON_SECONDARY && AICombat::combatDifficulty != CombatDifficulty::Easy && safetyRange == 0) {
				// используется вторичный режим оружия (стрельба очередью) и имеется расстояние между целью и атакующим
				// если атакующему хватате очков действия на подход к цели то выполнить передвижение к цели
				if (statIQ > 5 && dist > 0 && dist <= 5 && AICombat::AttackerAI()->distance != fo::AIpref::Distance::snipe) {
					if (fo::func::item_w_anim_weap(weapon, hitMode) == fo::Animation::ANIM_fire_burst) {
						long costAP = game::Items::item_weapon_mp_cost(source, weapon, hitMode, 0);
						long moveDist = source->critter.getMoveAP() - costAP;
						if (moveDist > 0) {
							if (moveDist >= costAP && fo::func::determine_to_hit(source, target, fo::BodyPart::Uncalled, hitMode) >= 70) {
								// если процент попадания по цели достаточно высокий, уменьшить дистанцию передвижения, чтобы хватило на несколько атак
								while (moveDist >= costAP) moveDist -= costAP;
							}
							if (moveDist > 0) AIHelpersExt::MoveToTarget(source, target, moveDist);
						}
					}
				}
			}
			break;
		//case 1: break; // [No Ammo]
		//case 2: break; // [Out of Range]
		//case 3: break; // [Not Enough APs]
		//case 4: break; // [Target dead]
		//case 5: break; // [Shot Blocked]
	}
	return result;
}

static void __declspec(naked) ai_try_attack_hook_check_attack() {
	using namespace fo::Fields;
	__asm {
		// fix: update curr_mp variable
		mov  eax, [esi + movePoints];
		mov  [esp + 0x364 - 0x20 + 4], eax; //  curr_mp

		mov  eax, [esp + 0x364 - 0x44 + 4]; // safety_range
		lea  ebx, [esp + 0x364 - 0x38 + 4]; // hit_mode ref
		lea  ecx, [esp + 0x364 - 0x3C + 4]; // right_weapon ref
		push eax;
		push edi;                           // attack_counter
		push ebx;
		push esi;                           // source
		call AICheckAttack;                 // edx - target
		cmp  eax, NoActionPoint; // CombatShootResult [8]
		je   exitTryAttack;
		cmp  eax, TargetDead;    // CombatShootResult [4]
		je   exitTryAttack;
		retn;
exitTryAttack:
		mov  edi, 10;                                 // set end loop
		mov  dword ptr [esp + 0x364 - 0x30 + 4], eax; // set result value
		retn;
	}
}

static void __declspec(naked) ai_try_attack_hook_w_switch_begin_turn() {
	__asm {
		mov weaponIsSwitched, 1;
		jmp fo::funcoffs::ai_switch_weapons_;
	}
}

/////////////////////////////////////////////////////////////////////////////////////////

// Заставляет NPC двигаться ближе к цели, чтобы начать атаковать, когда расстояние до цели превышает дальность действия оружия
static long __fastcall AIMoveStepToAttackTile(fo::GameObject* source, fo::GameObject* target, long &outHitMode) {
	if (AICombat::AttackerAI()->distance == fo::AIpref::Distance::stay) return 1;

	fo::GameObject* itemHand = fo::func::inven_right_hand(source);
	if (!itemHand) return 1;

	long hitMode = outHitMode;

	// check the distance and number of remaining AP's
	long weaponRange = fo::func::item_w_range(source, hitMode);
	if (weaponRange <= 1) return 1;

	long cost = game::Items::item_w_mp_cost(source, (fo::AttackType)hitMode, 0);
	long dist = fo::func::obj_dist(source, target);

	DEV_PRINTF1("\n[AI] MoveStepToAttackTile: dist to target: %d", dist);

	long distToMove = dist - weaponRange;        // required approach distance
	long ap = source->critter.getAP(distToMove); // subtract the number of action points to the move, leaving the number for the shot
	long remainingAP = ap % cost;                // оставшиеся очки действия после атаки

	bool notEnoughAP = (cost > ap); // check whether the critter has enough AP to perform the attack
	if (notEnoughAP) return 1;      // нехватает очков действия для совершения атаки

	char rotationData[800];
	long pathLength = fo::func::make_path_func(source, source->tile, target->tile, rotationData, 0, (void*)fo::funcoffs::obj_blocking_at_);

	long getTile = -1;

	if (pathLength > 0) { // путь до цели построен успешно
		getTile = source->tile;

		// get tile to perform an attack
		for (long i = 0; i < distToMove; i++) {
			getTile = fo::func::tile_num_in_direction(getTile, rotationData[i], 1);
		}

		// проверяем вероятность поразить цель, если более 50% то атакуем с гекса, иначе двигаемя до конца дистанции
		if (fo::func::determine_to_hit_from_tile(source, getTile, target, fo::BodyPart::Uncalled, hitMode) < 50) {
			long start = distToMove;
			// подходим на все имеющиеся AP
			distToMove += remainingAP; // add remaining AP's to distance
			if (distToMove > pathLength) distToMove = pathLength;

			for (long i = start; i < distToMove; i++)	{
				getTile = fo::func::tile_num_in_direction(getTile, rotationData[i], 1);
			}
		}
		DEV_PRINTF3("\nAIMoveStepToAttackTile1: distToMove/pathLength: %d/%d, distTile: %d", distToMove, pathLength, fo::func::tile_dist(source->tile, getTile));
	} else {
		// AP достаточно для подхода и совершения атаки, но путь до цели построить не удалось
		// построить путь до предпологаемой плитки атаки
		long dir = fo::func::tile_dir(source->tile, target->tile);
		getTile = fo::func::tile_num_in_direction(source->tile, dir, distToMove); // get tile to move to

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

			if (remainingAP <= 0) _dir = (_dir + 3) % 6; // invert direction, if there is no AP's in reserve
			_getTile = fo::func::tile_num_in_direction(getTile, _dir, 1);
		}

		// make a path and check the actual distance of the path
		pathLength = fo::func::make_path_func(source, source->tile, getTile, 0, 0, (void*)fo::funcoffs::obj_blocking_at_);
		if (pathLength == 0) return 1; // путь до гекса построить не удалось
		if (pathLength > distToMove) {
			// путь больше чем дистанция, учитываем оставшиеся AP
			long diff = pathLength - distToMove;
			if (diff > remainingAP) return 1; // NPC нехватит очков действия для совершения атаки
		}
		// Примечание: здесь значение distToMove и расстояние между getTile и исходной плиткой могут не совпадать на 1 единицу
		DEV_PRINTF3("\nAIMoveStepToAttackTile2: distToMove/pathLength: %d/%d, distTile: %d", distToMove, pathLength, fo::func::tile_dist(source->tile, getTile));
		distToMove = pathLength;
	}

	if (getTile != -1) {
		dist = fo::func::tile_dist(target->tile, getTile);
		// убедитесь, что расстояние находится в пределах досягаемости оружия и атака не заблокирована
		if (dist > 1 && (fo::func::obj_dist_with_tile(source, getTile, target, target->tile) > weaponRange ||
			fo::func::combat_is_shot_blocked(source, getTile, target->tile, target, 0)))
		{
			return 1;
		}
		if (sf::isDebug) fo::func::debug_printf("\n[AI] %s: Attack out of range. Move to tile for attack.", fo::func::critter_name(source));
	}

	int result = (getTile != -1) ? AIHelpersExt::CombatMoveToTile(source, getTile, distToMove) : 1;
	if (!result) outHitMode = fo::func::ai_pick_hit_mode(source, itemHand, target); // try pick new weapon mode after move

	return result;
}

static void __declspec(naked) ai_try_attack_hook_out_of_range() {
	__asm {
		push ecx;
		lea  eax, [esp + 0x364 - 0x38 + 4 + 4]; // hit mode ref
		push eax;
		mov  ecx, esi;
		call AIMoveStepToAttackTile;
		pop  ecx;
		test eax, eax;
		jnz  default;
		retn;
default:
		mov  edx, ebp;
		mov  eax, esi;
		jmp  fo::funcoffs::ai_move_steps_closer_; // default behavior
	}
}

/////////////////////////////////////////////////////////////////////////////////////////

// Событие после совершенной атаки, когда больше не хватает AP для продолжения текущей атаки
static AIBehavior::AttackResult __fastcall AICheckResultAfterAttack(fo::GameObject* source, fo::GameObject* target, fo::GameObject* &weapon, fo::AttackType &hitMode) {

	if (source->critter.getAP() <= 0) return AIBehavior::AttackResult::NoMovePoints;

	// цель мертва после атаки, нужно взять другую цель (аналог поведения включенной NPCsTryToSpendExtraAP опции)
	if (target->critter.IsDead()) return AIBehavior::AttackResult::TargetDead;

	if (hitMode >= fo::AttackType::ATKTYPE_PUNCH) return AIBehavior::AttackResult::Default;
	if (AICombat::AttackerBodyType() == fo::BodyType::Quadruped && source->protoId != fo::ProtoID::PID_GORIS) {
		return AIBehavior::AttackResult::Default;
	}

	long dist = fo::func::obj_dist(source, target) - 1;
	DEV_PRINTF2("\n[AI] Attack result: AP: %d. Distance to target: %d", source->critter.getAP(), dist);
	if (dist < 0) dist = 0;

	long costAP = game::Items::item_w_mp_cost(source, fo::AttackType::ATKTYPE_PUNCH, 0);
	fo::GameObject* handItem = fo::func::inven_right_hand(source);

	// условие когда атакующий потерял оружие в процессе атаки
	if (weapon && !handItem /*&& (hitMode == fo::AttackType::)*/) {
		DEV_PRINTF("\n[AI] Attack result: LOST WEAPON\n");
		// проверить дистанцию до цели, если дистанция позволяет то использовать рукопашную атаку
		// иначе подобрать оружие или достать другое из инвентаря (при этом необходимо вычесть 3 AP)
		if (dist >= 3 || (costAP > source->critter.getAP(dist))) return AIBehavior::AttackResult::LostWeapon;

		// ????
		DEV_PRINTF("[AI] Attack result: LOST WEAPON > Try ATTACK\n");
	}

	// еще остались очки действий, но не хватает для атаки, попробовать найти подходящее оружие c меньшим AP в инвентаре и повторить атаку
	if (handItem) {
		fo::AttackType _hitMode = (fo::AttackType)fo::func::ai_pick_hit_mode(source, handItem, target);
		if (_hitMode != hitMode) {
			hitMode = _hitMode;
			DEV_PRINTF("\n[AI] Attack result: SWITCH WEAPON MODE\n");
			return AIBehavior::AttackResult::ReTryAttack; // сменили режим стрельбы, продолжить цикл атаки
		}

		// Выбрать другое подходящее оружие из инвентаря для атаки по цели
		fo::AttackType outHitMode;
		fo::GameObject* findWeapon = AIInventory::FindSafeWeaponAttack(source, target, handItem, outHitMode);
		if (findWeapon) {
			DEV_PRINTF("\n[AI] Attack result: SWITCH WEAPON\n");

			fo::func::inven_wield(source, findWeapon, fo::InvenType::INVEN_TYPE_RIGHT_HAND);

			hitMode = outHitMode;
			weapon = findWeapon;
			__asm call fo::funcoffs::combat_turn_run_;
			return AIBehavior::AttackResult::ReTryAttack;
		}
	}

	// не было найдено другого оружия для продолжения атаки, использовать рукопашную атаку если цель расположена достаточно близко
	if (dist > 0 && dist <= 2) {
		if (source->critter.getAP(dist) >= costAP) { // очков действия хватает чтобы сделать удар
			DEV_PRINTF("\n[AI] Attack result: UNARMED ATTACK\n");

			// проверить вероятность удара и очки жизней прежде чем подходить

			if (dist > 0 && AIHelpersExt::ForceMoveToTarget(source, target, dist) == -1) return AIBehavior::AttackResult::Default; // не удалось приблизиться к цели
			hitMode = fo::AttackType::ATKTYPE_PUNCH; // установить тип атаки "без оружия"
			//weapon = nullptr;                      // не делать это, иначе возможны баги в ванильной ai_try_attack
			return AIBehavior::AttackResult::ReTryAttack;
		}
	}
	return AIBehavior::AttackResult::Default; // выйти из ai_try_attack
}

static const uint32_t ai_try_attack_hack_GoNext_Ret = 0x42A9F2;

static void __declspec(naked) ai_try_attack_hack_after_attack() {
	__asm {
		test eax, eax; // -1 error attack
		jl   end;
		lea  eax, [esp + 0x364 - 0x38 + 4]; // hit_mode
		lea  ebx, [esp + 0x364 - 0x3C + 4]; // right_weapon
		push eax;
		mov  edx, ebp;
		mov  ecx, esi;
		push ebx;
		call AICheckResultAfterAttack;
		cmp  eax, 8; // AttackResult::NoMovePoints
		jg   lostWeapon;
		retn; // Default / TargetDead / NoMovePoints
lostWeapon:
		cmp  eax, 9; // AttackResult::LostWeapon;
		jne  reTryAttack;
		//lea  ebx, [esp + 0x364 - 0x3C + 4];  // right_weapon
		lea  edx, [esp + 0x364 - 0x38 + 4];    // hit_mode
		mov  ecx, ebp;                         // target
		mov  eax, esi;                         // source
		call fo::funcoffs::ai_switch_weapons_; // ebx - right_weapon
		test eax, eax;
		jge  reTryAttack;                      // -1 - оружие не было найдено
end:
		retn;
reTryAttack:
		add  esp, 4;
		jmp  ai_try_attack_hack_GoNext_Ret;
	}
}

/////////////////////////////////////////////////////////////////////////////////////////

// Событие перед атакой, когда шанс поразить цель является минимальным установленному значению 'min_to_hit' в AI.txt
static fo::GameObject* __fastcall AIBadToHit(fo::GameObject* source, fo::GameObject* target, fo::AttackType hitMode) {
	DEV_PRINTF2("\n[AI] BadToHit: %s attack bad to hit my (%s) target.\n", fo::func::critter_name(source), fo::func::critter_name(target));

	//TODO: попробовать сменить оружие

	AICombat::AttackerSetHitMode(hitMode);

	fo::GameObject* newTarget = AISearchTarget::AIDangerSource(source, 8); // попробовать найти другую цель со значеннием шанса выше 'min_to_hit'
	if (!newTarget || newTarget == target) {
		// новая цель не была найдена, проверяем боевой рейтинг цели
		fo::GameObject* lastTarget = fo::func::combatAIInfoGetLastTarget(target);
		if (lastTarget == source && fo::func::combatai_rating(target) > (fo::func::combatai_rating(source) * 2)) {
			source->critter.combatState |= fo::CombatStateFlag::ReTarget;
			return nullptr; // цель сильна - убегаем
		}
		DEV_PRINTF("\n[AI] BadToHit: No alternative target. Attack my target with bad to hit.\n");
		return (fo::GameObject*)-1; // продолжаем атаковать цель несмотря на низкий шанс
	}
	DEV_PRINTF1("\n[AI] BadToHit: Found alternative (%s) target.\n", fo::func::critter_name(newTarget));
	return newTarget;
}

static void __declspec(naked) ai_try_attack_hook_bad_tohit() {
	static const uint32_t ai_try_attack_hack_Attack_Ret0 = 0x42ABBB;
	static const uint32_t ai_try_attack_hack_Attack_Ret1 = 0x42AE09; // for no_range
	__asm {
		push [esp + 0x364 - 0x38 + 4]; // hit_mode
		mov  ecx, eax;
		call AIBadToHit;
		test eax, eax;
		jnz  reTarget;
		mov  edx, ebp;
		mov  eax, esi;
		jmp  fo::funcoffs::ai_run_away_;
reTarget:
		cmp  eax, -1;
		je   forceAttack;
		add  esp, 4;
		mov  ebp, eax;
		jmp  ai_try_attack_hack_GoNext_Ret;
forceAttack:
		pop  edx; // return addr
		mov  eax, ai_try_attack_hack_Attack_Ret0;
		cmp  edx, 0x42ACE5 + 5; // no_range
		cmove eax, ai_try_attack_hack_Attack_Ret1;
		jmp  eax;
	}
}

/////////////////////////////////////////////////////////////////////////////////////////

static fo::GameObject* MoveCritter(fo::GameObject* source, fo::GameObject* critter, long tile) {
	long dist = fo::func::obj_dist(source, critter);
	if (dist == 1) tile = source->tile; // направление к source
	long dir = fo::func::tile_dir(critter->tile, tile); // направление к гексу

	long moveTile = AIHelpersExt::GetFreeTile(critter, critter->tile, 1, dir);
	if (moveTile == -1)	return critter;

	if (AIHelpersExt::CombatMoveToTile(critter, moveTile, 1)) return critter; // error
	if (critter->critter.getAP()) critter->critter.movePoints--;

	DEV_PRINTF1("\n[AI] Path is сlear: Critter move: %s", fo::func::critter_name(critter));
	return nullptr;
}

static fo::GameObject* ClearMovePathSub(fo::GameObject* source, fo::GameObject* target, long &outTile) {
	uint16_t pathTiles[800]; // массив гексов

	// построить путь до цели игнорируя гексы союзных криттеров
	long pathLen = game::Tilemap::make_path_func(source, source->tile, target->tile, 1, 2000, pathTiles, 0, AIHelpersExt::obj_ai_move_blocking_at_);
	if (pathLen > 0) {
		// путь построен, ищем криттеров по гексам пути
		long tile = source->tile;

		long lastStep;
		fo::GameObject* lastCritter = nullptr;
		bool findBlock = false;

		long moveAP = source->critter.getMoveAP();
		if (pathLen > moveAP) pathLen = moveAP; // ограничиваем путь до текущего значения AP

		long stepPath;
		for (stepPath = 0; stepPath < pathLen; stepPath++)
		{
			tile = pathTiles[stepPath];
			fo::GameObject* obj = fo::func::obj_blocking_at(source, tile, source->elevation);
			if (obj) {
				if (lastCritter) {
					// проверить путь от атакующкго (source) до криттера (obj)
					if (game::Tilemap::make_path_func(source, source->tile, obj->tile, 0, 2000, 0, 0, (void*)fo::funcoffs::obj_blocking_at_) == 0) {
						// путь блокирован, передвинуть предыдущего криттера если у него имеется AP
						if (lastCritter->critter.getAP() > 0 && MoveCritter(source, lastCritter, pathTiles[lastStep - 1])) return lastCritter;
					}
				}
				lastCritter = obj; // запоминаем текущего криттера

				#ifndef NDEBUG
					obj->outline = 10 << 8; // grey
					fo::BoundRect rect;
					fo::func::obj_bound(obj, &rect);
					rect.x--;
					rect.y--;
					rect.offx += 2;
					rect.offy += 2;
					fo::func::tile_refresh_rect(&rect, obj->elevation);
				#endif

				// проверить свободен ли путь от него до цели
				if (game::Tilemap::make_path_func(obj, obj->tile, target->tile, 0, 2000, 0, 0, (void*)fo::funcoffs::obj_blocking_at_) > 0) {
					// путь свободен, значит только этот критер блокирует путь
					if (obj->critter.getAP() <= 0 || obj->critter.IsNotActive()) return obj; // криттер не активен или у него нет очков действий, возвращаем его как блокирующего путь
					findBlock = true;
					break;
				}
				// путь не свободен, блокирует кто-то еще
				// запомнить текущий шаг
				lastStep = stepPath;
			}
		}

		if (!findBlock && !lastCritter) {
			// не найдено блокирующего криттера и нет lastCritter, просто передвигаемся до последней плитки
			outTile = tile;
			return nullptr;
		}
		return (findBlock) ? MoveCritter(source, lastCritter, pathTiles[stepPath - 1]) : lastCritter; // перейти к последнему криттеру в пути
	}
	return nullptr;
}

static fo::GameObject* ClearMovePath(fo::GameObject* source, fo::GameObject* target, long &outTile) {
	fo::GameObject* object = nullptr;
	long count = 10;
	do {
		fo::GameObject* obj = ClearMovePathSub(source, target, outTile);
		if (!obj || obj == object) return obj;
		object = obj;
	} while (--count);

	return object;
}

// Доработанная и улучшенная реализация функции ai_move_steps_closer_ (с исправлениями)
// Функция принимает дополнительные флаги для игнорирования stay/stat_close
// flags:
//  0x00000001 - печатает сообщение (vanilla)
//	0x01000000 - игнорирует только stay_close
//  0x02000000 - игнорирует stay и stay_close
long __fastcall AIBehavior::AIMoveStepsCloser(long flags, fo::GameObject* target, fo::GameObject* source, long distance) {
	if (distance <= 0) return -1;

	// [ADD-EXT]
	bool ignoreStay = false;
	bool ignoreStayClose = false;
	// check move flags
	if (flags & 0x02000000) {
		ignoreStayClose = true;
		ignoreStay = true;
	}
	else if (flags & 0x01000000) {
		ignoreStayClose = true;
	}

	if (fo::func::obj_dist(source, target) <= 1) return -1;

	fo::AIpref::Distance prefDistance = fo::func::ai_cap(source)->distance;

	if (!ignoreStay && prefDistance == fo::AIpref::Distance::stay) {
		return -1;
	}

	if (!ignoreStayClose && prefDistance == fo::AIpref::Distance::stay_close) {
		// [ADD-EXT] stay_close - tweak for non party member NPC
		if (source->critter.teamNum != fo::var::obj_dude->critter.teamNum) {
			if (fo::func::obj_dist(source, target) > 5) return -1;
		}

		// Стоять на месте:
		// если target не игрок, и расстояние от игрока до source больше чем 5 гексов
		// или если расстояние между target и игроком более 5 гексов, и дистанция для передвижения привышает радиус в 5 гексов от игрока
		if (target != fo::var::obj_dude) {
			long scr_dude_dist = fo::func::obj_dist(source, fo::var::obj_dude);
			if (scr_dude_dist > 5 || fo::func::obj_dist(target, fo::var::obj_dude) > 5 && distance + scr_dude_dist > 5) {
				return -1;
			}
		}
	}

	fo::GameObject* sTarget = target;
	long destinationTile = target->tile;

	// unused code
	//bool multiHexIsMouse3d = false;
	//if (target->flags & fo::ObjectFlag::MultiHex) {
	//	target->flags |= fo::ObjectFlag::Mouse_3d;
	//	multiHexIsMouse3d = true;
	//}

	if (!fo::func::make_path_func(source, source->tile, target->tile, 0, 0, (void*)fo::funcoffs::obj_blocking_at_)) {
		// [ADD-EXT] Реализация функции освобождения пути криттера блокирующего путь к цели
		if (fo::func::critter_body_type(source) != fo::BodyType::Biped) goto block;

		long gotoTile = -1;
		fo::GameObject* object = ClearMovePath(source, target, gotoTile);
		if (object) {
			target = object;
			destinationTile = object->tile;
		}
		else if (gotoTile != -1) {
			destinationTile = gotoTile;
		}
		else {
	block:	// не удалось построить путь, найти ближайщего криттера
			fo::var::moveBlockObj = nullptr;
			if (!fo::func::make_path_func(source, source->tile, target->tile, 0, 0, (void*)fo::funcoffs::obj_ai_blocking_at_)
				&& fo::var::moveBlockObj && fo::var::moveBlockObj->IsCritter())
			{
				//if (multiHexIsMouse3d) target->flags ^= fo::ObjectFlag::Mouse_3d; // сбросить флаг

				target = fo::var::moveBlockObj;
				destinationTile = target->tile;

				// [FIX] Fixes for NPC stuck in an animation loop in combat when trying to move close to a multihex critter
				// this prevents moving to the multihex critter when the critters are close together
				if (fo::func::obj_dist(source, target) <= 1) return -1;

				//if (fo::var::moveBlockObj->flags & fo::ObjectFlag::MultiHex) {
				//	fo::var::moveBlockObj->flags |= fo::ObjectFlag::Mouse_3d;
				//	multiHexIsMouse3d = true;
				//} else {
				//	multiHexIsMouse3d = false;
				//}
				fo::func::debug_printf("\nai_move_steps_closer: can't move to target, move to near critter.");
			}
		}
	}
	//if (multiHexIsMouse3d) target->flags ^= fo::ObjectFlag::Mouse_3d; // сбросить флаг

	bool run = false;
	if (distance >= fo::func::stat_level(source, fo::Stat::STAT_max_move_points) / 2 && fo::func::artCritterFidShouldRun(source->artFid)) {
		run = true;
	}

	if (target == sTarget) fo::func::cai_retargetTileFromFriendlyFire(source, target, &destinationTile); // переопределяет гекс

	bool toTile = true;	// always move to tile
	if (target->flags & fo::ObjectFlag::MultiHex || source->flags & fo::ObjectFlag::MultiHex) { // [FIX] Fixes multihex critters moving too close and overlapping their targets in combat
		if (destinationTile == target->tile) toTile = false; // for move to object
	}

	fo::func::register_begin(fo::RB_RESERVED);
	// check say flag
	if (flags & 0x01) fo::func::combatai_msg(source, nullptr, 1, 0);

	if (toTile) {
		if (run) {
			fo::func::register_object_run_to_tile(source, destinationTile, source->elevation, distance, 0);
		} else {
			fo::func::register_object_move_to_tile(source, destinationTile, source->elevation, distance, 0);
		}
	} else {
		if (run) {
			fo::func::register_object_run_to_object(source, target, distance, 0);
		} else {
			fo::func::register_object_move_to_object(source, target, distance, 0);
		}
	}
	if (fo::func::register_end()) {
		fo::func::debug_printf("\n[ERROR] ai_move_steps_closer: failed register animation!");
		return -1;
	}
	__asm call fo::funcoffs::combat_turn_run_;

	// [FIX] Fixes for critters killed in combat still being able to move in their combat turn
	if (source->critter.IsNotActiveOrDead()) source->critter.movePoints = 0;

	return 0;
}

static void __declspec(naked) ai_move_steps_closer_replacement() {
	__asm {
		push ebx; // distance
		push eax; // source
		call AIBehavior::AIMoveStepsCloser;
		retn;
	}
}

/////////////////////////////////////////////////////////////////////////////////////////

// Принудительно использовать прицельную атаку c шансом 50% вместо 1/N, если цель атакующего находится в пределах 5-ти гексов
// TODO: переписать алгоритм без использование вторичного шанса
static void __declspec(naked) ai_called_shot_hook() {
	__asm {
		mov  ecx, edx;
		call fo::funcoffs::roll_random_;
		cmp  eax, 1;
		jne  distCheck;
		retn;
distCheck:
		cmp  ecx, 100; // cap.called_freq
		jg   skip;
		mov  edx, [esp + 0x1C - 0x14 + 4]; // target
		mov  eax, esi; // source
		call fo::funcoffs::obj_dist_;
		cmp  eax, 5;
		jg   skip; //setbe al;
		mov  eax, 1;
		lea  edx, [eax + 1];
		call fo::funcoffs::roll_random_; // 50%
skip:
		retn;
	}
}

void AIBehavior::init(bool smartBehavior) {

	// Реализация поиска предметов в трупах убитых NPC (looting corpses)
	LootingCorpses = sf::IniReader::GetConfigInt("CombatAI", "LootingCorpses", 1);
	if (LootingCorpses < 0 || LootingCorpses > 2) LootingCorpses = 1;

	AIInventory::init(LootingCorpses);

	// Реализация функции ai_move_steps_closer_ для освобождения пути криттера блокирующего путь к цели
	sf::MakeJump(fo::funcoffs::ai_move_steps_closer_, ai_move_steps_closer_replacement, 1); // 0x429FC8

	//////////////////// Combat AI improved behavior //////////////////////////

	AICombat::init(smartBehavior);
	AISearchTarget::init(smartBehavior);

	if (smartBehavior) {
		// Before starting his turn npc will always check if it has better weapons in inventory, than there is a current weapon
		LookupOnGround = (sf::IniReader::GetConfigInt("CombatAI", "TakeBetterWeapons", 0) > 0); // always check the items available on the ground

		sf::HookCall(0x42A6BF, ai_called_shot_hook);

		/**** Функция ai_try_attack_ ****/

		//Точки непосредственной атаки ai_attack_ (0x42AE1D, 0x42AE5C)

		// Точка проверки сделать атаку по цели, combat_check_bad_shot_ возвращается результат проверки (0-7)
		sf::HookCall(0x42A92F, ai_try_attack_hook_check_attack);

		// Точка смены оружия, если текущее оружие небезопасно для текущей ситуации
		// или найти оружие если NPC безоружен и цель не относится к типу Biped или цель вооружена и еще какие-то условия
		sf::HookCall(0x42A905, ai_try_attack_hook_w_switch_begin_turn);

		// Точка смены оружия, когда у NPC не хватает очков действия для совершения атаки
		// Switching weapons when action points is not enough
		sf::HookCall(0x42AB57, ai_try_attack_hook_switch_weapon); // old ai_try_attack_hook_switch_fix

		// Точка смены оружия, когда превышен радиус действия атаки и NPC безоружен
		//HookCall(0x42AC05, ai_try_attack_hook_switch_weapon_out_of_range);

		// Точка смены оружия, когда в оружие у NPC нет патронов и патроны не были найдены в инвентаре или на земле
		// перед этим происходит убирание текущего оружия из слота (возможно что эта точка не нужна будет)
		//HookCall(0x42AB3B, ai_try_attack_hook_switch_weapon_not_found_ammo);

		/// Точка входа, попытка найти патроны на земле, если они не были найдены в инвентаре
		///HookCall(0x42AA25, ai_try_attack_hook_); (занято для LootingCorpses)

		// Точка входа при блокировании выстрела по цели
		// Checks the movement path for the possibility а shot, if the shot to the target is blocked
		sf::HookCall(0x42AC55, ai_try_attack_hook_shot_blocked);

		// Точка входа когда дистанция превышает радиус атаки
		// Forces the AI to move to target closer to make an attack on the target when the distance exceeds the range of the weapon
		sf::HookCall(0x42ABD7, ai_try_attack_hook_out_of_range);

		// Точка: плохой шанс поразить цель (вызывается ai_run_away_) [переопределяется в AI.cpp]
		// Поведение попробовать сменить оружие или цель, если другой цели нет продолжить текущию атаку
		sf::HookCalls(ai_try_attack_hook_bad_tohit, { 0x42ACE5, 0x42ABA8 });

		// Мove away from the target if the target is near
		sf::MakeCalls(ai_try_attack_hack_after_attack, { 0x42AE40, 0x42AE7F });

		/***** Misc *****/

		// Поблажка для AI: Don't pickup a weapon if its magazine is empty and there are no ammo for it
		sf::HookCall(0x429CF2, ai_search_environ_hook_weapon);

		// Fix distance in ai_find_friend_ function (only for sfall extended)
		sf::SafeWrite8(0x428AF5, 0xC8); // cmp ecx, eax > cmp eax, ecx
	}

	// Fixes a rare situation for an NPC when an attacking NPC (unarmed or armed with a melee weapon)
	// wants to pickup a weapon placed on the map, but to perform actions, he will not have enough AP to pickup this item,
	// after the next turn, he will attack the player's
	// This will prevent an attempt to pickup the weapon at the beginning of the attack if there is not enough AP
	if (sf::IniReader::GetConfigInt("CombatAI", "ItemPickUpFix", 0)) {
		sf::HookCall(0x429CAF, ai_search_environ_hook);
	}

	// Замена отладочного сообшения "In the way!"
	sf::SafeWrite32(0x42A467, (DWORD)&retargetTileMsg); // cai_retargetTileFromFriendlyFireSubFunc_
}

}
}
