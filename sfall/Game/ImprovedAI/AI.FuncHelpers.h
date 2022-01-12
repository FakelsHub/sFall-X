/*
 *    sfall
 *    Copyright (C) 2020  The sfall team
 *
 */

#pragma once

namespace game
{
namespace imp_ai
{

class AIHelpersExt {
public:

	static bool AICanUseWeapon(fo::GameObject* weapon);

	static fo::GameObject* GetNearestEnemyCritter(fo::GameObject* source);

	// Получает стоимость очков действия для текущего оружия в правом слоте и указанного режима
	// в случае ошибки возвращается -1
	static long GetCurrenShootAPCost(fo::GameObject* source, long modeHit, long isCalled);

	// Получает стоимость очков действия для текущего оружия в правом слоте и цели атакующего
	static long GetCurrenShootAPCost(fo::GameObject* source, fo::GameObject* target);

	// Получает стоимость очков действия для оружия и цели атакующего
	static long GetCurrenShootAPCost(fo::GameObject* source, fo::GameObject* target, fo::GameObject* weapon);

	static long CombatMoveToObject(fo::GameObject* source, fo::GameObject* target, long dist);
	static long CombatMoveToTile(fo::GameObject* source, long tile, long dist, bool run = false);

	// Принудительно заставит NPC подойти к цели на указанную дистанцию (игнорируется stay и stay_close)
	static long ForceMoveToTarget(fo::GameObject* source, fo::GameObject* target, long dist);

	// Принудительно заставит NPC подойти к цели на указанную дистанцию (игнорируется stay_close)
	static long MoveToTarget(fo::GameObject* source, fo::GameObject* target, long dist);

	// Оценивает и возвращает одно из оружие чей навык использования для AI лучше, если навыки равны то возвращается sWeapon оружие
	static fo::GameObject* AICheckWeaponSkill(fo::GameObject* source, fo::GameObject* hWeapon, fo::GameObject* sWeapon);

	static fo::AttackSubType GetWeaponSubType(fo::GameObject* item, bool isSecond);
	static fo::AttackSubType GetWeaponSubType(fo::GameObject* item, fo::AttackType hitMode);

	static bool ItemIsGun(fo::GameObject* item);

	// Проверяет относится ли предмет к типу стрелковому или метательному оружию
	static bool WeaponIsGunOrThrowing(fo::GameObject* item, long type = -1);

	static long SearchTileShoot(fo::GameObject* target, long &inOutTile, char* path, long len);

	static bool TileWayIsDoor(long tile, long elev);

	static long GetFreeTile(fo::GameObject* source, long tile, long distMax, long dir);
	static long GetDirFreeTile(fo::GameObject* source, long tile, long distMax);

	static long GetRandomTile(long sourceTile, long minDist, long maxDist);
	static long GetRandomTileToMove(fo::GameObject* source, long minDist, long maxDist);

	static long GetRandomDistTile(fo::GameObject* source, long tile, long distMax);

	static bool CanSeeObject(fo::GameObject* source, fo::GameObject* target);

	static fo::GameObject* __fastcall obj_ai_move_blocking_at(fo::GameObject* source, long tile, long elev);
	static void obj_ai_move_blocking_at_(); // wrapper

	static fo::GameObject* __fastcall obj_ai_shoot_blocking_at(fo::GameObject* source, long tile, long elev);
	static void obj_ai_shoot_blocking_at_(); // wrapper

};

}
}
