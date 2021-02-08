/*
 *    sfall
 *    Copyright (C) 2020  The sfall team
 *
 */

#include <map>

#include "..\..\main.h"
#include "..\..\FalloutEngine\Fallout2.h"
#include "..\LoadGameHook.h"

#include "..\..\Game\items.h"

#include "..\AI.h"
#include "AI.FuncHelpers.h"

#include "AI.Behavior.h"

/*
	Distance:
	stay        - ограниченное передвижение в бою, атакующий будет по возможности оставаться на том гексе где начал бой, все передвижения запрещены, кроме побега с поле боя
	stay_close  - держаться рядом, атакующий будет держаться на дистанции не превышающей 5 гексов от игрока (применяется только для напарников).
	charge      - атакующее поведение, AI будет пытаться всегда приблизиться к своей цели перед или после атаки.
	snipe       - атака с расстояния, атакующий займет выжидающую позицию с которой будет атаковать, а при сокращении дистанции между атакующим и целью, атакующий попытается отойти от цели на дистанцию до 10 гексов.
	on_your_own - специального поведение для этого не определено.

	Disposition:
	coward      -
	defensive   -
	aggressive  -
	berserk     -
*/

namespace sfall
{

using namespace fo;
using namespace Fields;

// Доработка функции ai_move_steps_closer_ которая принимает флаги в параметре дистанции для игнорирования stay/stat_close
// параметр дистанции при этом должен передаваться в инвертированном значении
// flags:
//	0x01000000 - игнорирует stay
//  0x02000000 - игнорирует stay и stat_close
static void __declspec(naked) ai_move_steps_closer_hook() {
	static DWORD ai_move_steps_closer_ForceMoveRet = 0x42A02F;
	static DWORD ai_move_steps_closer_MoveRet      = 0x429FF4;
	static DWORD ai_move_steps_closer_ErrorRet     = 0x42A1B1;
	__asm {
		jz   badDistance;
		// здесь значении дистанции в отрицательном значении
		not  ebx;
		mov  ebp, ebx;         // restored the distance value
		and  ebp, ~0x0F000000; // unset flags
		test ebx, 0x02000000;  // check force move flag
		jz   stayClose;
		jmp  ai_move_steps_closer_ForceMoveRet;

stayClose:
		test ebx, 0x01000000; // check move flag
		jz   badDistance;
		call fo::funcoffs::ai_cap_;
		mov  eax, [eax + 0xA0]; // cap.distance
		jmp  ai_move_steps_closer_MoveRet;

badDistance:
		jmp  ai_move_steps_closer_ErrorRet;
	}
}

static void __declspec(naked) ai_search_environ_hook() {
	static const DWORD ai_search_environ_Ret = 0x429D3E;
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
		jmp  ai_search_environ_Ret;      // next object
	}
}

/////////////////////////////////////////////////////////////////////////////////////////

static uint32_t __fastcall CheckAmmo(fo::GameObject* weapon, fo::GameObject* critter) {
	if (AIHelpers::CritterHaveAmmo(critter, weapon)) return 1;

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
		call CheckAmmo;
		pop  ecx;
end:
		retn;
	}
}

/////////////////////////////////////////////////////////////////////////////////////////

static struct AttackerData {
	const char* name = nullptr; // не совсем безопасно так хранить указатель, так как память может быть использована под другое
	fo::AIcap*  cap  = nullptr;
	long killType;
	long bodyType;
	long maxAP; // без учета бонуса
	bool InDudeParty;
	bool BonusMoveAP = false;

	struct CoverTile {
		long tile;
		long distance;
	} cover;

	void setData(fo::GameObject* _attacker) {
		InDudeParty = (fo::func::isPartyMember(_attacker) > 0);
		name = fo::func::critter_name(_attacker);
		cap = fo::func::ai_cap(_attacker);
		bodyType = fo::func::critter_body_type(_attacker);
		killType = GetCritterKillType(_attacker);
		maxAP = _attacker->critter.getAP();
		cover.tile = -1;
	}
} attacker;

static bool TargetPerception(fo::GameObject* item) {

}

// Карта должна хранить номер тайла где криттер последний раз видел его атакующего криттера
// используется в тех случая когда не была найдена цель
std::unordered_map<fo::GameObject*, long> lastAttackerTile;

static bool CheckCoverTile(std::vector<long> &v, long tile) {
	return (std::find(v.cbegin(), v.cend(), tile) != v.cend());
}

// Поиск близлежащего гекса объекта за которым NPC может укрыться от стрелкового оружия нападающего
static long GetCoverBehindObjectTile(fo::GameObject* source, fo::GameObject* target, long inRadius, long allowMoveDistance) {
	if (target->critter.IsNotActive()) return -1;

	// если NPC безоружен или у атакующего не стрелковое оружие, то выход
	fo::GameObject* item = fo::func::inven_right_hand(source);
	if (!item || AIHelpers::GetWeaponSubType(item, false) != fo::AttackSubType::GUNS) {
		return -1;
	}

	// если цель вооружена и простреливается то переместиться за укрытие
	bool isRangeAttack = false;
	if (target == fo::var::obj_dude) {
		fo::GameObject* lItem = fo::func::inven_left_hand(target);
		if (lItem && AIHelpers::IsGunOrThrowingWeapon(lItem)) isRangeAttack = true;
	}
	if (!isRangeAttack) {
		fo::GameObject* rItem = fo::func::inven_right_hand(target);
		if (!rItem || !AIHelpers::IsGunOrThrowingWeapon(rItem)) return -1;
		isRangeAttack = true;
	}
	if (isRangeAttack && fo::func::combat_is_shot_blocked(target, target->tile, source->tile, source, 0)) return -1; // может ли цель стрелять по цели

	std::multimap<long, fo::GameObject*> objects;
	std::vector<long> checkTiles;

	const unsigned long mask = 0xFFFF0000 | (fo::OBJ_TYPE_WALL << 8) | fo::OBJ_TYPE_SCENERY;
	GetObjectsTileRadius(objects, source->tile, inRadius, source->elevation, mask); // объекты должны быть отсортированы по дальности расположения

	//long dir = fo::func::tile_dir(target->tile, source->tile); // направление цели к source

	long distToTarget = fo::func::obj_dist(source, target); // расстояние до цели
	long addDist = (distToTarget <= 3) ? 3 : 0;             // если цель расположена близко то сначала будем искать дальнее укрытие

reTryFindCoverTile:

	long tile = -1;
	long objectTile = tile;
	for (const auto &obj_pair : objects)
	{
		fo::GameObject* obj = obj_pair.second;
		if (obj->tile == objectTile) continue;
		objectTile = obj->tile; // запоминаем проверяемый гекс на котором расположен объект, для того чтобы не проверять другие объекты на гексе

		//DEV_PRINTF2("\nCover object: %s tile:%d", fo::func::critter_name(obj), objectTile);

		// получить гекс за объектом, направление расположения цели к объекту
		long dirCentre = fo::func::tile_dir(target->tile, obj->tile);

		// смежные направления гексов
		long roll = fo::func::roll_random(1, 2);
		if (roll > 1) roll = 5;
		long dirNear0 = (dirCentre + roll) % 6;
		roll = (roll == 5) ? 1 : 5;
		long dirNear1 = (dirCentre + roll) % 6;

		// берем первый не заблокированный гекс
		for (size_t i = 1; i < 3; i++) // максимальный радиус 2 в гекса
		{
			long _tile = fo::func::tile_num_in_direction(obj->tile, dirCentre, i);
			if (fo::func::obj_blocking_at(nullptr, _tile, obj->elevation))
			{
				_tile = fo::func::tile_num_in_direction(obj->tile, dirNear0, i);
				if (fo::func::obj_blocking_at(nullptr, _tile, obj->elevation))
				{
					_tile = fo::func::tile_num_in_direction(obj->tile, dirNear1, i);
					if (fo::func::obj_blocking_at(nullptr, _tile, obj->elevation))
					{
						continue; // все гексы заблокированы
					}
				}
			}
			if (CheckCoverTile(checkTiles, _tile)) continue;

			// оптимальное ли расстояние до укрываемого тайла?
			long distSource = fo::func::tile_dist(source->tile, _tile);
			long distTarget = fo::func::tile_dist(target->tile, _tile);
			if (distSource + addDist >= distTarget) {
				DEV_PRINTF3("\nCover no optimal distance: %d | s:%d >= t:%d", _tile, distSource + addDist, distTarget);
				if (!addDist) checkTiles.push_back(_tile);
				continue; // не оптимальное
			}

			// проверить не простреливается ли данный гекс (здесь видимо нужно дополнительно проверять всех враждебных криттеров)
			if (fo::func::combat_is_shot_blocked(target, target->tile, _tile, 0, 0) == false) {
				//DEV_PRINTF1("\nCover tile is shooting:%d", _tile);
				checkTiles.push_back(_tile);
				continue; // простреливается
			}

			// проверить не заблокирован ли путь к гексу
			long pathLength = fo::func::make_path_func(source, source->tile, _tile, 0, 0, (void*)fo::funcoffs::obj_blocking_at_);
			if (pathLength > 0) {
				// хватает ли очков действия чтобы переместиться к гексу
				if (allowMoveDistance >= pathLength) {
					tile = _tile;
					break; // хватает, выходим из цикла и возвращаем гекс
				}
				else if (attacker.cover.tile == -1) { // запоминаем самый ближе-расположенный гекс для отхода в укрытие (для поведения отступления)
					attacker.cover.tile = _tile;
					attacker.cover.distance = pathLength;
					DEV_PRINTF2("\nCover: %s set moveback tile: %d\n", fo::func::critter_name(obj), _tile);
				}
			} else {
				//DEV_PRINTF2("\nCover: %s I can't move to tile: %d\n", fo::func::critter_name(obj), _tile);
			}
			checkTiles.push_back(_tile);
		}
		if (tile > -1) {
			if (isDebug) fo::func::debug_printf("\nCover: %s move to tile %d\n", fo::func::critter_name(obj), tile);
			attacker.cover.tile = -1;
			return tile;
		}
	}
	if (addDist) {
		addDist = 0;
		goto reTryFindCoverTile; // повторяем
	}
	return -1;
}

static void FleeCover(fo::GameObject* source, fo::GameObject* target) {
}

static long GetRetargetTileSub(fo::GameObject* source, long shotDir, long roll, long range) {
	roll = (roll == 5) ? 1 : 5;
	long dir = (shotDir + roll) % 6;
	long tile = fo::func::tile_num_in_direction(source->tile, dir, range);
	if (fo::func::obj_blocking_at(nullptr, tile, source->elevation)) {
		tile = -1;
	}
	return tile;
}

// Проверяет наличие дружественных NPC на линии перед атакой для отхода в сторону на 1 или 2 гекса
// Имеет в себе функцию cai_retargetTileFromFriendlyFire для ретаргетинга гекса
static void ReTargetTileFromFriendlyFire(fo::GameObject* source, fo::GameObject* target) {
	long reTargetTile = source->tile;

	if (fo::func::item_w_range(source, fo::AttackType::ATKTYPE_RWEAPON_PRIMARY) > 1) {
		fo::GameObject* friendNPC = AI::CheckFriendlyFire(target, source);
		if (friendNPC) {
			long distToTarget = fo::func::obj_dist(friendNPC, target);
			if (distToTarget > 3) {
				long shotDir = fo::func::tile_dir(source->tile, target->tile);
				long tile = -1;
				long range = 0;
				char check = 0;
incDistance:
				range++;
				long roll = fo::func::roll_random(1, 2);
				if (roll > 1) roll = 5;

				long dir = (shotDir + roll) % 6;
				long _tile = fo::func::tile_num_in_direction(source->tile, dir, range);

				if (fo::func::obj_blocking_at(nullptr, _tile, source->elevation)) {
					tile = GetRetargetTileSub(source, shotDir, roll, range);
				} else {
					tile = _tile;
				}
reCheck:
				if (tile > -1) {
					if (!AIHelpers::CheckFriendlyFire(target, source, tile)) {
						reTargetTile = tile;
					} else if (!check) {
						tile = GetRetargetTileSub(source, shotDir, roll, range);
						check++;
						goto reCheck;
					} else if (range == 1) { // max range distance 2
						check = 0;
						long cost = AIHelpers::GetCurrenShootAPCost(source, target);
						if ((source->critter.getAP() - 2) >= cost) goto incDistance;
					}
				}
			}
		}
	}
	// cai_retargetTileFromFriendlyFire здесь не имеет приоритет т.к. проверяет линию огня для других атакующих NPC
	if (reTargetTile == source->tile) {
		if (fo::func::cai_retargetTileFromFriendlyFire(source, target, &reTargetTile) == -1) return;
		if (reTargetTile != source->tile) DEV_PRINTF("\n[AI] cai_retargetTileFromFriendlyFire");
	}
	if (reTargetTile != source->tile) {
		DEV_PRINTF("\n[AI] ReTarget tile before attack");
		AIHelpers::CombatMoveToTile(source, reTargetTile, source->critter.getAP());
	}
}

/////////////////////////////////////////////////////////////////////////////////////////

// Проверки при попытке сменить оружие, если у NPC не хватает очков действия для совершения атаки
static long __fastcall AI_Check_Weapon_Switch(fo::GameObject* target, long &hitMode, fo::GameObject* source, fo::GameObject* weapon) {
	DEV_PRINTF("\n[AI] ai_try_attack: No AP for shot.");

	if (source->critter.getAP() <= 0) return -1;
	if (!weapon) return 1; // no weapon in hand slot

	long _hitMode = fo::func::ai_pick_hit_mode(source, weapon, target);
	if (_hitMode != hitMode) {
		hitMode = _hitMode;
		return 0; // сменили режим стрельбы
	}

	fo::GameObject* item = fo::func::ai_search_inven_weap(source, 1, target);
	if (!item) return 1; // no weapon in inventory, true to allow the to search continue weapon on the map

	long wType = fo::func::item_w_subtype(item, AttackType::ATKTYPE_RWEAPON_PRIMARY);
	if (wType <= AttackSubType::MELEE) { // unarmed and melee weapon, check the distance before switching
		if (fo::func::obj_dist(source, target) > 2) return -1;
	}
	return 1; // выполнить ванильное поведения функции ai_switch_weapons
}

static void __declspec(naked) ai_try_attack_hook_switch_fix() {
	__asm {
		push edx;
		push [ebx];                  // weapon  (push dword ptr [esp + 0x364 - 0x3C + 8];)
		push esi;                    // source
		call AI_Check_Weapon_Switch; // edx - hit mode
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
// если таковой гекс не будет найден то выполнится действия по умолчанию для функции ai_move_steps_closer
// TODO: Необходимо улучшить алгоритм для поиска гекса для совершения выстрела, для снайперов должна быть применена другая тактика
// Добавить учет открывание двери при построении пути
static int32_t __fastcall AI_Move_Steps_Tile(fo::GameObject* source, fo::GameObject* target, int32_t &hitMode) {
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

	DEV_PRINTF1("\n[AI] Move_Steps_Tile: distance %d", ap);

	char rotationData[800];
	long pathLength = fo::func::make_path_func(source, source->tile, target->tile, rotationData, 0, (void*)fo::funcoffs::obj_blocking_at_);
	if (pathLength > ap) pathLength = ap;

	long checkTile = source->tile;
	for (int i = 0; i < pathLength; i++)
	{
		checkTile = fo::func::tile_num_in_direction(checkTile, rotationData[i], 1);
		DEV_PRINTF1("\n[AI] Move_Steps_Tile: path tile %d", checkTile);

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
			if (object->TypeFid() != ObjType::OBJ_TYPE_CRITTER || fo::func::combat_is_shot_blocked(object, object->tile, checkTile, 0, 0)) continue; //sf_check_block_line_of_fire(object, checkTile)
			shotTile = checkTile;
			distance = i + 1;
			DEV_PRINTF2("\n[AI] Get friendly fire shot tile:%d, Dist:%d", checkTile, distance);
		}
	}
	if (shotTile && ap > distance) {
		fo::AIcap* cap = fo::func::ai_cap(source);
		if (cap->distance != AIpref::distance::snipe) { // оставляем AP для поведения "Snipe"
			//проверить шанс попадания по цели если он хороший то выход из цикла, нет необходимости подбегать близко

			long leftAP = (ap - distance) % cost; // оставшиеся AP после подхода и выстрела
			// spend left APs
			long newTile = checkTile = shotTile;
			long dist;
			for (int i = distance; i < pathLength; i++) // начинаем со следующего тайла и идем до конца пути
			{
				checkTile = fo::func::tile_num_in_direction(checkTile, rotationData[i], 1);
				DEV_PRINTF1("\n[AI] Move_Steps_Tile: path extra tile %d", checkTile);

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
	if (shotTile && isDebug) fo::func::debug_printf("\n[AI] %s: Move to tile for shot.", attacker.name);

	int result = (shotTile && AIHelpers::CombatMoveToTile(source, shotTile, distance) == 0) ? 1 : 0;
	if (result) hitMode = fo::func::ai_pick_hit_mode(source, itemHand, target); // try pick new weapon mode after step move

	return result;
}

static void __declspec(naked) ai_try_attack_hook_shot_blocked() {
	__asm {
		pushadc;
		mov  ecx, eax;                      // source
		lea  eax, [esp + 0x364 - 0x38 + 12 + 4];
		push eax;                           // hit mode
		call AI_Move_Steps_Tile;            // edx - target
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

static fo::GameObject* __stdcall AI_SearchWeaponOnMap(fo::GameObject* source, fo::GameObject* item, fo::GameObject* target) {
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
//static bool LookupContaiter = false;

// Поиск наилучшего оружия перед совершением атаки (в первом цикле ai_try_attack_)
// Атакующий попытается найти лучшее оружие в своем инвентаре или подобрать близлежащее на земле оружие
// TODO: Добавить поддержку осматривать контейнеры/трупы на наличие в них оружия
// Executed once when the NPC starts attacking
static int32_t __fastcall AI_SearchWeapon(fo::GameObject* source, fo::GameObject* target, fo::GameObject* &weapon, uint32_t &hitMode) {

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
			fo::func::ai_can_use_weapon(source, item, AttackType::ATKTYPE_RWEAPON_PRIMARY))
		{
			if (item->item.ammoPid == -1 || // оружие не имеет патронов
				fo::func::item_w_subtype(item, AttackType::ATKTYPE_RWEAPON_PRIMARY) == fo::AttackSubType::THROWING || // Зачем здесь метательные?
				(fo::func::item_w_curr_ammo(item) || AIHelpers::CritterHaveAmmo(source, item)))
			{
				if (!fo::func::combat_safety_invalidate_weapon_func(source, item, AttackType::ATKTYPE_RWEAPON_PRIMARY, target, 0, 0)) { // weapon safety
					bestWeapon = fo::func::ai_best_weapon(source, bestWeapon, item, target);
				}
			}
		}
	}
	if (bestWeapon != itemHand) DEV_PRINTF1("\n[AI] Find best weapon Pid: %d", (bestWeapon) ? bestWeapon->protoId : -1);

	// выбрать лучшее на основе навыка
	if (itemHand != bestWeapon)	bestWeapon = AIHelpers::AICheckWeaponSkill(source, itemHand, bestWeapon);

	if ((LookupOnGround && !fo::func::critterIsOverloaded(source)) && source->critter.getAP() >= 3 && fo::func::critter_body_type(source) == BodyType::Biped) {

		// построить путь до цели (зачем?)
		int toDistTarget = fo::func::make_path_func(source, source->tile, target->tile, 0, 0, (void*)fo::funcoffs::obj_blocking_at_);
		if ((source->critter.getAP() - 3) >= toDistTarget) goto notRetrieve; // не поднимать, если у NPC хватает очков сделать удар по цели

		fo::GameObject* itemGround = AI_SearchWeaponOnMap(source, bestWeapon, target);

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

	int32_t _hitMode = -1;
	if (bestWeapon && (!itemHand || itemHand->protoId != bestWeapon->protoId)) {
		weapon = bestWeapon;
		hitMode = _hitMode = fo::func::ai_pick_hit_mode(source, bestWeapon, target);
		fo::func::inven_wield(source, bestWeapon, fo::InvenType::INVEN_TYPE_RIGHT_HAND);
		__asm call fo::funcoffs::combat_turn_run_;
		if (isDebug) fo::func::debug_printf("\n[AI] Wield best weapon pid: %d AP: %d", bestWeapon->protoId, source->critter.getAP());
	}
	return _hitMode;
}

static bool weaponIsSwitch = 0;

const char* checkShotResult = "\n[AI] Check bad shot result: %d";

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
		call AI_SearchWeapon;                 // edx - target
		test eax, eax;
		cmovge ebx, eax;                      // >= 0 (hit_mode)
		// restore value reg.
		mov  eax, esi;
		mov  edx, ebp;
		xor  ecx, ecx;
end:
		mov  weaponIsSwitch, 0;
		call fo::funcoffs::combat_check_bad_shot_; // возвращает результат 4 когда криттер умирает в цикле хода
#ifndef NDEBUG
		push eax;
		push eax;
		push checkShotResult;
		call fo::funcoffs::debug_printf_;
		add  esp, 8;
		pop  eax;
#endif
		cmp  eax, 4;
		je   targetIsDead;
		retn;
targetIsDead:
		mov  edi, 10;
		mov  dword ptr [esp + 0x364 - 0x30 + 4], 1; // set result TargetDead
		retn;
	}
}

static void __declspec(naked) ai_try_attack_hook_switch() {
	__asm {
		mov weaponIsSwitch, 1;
		jmp fo::funcoffs::ai_switch_weapons_;
	}
}

/////////////////////////////////////////////////////////////////////////////////////////

// Анализирует ситуацию для текущей выбранной цели атакующего, и если ситуация неблагоприятная для атакующего
// то будет совершена попытка сменить текущую цель на альтернативную. Поиск цели осуществляется в коде движка
static bool __fastcall AI_CheckTarget(fo::GameObject* source, fo::GameObject* target) {
	DEV_PRINTF1("\n[AI] Analyzing target: %s ", fo::func::critter_name(target));

	int distance = fo::func::obj_dist(source, target); // возвращает дистанцию 1 если объекты расположены вплотную
	if (distance <= 1) return false;

	bool shotIsBlock = fo::func::combat_is_shot_blocked(source, source->tile, target->tile, target, 0);

	int pathToTarget = fo::func::make_path_func(source, source->tile, target->tile, 0, 0, (void*)fo::funcoffs::obj_blocking_at_);
	if (shotIsBlock && pathToTarget == 0) { // shot and move block to target
		DEV_PRINTF("-> is blocking!");
		return true;                        // picking alternate target
	}

	fo::AIcap* cap = fo::func::ai_cap(source);
	if (shotIsBlock && pathToTarget >= 5) { // shot block to target, can move
		long dist_disposition = distance;
		switch (cap->disposition) {
		case AIpref::defensive:
			pathToTarget += 5;
			dist_disposition += 5;
			break;
		case AIpref::aggressive: // AI aggressive never does not change its target if the move-path to the target is not blocked
			pathToTarget = 1;
			break;
		case AIpref::berserk:
			pathToTarget /= 2;
			dist_disposition -= 5;
			break;
		}
		// поиск цели, возможно рядом есть альтернативная цель
		fo::GameObject* enemy = fo::func::ai_find_nearest_team(source, target, 1);
		if (enemy && enemy != target) {
			if (fo::func::obj_dist(source, enemy) <= 1) {
				DEV_PRINTF("-> has closer other enemy located!");
				return true; // поблизости имеется альтернативный враг -> picking alternate target
			}
			int path = fo::func::make_path_func(source, source->tile, enemy->tile, 0, 0, (void*)fo::funcoffs::obj_blocking_at_);
			if (path > 0 && path < pathToTarget) {
				DEV_PRINTF("-> has near other enemy located!");
				return true; // поблизости имеется альтернативный враг -> picking alternate target
			}
		}
		// dist=10 ap=8 cost=5 move=3
		// pathToTarget=6 8*2=16
		// 10 >= 8 && 6 >= 16
		// если дистанция до цели превышает 9 гексов а реальный путь до нее больше чем имеющихся очков действий в два раза тогда берем новую цель
		if (dist_disposition >= 10 && pathToTarget >= (source->critter.getAP() * 2)) {
			DEV_PRINTF("-> is far located!");
			return true; // target is far -> picking alternate target
		}
		DEV_PRINTF("-> shot is blocked. I can move to target.");
	}
	else if (shotIsBlock == false) { // can shot and move to target
		fo::GameObject* itemHand = fo::func::inven_right_hand(source); // current item
		if (!itemHand && pathToTarget == 0) {
			DEV_PRINTF("-> I unarmed and block move path to target!");
			return true; // no item and move block to target -> picking alternate target
		}
		if (!itemHand) return false; // безоружны

		fo::Proto* proto = GetProto(itemHand->protoId);
		if (proto && proto->item.type == ItemType::item_type_weapon) {
			int hitMode = fo::func::ai_pick_hit_mode(source, itemHand, target);
			int maxRange = fo::func::item_w_range(source, hitMode);

			// атакующий не сможет атаковать если доступ к цели заблокирован и он имеет оружие ближнего действия
			if (maxRange == 1 && !pathToTarget) {
				DEV_PRINTF("-> move path is blocking and my weapon no have range!");
				return true;
			}

			int diff = distance - maxRange; // 3 - 1 = 2
			if (diff > 0) { // shot out of range (положительное число не хватает дистанции для оружия)
				/*if (!pathToTarget) return true; // move block to target and shot out of range -> picking alternate target (это больше не нужно т.к. есть твик к подходу)*/

				if (cap->disposition == AIpref::coward && diff > fo::func::roll_random(8, 12)) {
					DEV_PRINTF("-> is located beyond range of weapon. I'm afraid to approach target!");
					return true;
				}

				long cost = game::Items::item_w_mp_cost(source, hitMode, 0);
				if (diff > (source->critter.getAP() - cost)) {
					DEV_PRINTF("-> I don't have enough AP to move to target and make shot!");
					return true; // не хватит очков действия для подхода и выстрела -> picking alternate target
				}
			}
		}    // can shot (or move), and hand item is not weapon
	} else { // block shot and can move
		DEV_PRINTF("-> shot is blocked. I can move to target [#2]");
		// Note: pathToTarget здесь будет всегда иметь значение 1-4
	}
	return false; // can shot and move / can shot and block move
}

static const char* reTargetMsg = "\n[AI] I can't get at my target. Try picking alternate.";
#ifndef NDEBUG
static const char* targetGood  = "-> is possible attack!\n";
#endif

static void __declspec(naked) ai_danger_source_hack_find() {
	static const uint32_t ai_danger_source_hack_find_Pick = 0x42908C;
	static const uint32_t ai_danger_source_hack_find_Ret  = 0x4290BB;
	__asm {
		push eax;
		push edx;
		mov  edx, eax; // source.who_hit_me target
		mov  ecx, esi; // source
		call AI_CheckTarget;
		pop  edx;
		test al, al;
		pop  eax;
		jnz  reTarget;
		add  esp, 0x1C;
		pop  ebp;
		pop  edi;
#ifndef NDEBUG
		push eax;
		push targetGood;
		call fo::funcoffs::debug_printf_;
		add  esp, 4;
		pop  eax;
#endif
		jmp  ai_danger_source_hack_find_Ret;
reTarget:
		push reTargetMsg;
		call fo::funcoffs::debug_printf_;
		add  esp, 4;
		jmp  ai_danger_source_hack_find_Pick;
	}
}

/////////////////////////////////////////////////////////////////////////////////////////

// Получает цель и дистанцию до нее
static unsigned long GetTargetDistance(fo::GameObject* source, fo::GameObject* &target) {
	unsigned long distanceLast = -1; // inactive

	fo::GameObject* lastAttacker = AI::AIGetLastAttacker(source);
	if (lastAttacker && lastAttacker != target && lastAttacker->critter.IsActiveNotDead()) {
		distanceLast = fo::func::obj_dist(source, lastAttacker);
	}
	// target is active?
	unsigned long distance = (target && target->critter.IsActiveNotDead())
							 ? fo::func::obj_dist(source, target)
							 : -1; // inactive

	if (distance >= 3 && distanceLast >= 3) return -1;   // also distances == -1
	if (distance >= distanceLast) target = lastAttacker; // replace target (attacker critter has priority)

	return (distance >= distanceLast) ? distanceLast : distance;
}

// Функция анализирует используемое оружие у цели, и если цель использует оружие ближнего действия то атакующий AI
// по завершению хода попытается отойти от атакующей его цели на небольшое расстояние
// Executed after the NPC attack
static long GetMoveAwayDistaceFromTarget(fo::GameObject* source, fo::GameObject* &target) {

	if (attacker.killType > KillType::KILL_TYPE_women ||    // critter is not men & women
		attacker.cap->disposition == AIpref::disposition::berserk ||
		///cap->distance == AIpref::distance::stay || // stay в ai_move_away запрещает движение
		attacker.cap->distance == AIpref::distance::charge) // charge в движке используется для сближения с целью
	{
		return 0;
	}

	unsigned long distance = source->critter.getAP(); // для coward: отойдет на максимально возможную дистанцию

	if (attacker.cap->disposition != AIpref::disposition::coward) {
		if (distance >= 3) return 0; // source still has a lot of action points

		if ((distance = GetTargetDistance(source, target)) > 2) return 0; // цели далеко, или неактивны

		fo::GameObject* sWeapon = fo::func::inven_right_hand(source);
		long wTypeR = fo::func::item_w_subtype(sWeapon, fo::func::ai_pick_hit_mode(source, sWeapon, target)); // возможно тут надо использовать последний HitMode
		if (wTypeR <= AttackSubType::MELEE) return 0; // source has a melee weapon or unarmed

		fo::Proto* protoR = nullptr;
		fo::Proto* protoL = nullptr;
		AttackSubType wTypeRs = AttackSubType::NONE;
		AttackSubType wTypeL  = AttackSubType::NONE;
		AttackSubType wTypeLs = AttackSubType::NONE;

		fo::GameObject* itemHandR = fo::func::inven_right_hand(target);
		if (!itemHandR && target != fo::var::obj_dude) { // target is unarmed
			long damage = fo::func::stat_level(target, Stat::STAT_melee_dmg);
			if (damage * 2 < source->critter.health / 2) return 0;
			goto moveAway;
		}
		if (itemHandR) {
			protoR = fo::GetProto(itemHandR->protoId);
			long weaponFlags = protoR->item.flagsExt;

			wTypeR = fo::GetWeaponType(weaponFlags);
			if (wTypeR == AttackSubType::GUNS) return 0; // the attacker **not move away** if the target has a firearm
			wTypeRs = fo::GetWeaponType(weaponFlags >> 4);
		}
		if (target == fo::var::obj_dude) {
			fo::GameObject* itemHandL = fo::func::inven_left_hand(target);
			if (itemHandL) {
				protoL = fo::GetProto(itemHandL->protoId);
				wTypeL = fo::GetWeaponType(protoL->item.flagsExt);
				if (wTypeL == AttackSubType::GUNS) return 0; // the attacker **not move away** if the target(dude) has a firearm
				wTypeLs = fo::GetWeaponType(protoL->item.flagsExt >> 4);
			} else if (!itemHandR) {
				// dude is unarmed
				long damage = fo::func::stat_level(target, Stat::STAT_melee_dmg);
				if (damage * 4 < source->critter.health / 2) return 0;
			}
		}
moveAway:
		// if attacker is aggressive then **not move away** from any throwing weapons (include grenades)
		if (attacker.cap->disposition == AIpref::aggressive) {
			if (wTypeRs == AttackSubType::THROWING || wTypeLs == AttackSubType::THROWING) return 0;
			if (wTypeR  == AttackSubType::THROWING || wTypeL  == AttackSubType::THROWING) return 0;
		} else {
			// the attacker **not move away** if the target has a throwing weapon and it is a grenade
			if (protoR && wTypeR == AttackSubType::THROWING && protoR->item.weapon.damageType != DamageType::DMG_normal) return 0;
			if (protoL && wTypeL == AttackSubType::THROWING && protoL->item.weapon.damageType != DamageType::DMG_normal) return 0;
		}
		distance -= 3; // всегда держаться на дистанции в 2 гекса от цели (не учитывать AP)
	}
	else if (GetTargetDistance(source, target) == -1) return 0; // цели неактивны

	return distance;
}

static void MoveAwayFromTarget(fo::GameObject* source, fo::GameObject* target, long distance) {
	if (isDebug) {
		#ifdef NDEBUG
		fo::func::debug_printf("\n[AI] %s: Away from my target!", fo::func::critter_name(source));
		#else
		fo::func::debug_printf("\n[AI] %s: Away from: %s, Dist:%d, AP:%d, HP:%d", fo::func::critter_name(source), fo::func::critter_name(target), distance, source->critter.getAP(), source->critter.health);
		#endif
	}
	// функция принимает отрицательный аргумент дистанции для того чтобы держаться на определенной дистанции от указанной цели при этом поведение stay будет игнорироваться
	fo::func::ai_move_away(source, target, distance);
}

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

/////////////////////////////////////////////////////////////////////////////////

// Заставляет NPC двигаться к цели ближе, чтобы атаковать цель, когда расстояние превышает дальность действия оружия
static int32_t __fastcall AI_Try_Move_Steps_Closer(fo::GameObject* source, fo::GameObject* target, int32_t &outHitMode) {

	fo::GameObject* itemHand = fo::func::inven_right_hand(source);
	if (!itemHand) return 1;

	long getTile = -1, dist = -1;

	long mode = fo::func::ai_pick_hit_mode(source, itemHand, target);
	long cost = game::Items::item_w_mp_cost(source, mode, 0);

	// check the distance and number of remaining AP's
	long weaponRange = fo::func::item_w_range(source, mode);
	if (weaponRange <= 1) return 1;

	dist = fo::func::obj_dist(source, target) - weaponRange; // required approach distance
	long ap = source->critter.getAP() - dist; // subtract the number of action points to the move, leaving the number for the shot
	long remainingAP = ap % cost;

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

	//if (dist == -1) dist = source->critter.getAP(); // in dist - the distance to move
	if (getTile != -1 && isDebug) fo::func::debug_printf("\n[AI] %s: Weapon out of range. Move to tile for shot.", fo::func::critter_name(source));

	int result = (getTile != -1) ? AIHelpers::CombatMoveToTile(source, getTile, dist) : 1;
	if (!result) outHitMode = fo::func::ai_pick_hit_mode(source, itemHand, target); // try pick new weapon mode after step move

	return result;
}

static void __declspec(naked) ai_try_attack_hook_out_of_range() {
	__asm {
		pushadc;
		mov  ecx, eax;
		lea  eax, [esp + 0x364 - 0x38 + 12 + 4];
		push eax;
		call AI_Try_Move_Steps_Closer;
		test eax, eax;
		popadc;
		jnz  defaultMove;
		retn;
defaultMove:
		jmp fo::funcoffs::ai_move_steps_closer_; // default behavior
	}
}

/////////////////////////////////////////////////////////////////////////////////

enum class AIAttackResult : int32_t
{
	Default      = -1,
	TargetDead   = 1,
	NoMovePoints = 2,
	LostWeapon   = 3,
	ReTryAttack  = 4,
	MoveAway
};

enum class CombatDifficulty : long
{
	Easy   = 0,
	Normal = 1,
	Hard   = 2
};

// Расстояния для напарников игрока когда они остаются без найденной цели
static inline long getAIPartyMemberDistances(long aiDistance) {
	static long aiPartyMemberDistances[5] = {
		5,   // stay_close
		10,  // charge
		12,  // snipe
		8,   // on_your_own
		5000 // stay
	};
	return (aiDistance > -1) ? aiPartyMemberDistances[aiDistance] : 8;
}

static long bonusAP = 6;
static CombatDifficulty combatDifficulty = CombatDifficulty::Normal;

static bool AttackerIsHumanoid() {
	return (attacker.bodyType == BodyType::Biped && attacker.killType != fo::KillType::KILL_TYPE_gecko);
}

static void SetMoveBonusAP(fo::GameObject* source) {
	if (AttackerIsHumanoid()) {
		switch (combatDifficulty) {
			case CombatDifficulty::Easy:
				bonusAP = 4;
				break;
			case CombatDifficulty::Normal:
				bonusAP = 6;
				break;
			case CombatDifficulty::Hard:
				bonusAP = 8;
				break;
		}
		source->critter.movePoints += bonusAP;
		attacker.BonusMoveAP = true;
	}
}

static void RemoveMoveBonusAP(fo::GameObject* source) {
	attacker.BonusMoveAP = false;
	if (source->critter.getAP() == 0) return;
	if (source->critter.getAP() <= bonusAP) {
		source->critter.movePoints = 0;
	} else {
		source->critter.movePoints -= bonusAP;
	}
}

static bool npcPercentMinHP = false;

// Реализация движковой функции cai_perform_distance_prefs с измененным и дополнительным функционалом
static void DistancePrefBeforeAttack(fo::GameObject* source, fo::GameObject* target) {
	long distance = 0;

	/* Distance: Charge behavior */
	if (attacker.cap->distance == fo::AIpref::distance::charge && fo::func::obj_dist(source, target) > fo::func::item_w_range(source, fo::AttackType::ATKTYPE_RWEAPON_PRIMARY) / 2) {
		// приблизиться на расстояние для совершения одной атаки
		distance = source->critter.getAP();
		long cost = AIHelpers::GetCurrenShootAPCost(source, fo::AttackType::ATKTYPE_RWEAPON_PRIMARY, 0);
		if (cost != -1 && distance > cost) distance -= cost;
		fo::func::ai_move_steps_closer(source, target, distance, 1);
	}
	/* Distance: Snipe behavior */
	else if (attacker.cap->distance == fo::AIpref::distance::snipe && (distance = fo::func::obj_dist(source, target)) < 10) {
		DEV_PRINTF1("\n[AI] AIpref::distance::snipe: %s", fo::func::critter_name(target));
		if (AI::AIGetLastTarget(target) == source) { // target атакует source target->critter.getHitTarget()
			// атакующий отойдет на расстояние в 10 гексов от своей цели если она на него нападает
			bool shouldMove = ((fo::func::combatai_rating(source) + 10) < fo::func::combatai_rating(target));
			if (shouldMove) {
				long costAP = AIHelpers::GetCurrenShootAPCost(source, fo::AttackType::ATKTYPE_RWEAPON_PRIMARY, 0);
				if (costAP != -1) {
					long shotCount = source->critter.getAP() / costAP;
					long freeAPs = source->critter.getAP() - (costAP * shotCount); // положительное число если останутся AP после атаки
					if (freeAPs > 0) {
						long dist = distance - 1;
						if (freeAPs + dist >= 5) shouldMove = false;
					}
				}
			}
			if (shouldMove) fo::func::ai_move_away(source, target, 10);
		}
	}
	/* Distance: Stay Close behavior */
	// поведение вырезано, используется только после атаки в функции движка cai_perform_distance_prefs_
}

static void ReTargetTileFromFriendlyFire(fo::GameObject* source, fo::GameObject* target, bool сheckAP) {
	if (сheckAP) {
		long cost = AIHelpers::GetCurrenShootAPCost(source, target);
		if (cost == -1 || cost >= source->critter.getAP()) return;
	}
	ReTargetTileFromFriendlyFire(source, target);
}

// Реализация движковой функции combat_ai_
static void __fastcall combat_ai_extended(fo::GameObject* source, fo::GameObject* target) {

	combatDifficulty = (CombatDifficulty)iniGetInt("preferences", "combat_difficulty", (int)combatDifficulty, (const char*)FO_VAR_game_config);
	attacker.setData(source);

	// добавить очки действия атакующему не партийцу для увеличении сложности боя
	if (!attacker.InDudeParty) source->critter.movePoints += (long)combatDifficulty * 2;

	DEV_PRINTF2("\n[AI] Begin combat: %s ID: %d", attacker.name, source->id);

	// если опция включена или атакующий не является постоянным партийцем (для постоянных партийцев min_hp рассчитывается при выборе предпочтений в панели управления)
	if (npcPercentMinHP && attacker.cap->getRunAwayMode() != AIpref::run_away_mode::none && !fo::IsPartyMember(source)) {
		long caiMinHp = fo::func::cai_get_min_hp(attacker.cap);
		// вычисляем минимальное значение HP при котором NPC будет убегать с поле боя
		long maxHP = fo::func::stat_level(source, fo::STAT_max_hit_points);
		long minHpPercent = maxHP * caiMinHp / 100;
		long fleeMinHP = maxHP - minHpPercent;
		attacker.cap->min_hp = fleeMinHP; // должно устанавливаться перед выполнением ai_check_drugs
		fo::func::debug_printf("\n[AI] Calculated flee minHP for NPC");
	}
	fo::func::debug_printf("\n[AI] %s: Flee MinHP: %d, CurHP: %d, AP: %d", attacker.name, attacker.cap->min_hp, fo::func::stat_level(source, fo::STAT_current_hp), source->critter.getAP());

	if ((source->critter.combatState & fo::CombatStateFlag::IsFlee) || (source->critter.damageFlags & attacker.cap->hurt_too_much)) {
		// fix for flee from sfall
		if (!(source->critter.combatState & fo::CombatStateFlag::ReTarget)) {
			fo::func::debug_printf("\n[AI] %s FLEEING: I'm Hurt!", attacker.name);
fleeAndCover:
			fo::func::ai_run_away(source, target); // убегает от цели или от игрока если цель не была назначена

			// функция которая должна уводить NPC в укрытие, если расстояние до игрока/цели превышено
			if (source->critter.getAP() > 0) FleeCover(source, target);
			return;
		}
		{	// fix for flee from sfall
			source->critter.combatState &= ~(fo::CombatStateFlag::ReTarget | fo::CombatStateFlag::IsFlee);
			source->critter.whoHitMe = target = 0;
		}
	}

	// требуется сделать проверку на врагов прежде чем применять наркотик (NPC может в конце боя принять)
	fo::func::ai_check_drugs(source); // попытка принять какие либо наркотики перед атакой или стимпаки если NPC ранен

	if (fo::func::stat_level(source, fo::STAT_current_hp) < attacker.cap->min_hp) {
		fo::func::debug_printf("\n[AI] %s FLEEING: I need DRUGS!", attacker.name);
		// нет медикаментов для лечения
		if (attacker.InDudeParty) { // партийцы бегут к игроку

			//fo::func::ai_run_away(source, target);
			// снять флаг бегства для партийцев, чтобы была возможность его подлечить
			//source->critter.combatState &= ~fo::CombatStateFlag::IsFlee;
			return;
		}
		goto fleeAndCover;
	}
	if (source->critter.getAP() == 0) return; // закончились очки действия

	bool findTargetAfterKill = false;
	long lastCombatAP = 0;

	if (!target) { // цель не задана, произвести поиск цели
findNewTarget:
		DEV_PRINTF("\n[AI] Find targets...");
		target = fo::func::ai_danger_source(source);

		if (!target) DEV_PRINTF("\n[AI] No find target!"); else DEV_PRINTF1("\n[AI] Pick target: %s", fo::func::critter_name(target));

		if (rememberTarget) { // rememberTarget: первая цель до которой превышен радиус действия атаки
			DEV_PRINTF1("\n[AI] I have remember target: %s", fo::func::critter_name(rememberTarget));
			if (target) {
				// выбрать ближайшую цель
				long dist1 = fo::func::obj_dist(source, target);
				long dist2 = fo::func::obj_dist(source, rememberTarget);
				if (dist1 > dist2) target = rememberTarget;
			} else {
				target = rememberTarget;
			}
			rememberTarget = nullptr;
		} else if (!target && source->critter.getHitTarget() && source->critter.getHitTarget()->critter.IsNotDead()) {
			target = source->critter.getHitTarget(); // в случае если новая цель не была найдена
			DEV_PRINTF1("\n[AI] Get my hit target: %s", fo::func::critter_name(target));
		}
	}

	if (target) {
		if (target->critter.IsNotDead()) {
			DistancePrefBeforeAttack(source, target); // используется вместо функции cai_perform_distance_prefs для предпочтения расстояния
			ReTargetTileFromFriendlyFire(source, target, findTargetAfterKill);
		}

tryAttack:
		switch ((AIAttackResult)fo::func::ai_try_attack(source, target))
		{
		case AIAttackResult::TargetDead:
			findTargetAfterKill = true;
			DEV_PRINTF("\n[AI] Attack result: TARGET DEAD!\n");
			goto findNewTarget; // поиск новой цели т.к. текущая была убита

		case AIAttackResult::NoMovePoints:
			//break;

		default:
			if (source->critter.IsDead()) return;
		}
		DEV_PRINTF("\n[AI] End attack");
	}

	/***************************************************************************************
		Поведение: Враг вне зоны. [для всех]
		Атакующий все еще имеет очки действия, его цель(обидчик) не мертв
		и дистанция до него превышает дистанцию заданную параметром max_dist в AI.txt
		Данное поведение было определено в ванильной функции
	***************************************************************************************/
	if (target && target->critter.IsNotDead() && source->critter.getAP() && fo::func::obj_dist(source, target) > attacker.cap->max_dist * 2) { // дистанция max_dist была удвоенна
		long peRange = fo::func::stat_level(source, fo::STAT_pe) * 5; // x5 множитель по умолчанию в is_within_perception (было x2)

		// найти ближайшего сокомандника и направиться к нему
		if (!fo::func::ai_find_friend(source, peRange, 5) && !attacker.InDudeParty) {
			// ближайший сокомандник не был найден в радиусе зрения атакующего
			fo::GameObject* dead_critter = fo::func::combatAIInfoGetFriendlyDead(source); // получить труп криттера которого кто-то недавно убил
			if (dead_critter) {
				fo::func::ai_move_away(source, dead_critter, 10);       // отойти от убитого криттера
				fo::func::combatAIInfoSetFriendlyDead(source, nullptr); // очистка
			} else {
				// определить поведение когда не было убитых криттеров
				if (attacker.cap->getDistance() != fo::AIpref::distance::stay) {


				}
			}
			source->critter.combatState |= fo::CombatStateFlag::EnemyOutOfRange; // установить флаг (для перемещения NPC из боевого списка в не боевой список)
			DEV_PRINTF("\n[AI] My target is over max distance. I can't find my friends!");
			return;
		}
		DEV_PRINTF("\n[AI] My target is over max distance!");
		//if () //проверить очки действия
	}

	// функция отхода от цели [доступно только для типа Human]
	long moveAwayDistance = 0;
	fo::GameObject* moveAwayTarget = target; // moveAwayTarget - может измениться на последнего атакующего криттера, если текущая цель оказалась неактивна
	if (source->critter.getAP()) {
		moveAwayDistance = GetMoveAwayDistaceFromTarget(source, moveAwayTarget);
		if (target != moveAwayTarget) DEV_PRINTF("\n[AI] target != moveAwayTarget");
		DEV_PRINTF1("\n[AI] Try move away... %d", moveAwayDistance);
		// в том случае если изначально не было цели
		if (!target && moveAwayTarget && moveAwayDistance) MoveAwayFromTarget(source, moveAwayTarget, moveAwayDistance);
	}

	if (!attacker.InDudeParty) SetMoveBonusAP(source); // добавить AP атакующему только для перемещения, этот бонус можно использовать совместно со сложностью боя (только для типа Biped)

	/*
		Тактическое укрытие: определено для всех типов Biped кроме Gecko
		для charge - недоступно, если цель находится на дистанции больше, чем атакующий может иметь очков действий (атакующий будет приближаться к цели)
		для stay   - недоступно (можно позволить в пределах 3 гексов)
	*/
	long coverTile = (!moveAwayTarget || source->critter.getAP() <= 0 || attacker.cap->getDistance() == fo::AIpref::distance::stay ||
					  !AttackerIsHumanoid() ||
					  (attacker.cap->getDistance() == fo::AIpref::distance::charge && fo::func::obj_dist(source, moveAwayTarget) > attacker.maxAP))
	                 ? -1 // не использовать укрытие
	                 : GetCoverBehindObjectTile(source, moveAwayTarget, source->critter.getAP() + 1, source->critter.getAP());

	/************************************************************************************
		Поведение: Для атакующих не состоящих в партии игрока.
		У атакующего нет цели (атакующий не видит целей), он получил повреждения в предыдущем туре
		и кто-то продолжает стрелять по нему, при этом его обидчик не мертв.
	************************************************************************************/
	//if (!target && !attacker.InDudeParty) { // цели нет, и атакующий не находится в партии игрока
	//	if (source->critter.damageLastTurn > 0 && source->critter.getHitTarget() && source->critter.getHitTarget()->critter.IsNotDead()) {
	//		// пытаться спрятаться за ближайшее укрытие (только для типа Biped)

	//		// если рядом нет укрытий?
	//		// есть ли убитые криттеры
	//		fo::GameObject* dead_critter = fo::func::combatAIInfoGetFriendlyDead(source); // получить труп криттера которого кто-то недавно убил
	//		if (dead_critter) {
	//			fo::func::ai_move_away(source, dead_critter, 10); // отойти от убитого криттера
	//			fo::func::combatAIInfoSetFriendlyDead(source, nullptr); // очистка
	//		} else {
	//			fo::func::debug_printf("\n[AI] %s: FLEEING: Somebody is shooting at me that I can't see!", attacker.name); // Бегство: кто-то стреляет в меня, но я этого не вижу!
 //               fo::func::ai_run_away(source, 0); // убегаем от игрока
	//		}
	//		return; // обязательно иначе возникнет конфликт поведения
	//	}
	//}

	/************************************************************************************
		Поведение: Для случаев когда цель не была найдена.
		1. Если атакующий принадлежит к партийцам игрока, то в случае если цель не была найдена в функции ai_danger_source
		   то сопартиец должен направиться к игроку если он находится на расстоянии превышающим заданную дистанцию.
		2. Если атакующий не принадлежит к партийцам игрока, то он должен найти своего ближайшего со-командника
		   у которого есть цель и идти к нему.
	************************************************************************************/
	if (!target) {
		DEV_PRINTF1("\n[AI] %s: I no have target!", attacker.name);
		fo::GameObject* critter = fo::func::ai_find_nearest_team_in_combat(source, source, 1); // найти ближайшего со-комадника у которого есть цель (проверить цели)

		// атакующий из команды игрока
		if (!critter && !source->critter.teamNum) critter = fo::var::obj_dude;

		long distance = (attacker.InDudeParty) ? getAIPartyMemberDistances(attacker.cap->distance) : 8; // default (было 5)

		DEV_PRINTF1("\n[AI] Find team critter: %s", (critter) ? fo::func::critter_name(critter) : "None");
		if (critter) {
			long dist = fo::func::obj_dist(source, critter);

			// если атакующий находится в радиусе восприятия со-командника, то взять его цель
			if (dist <= (fo::func::stat_level(source, fo::STAT_pe) + 5)) {
				fo::GameObject* _target = critter->critter.getHitTarget();
				if (_target->critter.IsNotDead()) {
					target = _target;
					DEV_PRINTF1("\n[AI] Pick target from critter: %s. Try Attack!", fo::func::critter_name(target));
					goto tryAttack;
				}
			}

			// дистанция больше чем определено по умолчанию, идем к криттеру из своей команды который имеет цель
			if (dist > distance) {
				dist -= distance; // | 7-6=1
				dist = fo::func::roll_random(dist, dist + 3);
				fo::func::ai_move_steps_closer(source, critter, dist, 0);
				DEV_PRINTF1("\n[AI] Move close to: %s", fo::func::critter_name(critter));
			}
			else if (!attacker.InDudeParty) {
				// если атакующий уже находится в радиусе, то идти к гексу где последний раз был атакован critter
				if (lastAttackerTile.find(critter) != lastAttackerTile.cend()) {
					long tile = lastAttackerTile[critter];
					if (fo::func::obj_blocking_at(source, tile, source->elevation)) {
						for (size_t r = 0; r < 6; r++) {
							long _tile = fo::func::tile_num_in_direction(tile, r, 1);
							if (!fo::func::obj_blocking_at(source, _tile, source->elevation)) {
								DEV_PRINTF("\n[AI] Pick alternate move tile from near team critter.");
								AIHelpers::CombatMoveToTile(source, _tile, source->critter.getAP());
								break;
							}
						}
					} else {
						DEV_PRINTF("\n[AI] Move to tile from near team critter.");
						AIHelpers::CombatMoveToTile(source, tile, source->critter.getAP());
					}
				}
			}
			// со следующего хода (если уже закончились AP) возможно, что атакующий получит цель которую он видит
		} else {
			/************************************************************************************
				Поведение: Для атакующих не состоящих в партии игрока.
				У атакующего нет цели (атакующий не видит целей), он получил повреждения в предыдущем туре
				и кто-то продолжает стрелять по нему, при этом его обидчик не мертв, и его ближайший со-командник не найден.
			************************************************************************************/
			if (!attacker.InDudeParty && source->critter.damageLastTurn > 0 && source->critter.getHitTarget() && source->critter.getHitTarget()->critter.IsNotDead()) {
				// пытаться спрятаться за ближайшее укрытие (только для типа Biped)

				// но если рядом нет укрытий?

				// есть ли убитые криттеры
				fo::GameObject* dead_critter = fo::func::combatAIInfoGetFriendlyDead(source); // получить труп криттера которого кто-то недавно убил
				if (dead_critter) {
					fo::func::ai_move_away(source, dead_critter, 10);       // отойти от убитого криттера
					fo::func::combatAIInfoSetFriendlyDead(source, nullptr); // очистка
				} else {
					fo::func::debug_printf("\n[AI] %s: FLEEING: Somebody is shooting at me that I can't see!", attacker.name); // Бегство: кто-то стреляет в меня, но я этого не вижу!
					fo::func::ai_run_away(source, 0); // убегаем от игрока TODO: реализовать рандомное убегание от стороны игрока
				}
				return;
			}
		}
	}

	/************************************************************************************
		Если все еще остались очки действия у атакующего
	************************************************************************************/
	if (source->critter.getAP() > 0) {
		fo::func::debug_printf("\n[AI] %s had extra %d AP's to use!", attacker.name, source->critter.getAP());
		if (moveAwayTarget) {
			// условия для отхода в укрытие
			if (attacker.cover.tile != -1 && source->critter.damageLastTurn > 0) {                      // атакующий получил повреждения в прошлом ходе
				if ((attacker.cover.distance / 2) <= source->critter.getAP() &&                         // дистанция до плитки укрытия меньше, чем очков действия
					(fo::func::stat_level(source, fo::STAT_current_hp) < (attacker.cap->min_hp * 2) ||  // текущее HP меньше, чем определено в min_hp x 2
					fo::func::combatai_rating(source) * 2 < fo::func::combatai_rating(moveAwayTarget))) // боевой рейтинг цели, больше чем рейтинг атакующего
				{
					coverTile = attacker.cover.tile;
				}
			}
			// отход в укрытие имеет приоритет над другими функциями перестраивания
			if (coverTile == - 1 || AIHelpers::CombatMoveToTile(source, coverTile, source->critter.getAP()) != 0) {
				if (moveAwayDistance) MoveAwayFromTarget(source, moveAwayTarget, moveAwayDistance);
				fo::func::cai_perform_distance_prefs(source, moveAwayTarget);
			}
		} else {
			// нет цели, попробовать найти другую цель (аналог опции TrySpendAPs)
			if (lastCombatAP != source->critter.getAP()) {
				lastCombatAP = source->critter.getAP(); // для того чтобы не было зависания в цикле
				goto findNewTarget;
			}
		}
		DEV_PRINTF1("\n[AI] left extra %d AP's", source->critter.getAP());
	}
	if (attacker.BonusMoveAP) RemoveMoveBonusAP(source);  // удалить полученные бонусные очки передвижения

	DEV_PRINTF1("\n[AI] End combat: %s\n", attacker.name);
}

/////////////////////////////////////////////////////////////////////////////////////////

// Выбрать другое подходящее оружие из инвентаря для атаки по цели, когда не хватает очков действия
static fo::GameObject* FindSafeWeaponAttack(fo::GameObject* source, fo::GameObject* target, fo::GameObject* hWeapon) {
	long distance = fo::func::obj_dist(source, target);

	fo::GameObject* pickWeapon = nullptr;
	fo::Proto* proto;
	DWORD slotNum = -1;

	while (true)
	{
		fo::GameObject* item = fo::func::inven_find_type(source, fo::item_type_weapon, &slotNum);
		if (!item) break;
		if (hWeapon && hWeapon->protoId == item->protoId) continue; // это тоже самое оружие

		// проверить дальность оружия до цели
		fo::func::proto_ptr(item->protoId, &proto);
		if (proto->item.weapon.AttackInRange(distance) == false) continue;

		if (proto->item.weapon.AttackHaveEnoughAP(source->critter.getAP()) &&
			(fo::func::ai_can_use_weapon(source, item, AttackType::ATKTYPE_RWEAPON_PRIMARY) ||
			fo::func::ai_can_use_weapon(source, item, AttackType::ATKTYPE_LWEAPON_SECONDARY)))
		{
			if ((item->item.ammoPid == -1 || // оружие не имеет заряжаемых патронов
				fo::func::item_w_curr_ammo(item)) &&
				!fo::func::combat_safety_invalidate_weapon_func(source, pickWeapon, AttackType::ATKTYPE_RWEAPON_PRIMARY, target, 0, 0)) // weapon is safety
			{
				pickWeapon = fo::func::ai_best_weapon(source, pickWeapon, item, target); // выбрать лучшее из доступных
			}
		}
	}
	return pickWeapon;
}

// Анализируем ситуацию после атаки когда не хватает AP для продолжения атаки
static AIAttackResult __fastcall CheckResultAfterAttack(fo::GameObject* source, fo::GameObject* target, fo::GameObject* &weapon, long &hitMode) {

	if (source->critter.getAP() <= 0) return AIAttackResult::NoMovePoints;

	// цель мертва после атаки, нужно взять другую цель (аналог поведения включенной NPCsTryToSpendExtraAP опции)
	if (target->critter.IsDead()) return AIAttackResult::TargetDead;

	long dist = fo::func::obj_dist(source, target) - 1;
	if (dist < 0) dist = 0;
	long costAP = game::Items::item_w_mp_cost(source, AttackType::ATKTYPE_PUNCH, 0);
	fo::GameObject* handItem = fo::func::inven_right_hand(source);

	// условие когда атакующий потерял оружие после атаки
	if (weapon && !handItem) {
		DEV_PRINTF("\n[AI] Attack result: LOST WEAPON\n");
		// проверить дистанцию до цели, если дистанция позволяет то использовать рукопашную атаку
		// иначе подобрать оружие или достать другое из инвентаря (при этом необходимо вычесть 3 AP)
		if (dist >= 3 || ((dist + costAP) > source->critter.getAP())) return AIAttackResult::LostWeapon;
	}

	// еще остались очки действий, но не хватает для атаки, попробовать найти подходящее оружие c меньшим AP в инвентаре и повторить атаку
	if (handItem) {
		fo::GameObject* findWeapon = FindSafeWeaponAttack(source, target, handItem);
		if (findWeapon) {
			DEV_PRINTF("\n[AI] Attack result: SWITCH WEAPON\n");
			long _hitMode = fo::func::ai_pick_hit_mode(source, findWeapon, target);
			if (source->critter.getAP() >= game::Items::item_weapon_mp_cost(source, findWeapon, _hitMode, 0)) {
				fo::func::inven_wield(source, findWeapon, fo::InvenType::INVEN_TYPE_RIGHT_HAND);
				__asm call fo::funcoffs::combat_turn_run_;
				hitMode = _hitMode;
				weapon = findWeapon;
				return AIAttackResult::ReTryAttack;
			}
		}
	}

	// не было найдено другого оружия для продолжения атаки, использовать рукопашную атаку если цель расположена достаточно близко
	if (dist < 4) {
		if (source->critter.getAP() >= (dist + costAP)) { // очков действия хватает чтобы сделать удар
			DEV_PRINTF("\n[AI] Attack result: UNARMED ATTACK\n");
			if (dist > 0 && AIHelpers::ForceMoveToTarget(source, target, dist) == -1) return AIAttackResult::Default; // не удалось приблизиться к цели
			hitMode = AttackType::ATKTYPE_PUNCH; // установить тип атаки
			weapon = nullptr;                    // без оружия
			return AIAttackResult::ReTryAttack;
		}
	}
	return AIAttackResult::Default;
}

static void __declspec(naked) ai_try_attack_hack_after_attack() {
	static const uint32_t ai_try_attack_hack_go_next_ret  = 0x42A9F2;
	__asm {
		test eax, eax; // -1 error attack
		jl   end;
		lea  eax, [esp + 0x364 - 0x38 + 4]; // hit_mode
		lea  ebx, [esp + 0x364 - 0x3C + 4]; // right_weapon
		push eax;
		mov  edx, ebp;
		mov  ecx, esi;
		push ebx;
		call CheckResultAfterAttack;
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
		jmp  ai_try_attack_hack_go_next_ret;
	}
}

static fo::GameObject* __fastcall SetLastAttacker(fo::GameObject* attacker, fo::GameObject* target) {
	lastAttackerTile[target] = attacker->tile;
	return attacker;
}

static void __declspec(naked) combat_attack_hook() {
	__asm {
		push ecx;
		mov  ecx, eax;
		call SetLastAttacker;
		pop  ecx;
		mov  edx, edi;
		jmp  fo::funcoffs::combatAIInfoSetLastTarget_;
	}
}

void AIBehavior::init() {

	// Enables the use of the RunAwayMode value from the AI-packet for the NPC
	// the min_hp value will be calculated as a percentage of the maximum number of NPC health points, instead of using fixed min_hp values
	npcPercentMinHP = (GetConfigInt("CombatAI", "NPCRunAwayMode", 0) > 0);

	//////////////////// Combat AI improved behavior //////////////////////////

	if (GetConfigInt("CombatAI", "SmartBehavior", 0) > 0) {
		HookCall(0X4230E8, combat_attack_hook);
		LoadGameHook::OnCombatStart() += []() { lastAttackerTile.clear(); };

		// Override combat_ai_ engine function
		HookCall(0x422B94, combat_ai_extended);
		SafeWrite8(0x422B91, 0xF1); // mov  eax, esi > mov ecx, esi
		// swap asm codes
		SafeWrite32(0x422B89, 0x8904518B);
		SafeWrite8(0x422B8D, 0xF1); // mov  eax, esi > mov ecx, esi

		// Don't pickup a weapon if its magazine is empty and there are no ammo for it
		HookCall(0x429CF2, ai_search_environ_hook_weapon);

		/**** Точки входа в функции AI_Try_Attack ****/

		//Точки непосредственной атаки ai_attack_ (0x42AE1D, 0x42AE5C)

		// Мove away from the target if the target is near
		MakeCalls(ai_try_attack_hack_after_attack, { 0x42AE40, 0x42AE7F }); //ai_try_attack_hack_move

		// Точка смены оружия, когда у NPC не хватает очков действия для совершения атаки
		// Fixed switching weapons when action points is not enough
		HookCall(0x42AB57, ai_try_attack_hook_switch_fix); // ai_try_attack_hook_switch_weapon_not_enough_AP

		// Точка смены оружия, когда в оружие у NPC нет патронов и патроны не были найдены в инвентаре или на земле
		// перед этим происходит убирание текущего оружия из слота (возможно что эта точка не нужна будет)
		//HookCall(0ч42AB3B, ai_try_attack_hook_switch_weapon_not_found_ammo);

		// Точка смены оружия, если текущее оружие небезопасно для текущей ситуации
		// или найти оружие если NPC безоружен и цель не относится к типу Biped или цель вооружена
		// и еще какие-то условия
		HookCall(0x42A905, ai_try_attack_hook_switch); //HookCall(0x42A905, ai_try_attack_hook_switch_weapon_on_begin_turn);

		// Точка смены оружия, когда превышен радиус действия атаки и NPC безоружен
		//HookCall(0x42AC05, ai_try_attack_hook_switch_weapon_out_of_range);

		// Точка проверки сделать атаку по цели, возвращается результат проверки (0-7)
		HookCall(0x42A92F, ai_try_attack_hook); //HookCall(0x42A92F, ai_try_attack_hook_check_attack);

		// Точка входа, попытка найти патроны на земле, если они не были найдены в инвентаре
		//HookCall(0x42AA25, ai_try_attack_hook_check_attack);

		// Точка входа при блокировании выстрела по цели
		// Checks the movement path for the possibility а shot, if the shot to the target is blocked
		HookCall(0x42AC55, ai_try_attack_hook_shot_blocked);

		// Точка входа когда дистанция превышает радиус атаки
		//HookCall(0x42ABD7, ai_try_attack_hook_out_of_range);
		// дополнительная точка когда атака по цели не эффективна
		//HookCall(0x42ABA8, ai_try_attack_hook_out_of_range_bad_tohit); // (блокируется в AI.cpp)


		// Исправление функции для отрицательного значения дистанции которое игнорирует условия дистанции stay/stay_closer
		HookCall(0x429FDB, ai_move_steps_closer_hook); // jle hook

	}
		
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
		//HookCall(0x42A92F, ai_try_attack_hook);
		//HookCall(0x42A905, ai_try_attack_hook_switch);
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

	// When npc does not have enough AP to use the weapon, it begin looking in the inventory another weapon to use,
	// if no suitable weapon is found, then are search the nearby objects(weapons) on the ground to pick-up them
	// This fix prevents pick-up of the object located on the ground, if npc does not have the full amount of AP (ie, the action does occur at not the beginning of its turn)
	// or if there is not enough AP to pick up the object on the ground. Npc will not spend its AP for inappropriate use
	if (GetConfigInt("CombatAI", "ItemPickUpFix", 0)) {
		HookCall(0x429CAF, ai_search_environ_hook);
	}

	// Fix distance in ai_find_friend_ function (only for sfall extended)
	SafeWrite8(0x428AF5, 0xC8); // cmp ecx, eax > cmp eax, ecx

	//// Fixed switching weapons when action points is not enough
	//if (GetConfigInt("CombatAI", "NPCSwitchingWeaponFix", 1)) {
	//	HookCall(0x42AB57, ai_try_attack_hook_switch_fix);
	//}

	//GetConfigInt("CombatAI", "DifficultyMode", 0)
	/*Tough AI

	Agile AI*/
}

}
