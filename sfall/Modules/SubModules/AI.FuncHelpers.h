/*
 *    sfall
 *    Copyright (C) 2020  The sfall team
 *
 */

#pragma once

namespace sfall
{

class AIHelpers {
public:
	// Рекурсивная функция, возвращающая криттера принадлежащей указанной команде расположенного на линии огня указанной цели или не простреливаемый объект
	//static fo::GameObject* CheckShootAndTeamCritterOnLineOfFire(fo::GameObject* object, long targetTile, long team);

	// The return of the friendly critter that are located on the line of fire
	//static fo::GameObject* CheckFriendlyFire(fo::GameObject* target, fo::GameObject* attacker);

	// The return of the friendly critter that are located on the line of fire or any other non-shooting object
	// destTile - тайл с которого будет проверяться линия
	static fo::GameObject* CheckFriendlyFire(fo::GameObject* target, fo::GameObject* attacker, long destTile);

	static bool AttackInRange(fo::GameObject* source, fo::GameObject* weapon, long distance);

	static fo::GameObject* GetNearestEnemyCritter(fo::GameObject* source);

	// Альтернативная реализация функции ai_search_inven_weap_
	static fo::GameObject* GetInventoryWeapon(fo::GameObject* source, bool checkAP, bool useHand);

	// Получает стоимость очков действия для текущего оружия в правом слоте и указанного режима
	// в случае ошибки возвращается -1
	static long GetCurrenShootAPCost(fo::GameObject* source, long modeHit, long isCalled);

	// Получает стоимость очков действия для текущего оружия в правом слоте и цели атакующего
	static long GetCurrenShootAPCost(fo::GameObject* source, fo::GameObject* target);

	// Получает стоимость очков действия для оружия и цели атакующего
	static long GetCurrenShootAPCost(fo::GameObject* source, fo::GameObject* target, fo::GameObject* weapon);

	static long CombatMoveToObject(fo::GameObject* source, fo::GameObject* target, long dist);

	static long CombatMoveToTile(fo::GameObject* source, long tile, long dist);

	static long CombatRunToTile(fo::GameObject* source, long tile, long dist);

	// Принудительно заставит NPC подойти к цели на указанную дистанцию (игнорируется stay и stay_close)
	static long ForceMoveToTarget(fo::GameObject* source, fo::GameObject* target, long dist);

	// Принудительно заставит NPC подойти к цели на указанную дистанцию (игнорируется stay_close)
	static long MoveToTarget(fo::GameObject* source, fo::GameObject* target, long dist);

	// Оценивает и возвращает одно из оружие чей навык использования для AI лучше, если навыки равны то возвращается sWeapon оружие
	static fo::GameObject* AICheckWeaponSkill(fo::GameObject* source, fo::GameObject* hWeapon, fo::GameObject* sWeapon);

	static fo::AttackSubType GetWeaponSubType(fo::GameObject* item, bool isSecond);

	static bool CanSeeObject(fo::GameObject* source, fo::GameObject* target);

	// Проверяет относится ли предмет к типу стрелковому или метательному оружию
	static bool IsGunOrThrowingWeapon(fo::GameObject* item, long type = -1);

	static fo::GameObject* GetInventAmmo(fo::GameObject* critter, fo::GameObject* weapon);

	static long CritterHaveAmmo(fo::GameObject* critter, fo::GameObject* weapon);

	static bool AITryReloadWeapon(fo::GameObject* critter, fo::GameObject* weapon, fo::GameObject* ammo);

	static long GetFreeTile(fo::GameObject* source, long tile, long distMax);

	static long GetRandomTile(fo::GameObject* source, long min, long max);

	static long GetRandomDistTile(fo::GameObject* source, long tile, long distMax);
};

}
