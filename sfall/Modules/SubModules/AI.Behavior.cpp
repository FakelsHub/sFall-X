/*
 *    sfall
 *    Copyright (C) 2020  The sfall team
 *
 */

#include "..\..\FalloutEngine\Fallout2.h"

#include "..\..\main.h"
#include "..\..\Utils.h"

#include "..\..\Game\items.h"
#include "..\..\Game\objects.h"

#include "..\AI.h"
#include "AI.FuncHelpers.h"
#include "AI.Combat.h"
#include "AI.SearchTarget.h"

#include "AI.Behavior.h"

/*
	Distance:
	stay        - ограниченное передвижение в бою, атакующий будет по возможности оставаться на том гексе где начал бой, все передвижения запрещены, кроме побега с поле боя
	stay_close  - держаться рядом, атакующий будет держаться на дистанции не превышающей 5 гексов от игрока (применяется только для напарников).
	charge      - атакующее поведение, AI будет пытаться всегда приблизиться к своей цели [перед или же] после атаки.
	snipe       - атака с расстояния, атакующий займет выжидающую позицию с которой будет атаковать, при сокращении дистанции между атакующим и целью, атакующий попытается отойти от цели на дистанцию до 10 гексов.
	on_your_own - специального поведение для этого не определено.

	Disposition:
	coward      -
	defensive   -
	aggressive  -
	berserk     -
*/

namespace sfall
{

constexpr int pickupCostAP = 3; // engine default cost

// Доработка функции ai_move_steps_closer_ которая принимает флаги в параметре дистанции для игнорирования stay/stat_close
// параметр дистанции при этом должен передаваться в инвертированном значении
// flags:
//	0x01000000 - игнорирует только stay_close
//  0x02000000 - игнорирует stay и stay_close
static void __declspec(naked) ai_move_steps_closer_hook() {
	static DWORD ai_move_steps_closer_MoveRet  = 0x42A02F;
	static DWORD ai_move_steps_closer_ErrorRet = 0x42A1B1;
	__asm {
		jz   badDistance;
		not  ebx;              // здесь значение дистанции в отрицательном значении
		mov  ebp, ebx;         // restored the distance value
		and  ebp, ~0x0F000000; // unset flags
		test ebx, 0x02000000;  // check move flag
		jz   stayClose;        // неустановлен бит
		jmp  ai_move_steps_closer_MoveRet;

stayClose:
		test ebx, 0x01000000;  // check move flag
		jz   badDistance;      // неустановлен бит
		call fo::funcoffs::ai_cap_;
		cmp  [eax + 0xA0], 4;  // cap.distance
		je   badDistance;      // это stay
		jmp  ai_move_steps_closer_MoveRet;

badDistance:
		jmp  ai_move_steps_closer_ErrorRet;
	}
}

/////////////////////////////////////////////////////////////////////////////////////////

// Исправляет редкую ситуацию для NPC, когда атакующий NPC (безоружный или вооруженный ближнем оружием)
// захочет подобрать оружие лежащее на карте, но для совершения действий у него не будет хватать AP,
// чтобы подобрать этот предмет, после на следующем ходу он будет атаковать игрока.
// Это предотвратит в начале атаки попытку поднять оружие при недостаточном количестве AP.
static long  __fastcall AIPickupWeaponFix(fo::GameObject* critter, long distance) {
	long maxAP = fo::func::stat_level(critter, fo::STAT_max_move_points);
	long currAP = critter->critter.getAP();
	if (currAP != (maxAP + AICombat::AttackerBonusAP())) return 1; // NPC already used their AP?

	bool allowPickUp = ((currAP - pickupCostAP) >= distance); // distance & AP check
	if (!allowPickUp) {
		// если NPC имеет стрелковое или метательное оружие, тогда позволено подойти к предмету
		fo::GameObject* item = fo::func::inven_right_hand(critter);
		if (item && AIHelpers::IsGunOrThrowingWeapon(item)) return 1; // allow pickup
	}
	return (allowPickUp) ? 1 : 0; // false - next item
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

// Неподбирать оружие если у него пустой магазин, и к нему нет патронов в инвентаре или на земле
static long __fastcall AICheckAmmo(fo::GameObject* weapon, fo::GameObject* critter) {
	if (fo::func::ai_can_use_weapon(critter, weapon, fo::AttackType::ATKTYPE_RWEAPON_PRIMARY)) return 1;
	if (weapon->item.charges > 0 || weapon->item.ammoPid == -1) return 1;

	//TODO: если оружие расположено близко то подобрать

	if (AIHelpers::CritterHaveAmmo(critter, weapon)) return 1;

	// check ammo on ground
	uint32_t result = 0;
	long maxDist = fo::func::stat_level(critter, fo::Stat::STAT_pe) + 5;
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
	return result; // 0 - critter no have ammo
}

static void __declspec(naked) ai_search_environ_hook_weapon() {
	__asm {
		push ecx;
		mov  ecx, esi;    // source
		call AICheckAmmo; // edx - weapon
		pop  ecx;
		retn;
	}
}

/////////////////////////////////////////////////////////////////////////////////////////

// Проверки при попытке сменить оружие, если у NPC не хватает очков действия для совершения атаки
static long __fastcall AICheckBeforeWeaponSwitch(fo::GameObject* target, long &hitMode, fo::GameObject* source, fo::GameObject* weapon) {
	DEV_PRINTF("\n[AI] ai_try_attack: not AP's for shoot.");

	if (source->critter.getAP() <= 0) return -1; // exit from ai_try_attack_
	//if (!weapon) return 1; // no weapon in hand slot, call ai_switch_weapons_ отключено для теста

	if (weapon) {
		long _hitMode = fo::func::ai_pick_hit_mode(source, weapon, target);
		if (_hitMode != hitMode) {
			hitMode = _hitMode;
			DEV_PRINTF("\n[AI] -> switch hit mode.");
			return 0; // сменили режим стрельбы, продолжить цикл атаки
		}
	}
	fo::GameObject* item = fo::func::ai_search_inven_weap(source, 1, target); // поиск с учетом AP
	if (!item) return 1; // no weapon in inventory, continue to search weapon on the map (call ai_switch_weapons_)

	// оружие является ближнего действия?
	long wType = fo::func::item_w_subtype(item, fo::AttackType::ATKTYPE_RWEAPON_PRIMARY);
	if (wType <= fo::AttackSubType::MELEE) { // unarmed and melee weapon, check the distance before switching
		if (AIHelpers::AttackInRange(source, item, fo::func::obj_dist(source, target)) == false) return -1; // цель долеко, выйти из ai_try_attack_
	}
	return 1; // выполнить ванильное поведение функции ai_switch_weapons_
}

static void __declspec(naked) ai_try_attack_hook_switch_weapon() {
	__asm {
		push edx;
		push [ebx]; // weapon  (push dword ptr [esp + 0x364 - 0x3C + 8];)
		push esi;   // source
		call AICheckBeforeWeaponSwitch; // ecx - target; edx - hit mode
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

//static fo::GameObject* sf_check_block_line_of_fire(fo::GameObject* source, long destTile) {
//	fo::GameObject* object = nullptr; // check the line of fire from target to checkTile
//
//	fo::func::make_straight_path_func(source, source->tile, destTile, 0, (DWORD*)&object, 32, (void*)fo::funcoffs::obj_shoot_blocking_at_);
//	if (object) {
//		//if (object->tile == destTile) return nullptr; // объект расположен на гексе назначения, это может быть дверь или другой проходимый объект
//		if (object->TypeFid() == fo::OBJ_TYPE_CRITTER) object = sf_check_block_line_of_fire(object, destTile);
//	}
//	return object;
//}

// Функция попытается найти свободный гекс для совершения выстрела AI по цели, в случаях когда цель для AI заблокирована для выстрела каким либо объектом
// если свободный гекс для встрела не будет найден то выполнится действие по умолчанию для функции ai_move_steps_closer
// TODO: Необходимо улучшить алгоритм для поиска гекса для совершения выстрела, для снайперов должна быть применена другая тактика
// Добавить учет открывание двери при построении пути
static int32_t __fastcall AISearchTileForShoot(fo::GameObject* source, fo::GameObject* target, int32_t &hitMode) {
	long distance, shotTile = 0;

	fo::GameObject* itemHand = fo::func::inven_right_hand(source);
	if (!itemHand || fo::func::item_w_subtype(itemHand, hitMode) <= fo::MELEE) {
		return 0;
	}

	long ap = source->critter.getAP();
	long cost = game::Items::item_w_mp_cost(source, hitMode, 0);
	ap -= cost; // left ap for distance move
	if (ap <= 0) return 0;

	//if (ap <= cost) return 0;
	//// вычисляем количество выстрелов которые сможет сделать NPC за ход
	//long shots = ap / cost;     // 10 / 5 = 2
	//long freeMove = ap % cost;  // 0
	//if (freeMove == 0) {
	//	ap -= cost * (shots - 1); // left ap for distance move
	//} else {
	//	ap = freeMove;
	//}

	DEV_PRINTF1("\n[AI] Search tile for shoot: distance %d", ap);

	char rotationData[800];
	long pathLength = fo::func::make_path_func(source, source->tile, target->tile, rotationData, 0, (void*)fo::funcoffs::obj_blocking_at_);
	if (pathLength > ap) pathLength = ap;

	long checkTile = source->tile;
	for (int i = 0; i < pathLength; i++)
	{
		checkTile = fo::func::tile_num_in_direction(checkTile, rotationData[i], 1);
		DEV_PRINTF1("\n[AI] Search tile for shoot: path tile %d", checkTile);

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
		//if (!shotTile) {
/*checkDoor:  // учитываем двери которые NPC открывает при проходе (реализовать позже вместе с правкой движка т.к. критеры не учитываю заблокированные гексы дверей)
			long objType = object->TypeFid();
			if (objType == ObjType::OBJ_TYPE_SCENERY) {
				if (object->tile != checkTile) continue; // объект находятся на пути прохода

				fo::Proto* proto = fo::GetProto(object->protoId);
				if (proto->scenery.type != ScenerySubType::DOOR) continue; // это не двери

				// проверить простреливается ли цель от дверей
				if (sf_check_block_line_of_fire(target, object->tile)) continue;

				// теперь NPC встанет в проеме дверей, что не есть хорошо!!!
				// смотрим есть ли еще ходы в запасе?
				//if (i + 1 < ap) continue; // есть
				goto getTile;
			}
			else if (objType != ObjType::OBJ_TYPE_CRITTER) continue;

			object = sf_check_block_line_of_fire(object, checkTile); // проверяем простреливается ли путь за критером
			if (object) goto checkDoor;
getTile:*/                                                        // проверяем простреливается ли путь за критером
		if (!shotTile) {
			if (object->TypeFid() != fo::ObjType::OBJ_TYPE_CRITTER || fo::func::combat_is_shot_blocked(object, object->tile, checkTile, 0, 0)) continue; //sf_check_block_line_of_fire(object, checkTile)
			shotTile = checkTile;
			distance = i + 1;
			DEV_PRINTF2("\n[AI] Get friendly fire shot tile:%d, Dist:%d", checkTile, distance);
		}
	}
	if (shotTile && ap > distance) {
		fo::AIcap* cap = fo::func::ai_cap(source);
		if (cap->distance != fo::AIpref::distance::snipe) { // оставляем AP для поведения "Snipe"
			//проверить шанс попадания по цели если он хороший то выход из цикла, нет необходимости подбегать близко

			long leftAP = (ap - distance) % cost; // оставшиеся AP после подхода и выстрела
			// spend left APs
			long newTile = checkTile = shotTile;
			long dist;
			for (int i = distance; i < pathLength; i++) // начинаем со следующего тайла и идем до конца пути
			{
				checkTile = fo::func::tile_num_in_direction(checkTile, rotationData[i], 1);
				DEV_PRINTF1("\n[AI] Search tile for shoot: path extra tile %d", checkTile);

				fo::GameObject* object = nullptr; // check the line of fire from target to checkTile
				fo::func::make_straight_path_func(target, target->tile, checkTile, 0, (DWORD*)&object, 32, (void*)fo::funcoffs::obj_shoot_blocking_at_);
				if (!AI::CheckShootAndTeamCritterOnLineOfFire(object, checkTile, source->critter.teamNum)) { // if there are no friendly critters
					//проверить шанс попадания по цели если он хороший то выход из цикла, нет необходимости подбегать близко
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

	int result = (shotTile && AIHelpers::CombatMoveToTile(source, shotTile, distance) == 0) ? 1 : 0;
	if (result) hitMode = fo::func::ai_pick_hit_mode(source, itemHand, target); // try pick new weapon mode after step move

	return result;
}

static void __declspec(naked) ai_try_attack_hook_shot_blocked() {
	__asm {
		pushadc;
		mov  ecx, eax;             // source
		lea  eax, [esp + 0x364 - 0x38 + 12 + 4];
		push eax;                  // hit mode
		call AISearchTileForShoot; // edx - target
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

static fo::GameObject* __stdcall AISearchBestWeaponOnGround(fo::GameObject* source, fo::GameObject* item, fo::GameObject* target) {
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
				if (fo::func::obj_dist(source, itemGround) > source->critter.getAP() + 1) break;
				// check real path distance
				int toDistObject = fo::func::make_path_func(source, source->tile, itemGround->tile, 0, 0, (void*)fo::funcoffs::obj_blocking_at_);
				if (toDistObject > source->critter.getAP() + 1) continue;

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

static bool LookupOnGround = false;
static bool LookupIntoContainers = false;

// Поиск наилучшего оружия перед совершением атаки (в первом цикле ai_try_attack_)
// Атакующий попытается найти лучшее оружие в своем инвентаре или подобрать близлежащее на земле оружие
// TODO: Добавить поддержку осматривать контейнеры/трупы на наличие в них оружия
// Executed once when the NPC starts attacking
static fo::AttackType AISearchBestWeaponOnFirstAttack(fo::GameObject* source, fo::GameObject* target, fo::GameObject* &weapon, fo::AttackType hitMode) {

	fo::GameObject* itemHand   = fo::func::inven_right_hand(source); // current item
	fo::GameObject* bestWeapon = itemHand;

	DEV_PRINTF1("\n[AI] HandPid: %d", (itemHand) ? itemHand->protoId : -1);

	DWORD slotNum = -1;
	while (true)
	{
		fo::GameObject* item = fo::func::inven_find_type(source, fo::item_type_weapon, &slotNum);
		if (!item) break;
		if (itemHand && itemHand->protoId == item->protoId) continue;

		if ((source->critter.getAP() >= fo::func::item_w_primary_mp_cost(item)) &&
			fo::func::ai_can_use_weapon(source, item, fo::AttackType::ATKTYPE_RWEAPON_PRIMARY))
		{
			if (item->item.ammoPid == -1 || // оружие не имеет патронов
				fo::func::item_w_subtype(item, fo::AttackType::ATKTYPE_RWEAPON_PRIMARY) == fo::AttackSubType::THROWING || // Зачем здесь метательные?
				(fo::func::item_w_curr_ammo(item) || AIHelpers::CritterHaveAmmo(source, item)))
			{
				if (!fo::func::combat_safety_invalidate_weapon_func(source, item, fo::AttackType::ATKTYPE_RWEAPON_PRIMARY, target, 0, 0)) { // weapon safety
					bestWeapon = fo::func::ai_best_weapon(source, bestWeapon, item, target);
				}
			}
		}
	}
	if (bestWeapon != itemHand) DEV_PRINTF1("\n[AI] Find best weapon Pid: %d", (bestWeapon) ? bestWeapon->protoId : -1);

	// выбрать лучшее на основе навыка
	if (itemHand != bestWeapon)	bestWeapon = AIHelpers::AICheckWeaponSkill(source, itemHand, bestWeapon);

	if ((LookupOnGround && !fo::func::critterIsOverloaded(source)) && source->critter.getAP() >= 3 && fo::func::critter_body_type(source) == fo::BodyType::Biped) {

		// построить путь до цели (зачем?)
		int toDistTarget = fo::func::make_path_func(source, source->tile, target->tile, 0, 0, (void*)fo::funcoffs::obj_blocking_at_);
		if ((source->critter.getAP() - 3) >= toDistTarget) goto notRetrieve; // не поднимать, если у NPC хватает очков сделать удар по цели

		fo::GameObject* itemGround = AISearchBestWeaponOnGround(source, bestWeapon, target);

		DEV_PRINTF1("\n[AI] BestWeapon on ground. Pid: %d", (itemGround) ? itemGround->protoId : -1);

		if (itemGround != bestWeapon) {
			if (itemGround && (!bestWeapon || itemGround->protoId != bestWeapon->protoId)) {

				if (bestWeapon && fo::func::item_cost(itemGround) < fo::func::item_cost(bestWeapon) + 50) goto notRetrieve;

				fo::GameObject* item = AIHelpers::AICheckWeaponSkill(source, bestWeapon, itemGround);
				if (item != itemGround) goto notRetrieve;

				fo::func::dev_printf("\n[AI] TryRetrievePid: %d AP: %d", itemGround->protoId, source->critter.getAP());

				fo::GameObject* itemRetrieve = fo::func::ai_retrieve_object(source, itemGround); // pickup item

				DEV_PRINTF2("\n[AI] PickupPid: %d AP: %d", (itemRetrieve) ? itemRetrieve->protoId : -1, source->critter.getAP());

				if (itemRetrieve && itemRetrieve->protoId == itemGround->protoId) {
					// if there is not enough action points to use the weapon, then just pick up this item
					bestWeapon = (source->critter.getAP() >= fo::func::item_w_primary_mp_cost(itemRetrieve)) ? itemRetrieve : nullptr;
				}
			}
		}
	}
notRetrieve:
	DEV_PRINTF2("\n[AI] BestWeapon Pid: %d AP: %d", ((bestWeapon) ? bestWeapon->protoId : 0), source->critter.getAP());

	if (bestWeapon && (!itemHand || itemHand->protoId != bestWeapon->protoId)) {
		weapon = bestWeapon;
		hitMode = (fo::AttackType)fo::func::ai_pick_hit_mode(source, bestWeapon, target);
		fo::func::inven_wield(source, bestWeapon, fo::InvenType::INVEN_TYPE_RIGHT_HAND);
		__asm call fo::funcoffs::combat_turn_run_;
		if (isDebug) fo::func::debug_printf("\n[AI] Wield best weapon pid: %d AP: %d", bestWeapon->protoId, source->critter.getAP());
	}
	return hitMode;
}

static void PrintShootResult(long result) {
	const char* type;
	switch (result) {
		case 0: type = "0 [OK]"; break;
		case 1: type = "1 [No Ammo]"; break;
		case 2: type = "2 [Out of Range]"; break;
		case 3: type = "3 [No Action Point]"; break;
		case 4: type = "4 [Target dead]"; break;
		case 5: type = "5 [Shot Blocked]"; break;
		default:
			fo::func::debug_printf("\n[AI] Check bad shot result: %d.", result);
			return;
	}
	fo::func::debug_printf("\n[AI] Check bad shot result: %s.", type);
}

static bool weaponIsSwitched = false; // true - смена оружия уже была произведена

static long __fastcall AICheckAttack(fo::GameObject* &weapon, fo::GameObject* target, fo::GameObject* source, fo::AttackType &hitMode, long attackCounter, long safetyRange) {
	// safetyRange: значение дистанции для перестройки во время выполнения атаки для оружия по площади

	if (attackCounter == 0 && safetyRange == 0 && !weaponIsSwitched) {
		hitMode = AISearchBestWeaponOnFirstAttack(source, target, weapon, hitMode);
	}
	weaponIsSwitched = false;

	long dist = fo::func::obj_dist(source, target) - 1;
	if (attackCounter == 0 && hitMode == fo::AttackType::ATKTYPE_RWEAPON_PRIMARY && dist <= 3) {
		// атакующий расположен достаточно близко к цели, его оружие имеет стрельбу очередью (burst attack)
		// принудительно использовать втоичный режим стрельбы если атака по цели будет безопасной
		if (fo::func::item_w_anim_weap(weapon, fo::AttackType::ATKTYPE_RWEAPON_SECONDARY) == fo::Animation::ANIM_fire_burst &&
			!fo::func::combat_safety_invalidate_weapon_func(source, weapon, fo::AttackType::ATKTYPE_RWEAPON_SECONDARY, target, 0, 0)) // weapon is safety
		{
			hitMode = fo::AttackType::ATKTYPE_RWEAPON_SECONDARY; // TODO: скриптово запрещается испоьзование вторичного режима
			DEV_PRINTF("\n[AI] Force use burst attack.");
		}
	}
	long result = fo::func::combat_check_bad_shot(source, target, hitMode, 0);

	#ifndef NDEBUG
	PrintShootResult(result);
	#endif

	switch (result)
	{
		case 0: // [OK]
			if (hitMode == fo::AttackType::ATKTYPE_RWEAPON_SECONDARY && safetyRange == 0) {
				// используется вторичный режим оружия (стрельба очередью) и имеется расстояние между целью и атакующим
				// если атакующему хватате очков действия на подход к цели то выполнить передвижение к цели
				if (dist >= 1 && fo::func::ai_cap(source)->distance != fo::AIpref::distance::snipe) {
					if (fo::func::item_w_anim_weap(weapon, hitMode) == fo::Animation::ANIM_fire_burst) {
						long costAP = game::Items::item_w_mp_cost(weapon, hitMode, 0);
						long moveDist = source->critter.getAP() - costAP;
						if (moveDist > 0) {
							if (moveDist >= costAP && fo::func::determine_to_hit(source, target, fo::BodyParts::Uncalled, hitMode) >= 70) {
								// если процент попадания по цели достатояно высокий, уменьшить дистанцию передвижения, чтобы хватило на несколько атак
								while (moveDist >= costAP) moveDist -= costAP;
							}
							if (moveDist > 0) AIHelpers::MoveToTarget(source, target, moveDist);
						}
					}
				}
			}
			break;
		case 1:	break; // [No Ammo]
		case 2:	break; // [Out of Range]
		case 3:	break; // [No Action Point]
		case 4:	break; // [Target dead]
		case 5:	break; // [Shot Blocked]
	}
	return result;
}

static void __declspec(naked) ai_try_attack_hook_check_attack() {
	__asm {
		mov  eax, [esp + 0x364 - 0x44 + 4]; // safety_range
		lea  ebx, [esp + 0x364 - 0x38 + 4]; // hit_mode ref
		lea  ecx, [esp + 0x364 - 0x3C + 4]; // right_weapon ref
		push eax;
		push edi;                           // attack_counter
		push ebx;
		push esi;                           // source
		call AICheckAttack;                 // edx - target
		cmp  eax, 4;                        // возвращает результат 4 когда криттер умирает в цикле хода
		je   targetIsDead;
		retn;
targetIsDead:
		mov  edi, 10;                               // set end loop
		mov  dword ptr [esp + 0x364 - 0x30 + 4], 1; // set result value for TargetDead
		retn;
	}
}

static void __declspec(naked) ai_try_attack_hook_switch() {
	__asm {
		mov weaponIsSwitched, 1;
		jmp fo::funcoffs::ai_switch_weapons_;
	}
}

/////////////////////////////////////////////////////////////////////////////////////////

//static void __declspec(naked) ai_try_attack_hack_move() {
//	__asm {
//		mov  eax, [esi + movePoints];
//		test eax, eax;
//		jz   noMovePoint;
//		mov  eax, dword ptr [esp + 0x364 - 0x3C + 4]; // right_weapon
//		push [esp + 0x364 - 0x38 + 4]; // hit_mode
//		mov  edx, ebp;
//		mov  ecx, esi;
//		push eax;
//		call GetMoveAwayDistaceFromTarget;
//noMovePoint:
//		mov  eax, -1;
//		retn;
//	}
//}

/////////////////////////////////////////////////////////////////////////////////////////

// Заставляет NPC двигаться ближе к цели, чтобы начать атаковать, когда расстояние до цели превышает дальность действия оружия
static long __fastcall AIMoveStepToAttackTile(fo::GameObject* source, fo::GameObject* target, long &outHitMode) {
	if (fo::func::ai_cap(source)->distance == fo::AIpref::distance::stay) return 1;

	fo::GameObject* itemHand = fo::func::inven_right_hand(source);
	if (!itemHand) return 1;

	long hitMode = outHitMode; //fo::func::ai_pick_hit_mode(source, itemHand, target);

	// check the distance and number of remaining AP's
	long weaponRange = fo::func::item_w_range(source, hitMode);
	if (weaponRange <= 1) return 1;

	long cost = game::Items::item_w_mp_cost(source, hitMode, 0);
	long distToMove = fo::func::obj_dist(source, target) - weaponRange; // required approach distance

	long ap = source->critter.getAP() - distToMove; // subtract the number of action points to the move, leaving the number for the shot
	long remainingAP = ap % cost;             // оставшиеся очки действия после атаки

	bool notEnoughAP = (cost > ap); // check whether the critter has enough AP to perform the attack

	char rotationData[800];
	long pathLength = fo::func::make_path_func(source, source->tile, target->tile, rotationData, 0, (void*)fo::funcoffs::obj_blocking_at_);

	long getTile = -1;

	if (pathLength > 0) {          // путь до цели построен успешно
		if (notEnoughAP) return 1; // но нехватает очков действия для совершения атаки

		getTile = source->tile;

		// get tile to perform an attack
		for (long i = 0; i < distToMove; i++)	{
			getTile = fo::func::tile_num_in_direction(getTile, rotationData[i], 1);
		}

		// проверяем вероятность поразить цель, если более 50% то атакуем с гекса, иначе двигаемя до конца дистанции
		if (fo::func::determine_to_hit_from_tile(source, getTile, target, fo::BodyParts::Uncalled, hitMode) < 50) {
			long start = distToMove;
			// подходим на все имеющиеся AP
			distToMove += remainingAP; // add remaining AP's to distance
			if (distToMove > pathLength) distToMove = pathLength;

			for (long i = start; i < distToMove; i++)	{
				getTile = fo::func::tile_num_in_direction(getTile, rotationData[i], 1);
			}
		}
		DEV_PRINTF3("\nAIMoveStepToAttackTile: distToMove/pathLength: %d/%d, distTile: %d", distToMove, pathLength, fo::func::tile_dist(source->tile, getTile));
	}
	else if (!notEnoughAP) {
		// AP достаточно для подхода и совершения атакит, но путь до цели построить не удалось
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
		DEV_PRINTF3("\nAIMoveStepToAttackTile: distToMove/pathLength: %d/%d, distTile: %d", distToMove, pathLength, fo::func::tile_dist(source->tile, getTile));
		distToMove = pathLength;
	}

	// убедитесь, что расстояние находится в пределах досягаемости оружия и атака не заблокирована
	if (getTile != -1 && (fo::func::obj_dist_with_tile(source, getTile, target, target->tile) > weaponRange ||
		fo::func::combat_is_shot_blocked(source, getTile, target->tile, target, 0)))
	{
		return 1;
	}

	if (getTile != -1 && isDebug) fo::func::debug_printf("\n[AI] %s: Attack out of range. Move to tile for attack.", fo::func::critter_name(source));

	int result = (getTile != -1) ? AIHelpers::CombatMoveToTile(source, getTile, distToMove) : 1;
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
		jnz  defaultMove;
		retn;
defaultMove:
		mov  edx, ebp;
		mov  eax, esi;
		jmp  fo::funcoffs::ai_move_steps_closer_; // default behavior
	}
}

// Выбрать другое подходящее оружие из инвентаря для атаки по цели, когда не хватает очков действия
static fo::GameObject* FindSafeWeaponAttack(fo::GameObject* source, fo::GameObject* target, fo::GameObject* hWeapon) {
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

		if (game::Items::item_w_mp_cost(source, fo::AttackType::ATKTYPE_RWEAPON_PRIMARY, 0) <= source->critter.getAP() &&
			(fo::func::ai_can_use_weapon(source, item, fo::AttackType::ATKTYPE_RWEAPON_PRIMARY) ||
			fo::func::ai_can_use_weapon(source, item, fo::AttackType::ATKTYPE_LWEAPON_SECONDARY)))
		{
			if ((item->item.ammoPid == -1 || // оружие не имеет магазина для патронов
				fo::func::item_w_curr_ammo(item)) &&
				!fo::func::combat_safety_invalidate_weapon_func(source, pickWeapon, fo::AttackType::ATKTYPE_RWEAPON_PRIMARY, target, 0, 0)) // weapon is safety
			{
				pickWeapon = fo::func::ai_best_weapon(source, pickWeapon, item, target);
			}
		}
	}
	return pickWeapon;
}

// Анализируем ситуацию после атаки когда не хватает AP для продолжения атаки
static AIBehavior::AttackResult __fastcall AICheckResultAfterAttack(fo::GameObject* source, fo::GameObject* target, fo::GameObject* &weapon, long &hitMode) {

	if (source->critter.getAP() <= 0) return AIBehavior::AttackResult::NoMovePoints;

	// цель мертва после атаки, нужно взять другую цель (аналог поведения включенной NPCsTryToSpendExtraAP опции)
	if (target->critter.IsDead()) return AIBehavior::AttackResult::TargetDead;

	long dist = fo::func::obj_dist(source, target) - 1;
	if (dist < 0) dist = 0;
	DEV_PRINTF1("\n[AI] Attack result: distance to target: %d", dist);

	long costAP = game::Items::item_w_mp_cost(source, fo::AttackType::ATKTYPE_PUNCH, 0);
	fo::GameObject* handItem = fo::func::inven_right_hand(source);

	// условие когда атакующий потерял оружие после атаки
	if (weapon && !handItem) {
		DEV_PRINTF("\n[AI] Attack result: LOST WEAPON\n");
		// проверить дистанцию до цели, если дистанция позволяет то использовать рукопашную атаку
		// иначе подобрать оружие или достать другое из инвентаря (при этом необходимо вычесть 3 AP)
		if (dist >= 3 || ((dist + costAP) > source->critter.getAP())) return AIBehavior::AttackResult::LostWeapon;
	}

	// еще остались очки действий, но не хватает для атаки, попробовать найти подходящее оружие c меньшим AP в инвентаре и повторить атаку
	if (handItem) {
		long _hitMode = fo::func::ai_pick_hit_mode(source, handItem, target);
		if (_hitMode != hitMode) {
			hitMode = _hitMode;
			DEV_PRINTF("\n[AI] Attack result: SWITCH WEAPON MODE\n");
			return AIBehavior::AttackResult::ReTryAttack; // сменили режим стрельбы, продолжить цикл атаки
		}

		fo::GameObject* findWeapon = FindSafeWeaponAttack(source, target, handItem);
		if (findWeapon) {
			DEV_PRINTF("\n[AI] Attack result: SWITCH WEAPON\n");
			long _hitMode = fo::func::ai_pick_hit_mode(source, findWeapon, target);
			//if (source->critter.getAP() >= game::Items::item_weapon_mp_cost(source, findWeapon, _hitMode, 0)) {

				fo::func::inven_wield(source, findWeapon, fo::InvenType::INVEN_TYPE_RIGHT_HAND);
				__asm call fo::funcoffs::combat_turn_run_;
				hitMode = _hitMode;
				weapon = findWeapon;

				return AIBehavior::AttackResult::ReTryAttack;
			//}
		}
	}

	// не было найдено другого оружия для продолжения атаки, использовать рукопашную атаку если цель расположена достаточно близко
	if (dist > 0 && dist <= 3) { //
		if (source->critter.getAP() >= (dist + costAP)) { // очков действия хватает чтобы сделать удар
			DEV_PRINTF("\n[AI] Attack result: UNARMED ATTACK\n");

			// проверить веротность удара и очки жизней прежде чем подходить

			if (dist > 0 && AIHelpers::ForceMoveToTarget(source, target, dist) == -1) return AIBehavior::AttackResult::Default; // не удалось приблизиться к цели
			hitMode = fo::AttackType::ATKTYPE_PUNCH; // установить тип атаки
			weapon = nullptr;                    // без оружия
			return AIBehavior::AttackResult::ReTryAttack;
		}
	}
	return AIBehavior::AttackResult::Default;
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
		cmp  eax, 2;
		jg   LostWeapon;
		retn; // Default / TargetDead / NoMovePoints
LostWeapon:
		cmp  eax, 3;
		jne  ReTryAttack;
		//lea  ebx, [esp + 0x364 - 0x3C + 4];  // right_weapon
		lea  edx, [esp + 0x364 - 0x38 + 4];    // hit_mode
		mov  ecx, ebp;                         // target
		mov  eax, esi;                         // source
		call fo::funcoffs::ai_switch_weapons_; // ebx - right_weapon
		test eax, eax;
		jge  ReTryAttack;                      // -1 - оружие не было найдено
end:
		retn;
ReTryAttack:
		add  esp, 4;
		jmp  ai_try_attack_hack_GoNext_Ret;
	}
}

static fo::GameObject* __fastcall AIBadToHit(fo::GameObject* source, fo::GameObject* target, fo::AttackType hitMode) {
	//TODO: попробовать сменить оружие

	AICombat::AttackerSetHitMode(hitMode);

	fo::GameObject* newTarget = AISearchTarget::AIDangerSource(source, 8);
	if (!newTarget || newTarget == target) {
		if (fo::func::combatai_rating(target) > (fo::func::combatai_rating(source) * 2)) {
			source->critter.combatState |= fo::CombatStateFlag::ReTarget;
			return nullptr;
		}
		return (fo::GameObject*)-1;
	}
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

void AIBehavior::init() {

	// Fix distance in ai_find_friend_ function (only for sfall extended)
	SafeWrite8(0x428AF5, 0xC8); // cmp ecx, eax > cmp eax, ecx

	bool smartBehaviorEnabled = (GetConfigInt("CombatAI", "SmartBehavior", 0) > 0);
	AICombat::init(smartBehaviorEnabled);
	AISearchTarget::init(smartBehaviorEnabled);

	//////////////////// Combat AI improved behavior //////////////////////////

	if (smartBehaviorEnabled) {
		// Don't pickup a weapon if its magazine is empty and there are no ammo for it
		// Это поблажка для AI
		HookCall(0x429CF2, ai_search_environ_hook_weapon);

		/**** Точки входа в функции AI_Try_Attack ****/

		//Точки непосредственной атаки ai_attack_ (0x42AE1D, 0x42AE5C)

		// Мove away from the target if the target is near
		MakeCalls(ai_try_attack_hack_after_attack, { 0x42AE40, 0x42AE7F }); // old ai_try_attack_hack_move

		// Точка смены оружия, когда у NPC не хватает очков действия для совершения атаки
		// Switching weapons when action points is not enough
		HookCall(0x42AB57, ai_try_attack_hook_switch_weapon); // old ai_try_attack_hook_switch_fix

		// Точка смены оружия, когда в оружие у NPC нет патронов и патроны не были найдены в инвентаре или на земле
		// перед этим происходит убирание текущего оружия из слота (возможно что эта точка не нужна будет)
		//HookCall(0x42AB3B, ai_try_attack_hook_switch_weapon_not_found_ammo);

		// Точка смены оружия, если текущее оружие небезопасно для текущей ситуации
		// или найти оружие если NPC безоружен и цель не относится к типу Biped или цель вооружена
		// и еще какие-то условия
		HookCall(0x42A905, ai_try_attack_hook_switch); // ai_try_attack_hook_switch_weapon_on_begin_turn

		// Точка смены оружия, когда превышен радиус действия атаки и NPC безоружен
		//HookCall(0x42AC05, ai_try_attack_hook_switch_weapon_out_of_range);

		// Точка проверки сделать атаку по цели, combat_check_bad_shot_ возвращается результат проверки (0-7)
		HookCall(0x42A92F, ai_try_attack_hook_check_attack);

		// Точка входа, попытка найти патроны на земле, если они не были найдены в инвентаре
		//HookCall(0x42AA25, ai_try_attack_hook_check_attack);

		// Точка входа при блокировании выстрела по цели
		// Checks the movement path for the possibility а shot, if the shot to the target is blocked
		HookCall(0x42AC55, ai_try_attack_hook_shot_blocked);

		// Точка входа когда дистанция превышает радиус атаки
		// Forces the AI to move to target closer to make an attack on the target when the distance exceeds the range of the weapon
		HookCall(0x42ABD7, ai_try_attack_hook_out_of_range);

		// Точка: плохой шанс поразить цель (вызывается ai_run_away_) [переопределяется в AI.cpp]
		// Поведение попробовать сменить оружие или цель, если другой цели нет продолжить атаку текушей
		HookCalls(ai_try_attack_hook_bad_tohit, { 0x42ACE5, 0x42ABA8 });

		/***** Разное ******/

		// Исправление функции для отрицательного значения дистанции которое игнорирует условия дистанции stay/stay_closer
		HookCall(0x429FDB, ai_move_steps_closer_hook); // jle hook

	}

	// Before starting his turn npc will always check if it has better weapons in inventory, than there is a current weapon
	int BetterWeapons = GetConfigInt("CombatAI", "TakeBetterWeapons", 0);
	if (BetterWeapons) {
		//HookCall(0x42A92F, ai_try_attack_hook);
		//HookCall(0x42A905, ai_try_attack_hook_switch);
		LookupOnGround = (BetterWeapons > 1); // always check the items available on the ground
	}

	// Fixes a rare situation for an NPC when an attacking NPC (unarmed or armed with a melee weapon)
	// wants to pickup a weapon placed on the map, but to perform actions, he will not have enough AP to pickup this item,
	// after the next turn, he will attack the player's
	// This will prevent an attempt to pickup the weapon at the beginning of the attack if there is not enough AP
	if (GetConfigInt("CombatAI", "ItemPickUpFix", 0)) { // TODO rename to WeaponPickUpFix
		HookCall(0x429CAF, ai_search_environ_hook);
	}

	// Пренести в AI.cpp
	// Fixed switching weapons when action points is not enough
	//if (GetConfigInt("CombatAI", "NPCSwitchingWeaponFix", 1)) {
	//	HookCall(0x42AB57, ai_try_attack_hook_switch_fix);
	//}
}

}
