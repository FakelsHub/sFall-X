/*
 *    sfall
 *    Copyright (C) 2021  The sfall team
 *
 */

#pragma once

namespace sfall
{

class AIInventory {
public:
	static void CorpsesLootingHack();

	// Оригинальнная функция ai_best_weapon_ с вызовом крючка
	static fo::GameObject* BestWeapon(fo::GameObject* source, fo::GameObject* weapon1, fo::GameObject* weapon2, fo::GameObject* target);

	// Проверяет наличие патронов к оружию на карте или в инвентере убитых криттеров а так же в инвентаре самого NPC
	static long AICheckAmmo(fo::GameObject* weapon, fo::GameObject* critter);

	static fo::GameObject* SearchInventoryItemType(fo::GameObject* source, long itemType, fo::GameObject* object, fo::GameObject* weapon);

	// Альтернативная реализация функции ai_search_inven_weap_
	static fo::GameObject* GetInventoryWeapon(fo::GameObject* source, bool checkAP, bool useHand);

	// Возвращает первый наденные патроны к оружию в инвентаре криттера
	static fo::GameObject* GetInventAmmo(fo::GameObject* critter, fo::GameObject* weapon);

	// Проверяет имеет ли криттер в своем инвентаре патроны к оружию для перезарядки (возвращает их количество)
	static long CritterHaveAmmo(fo::GameObject* critter, fo::GameObject* weapon);

	// Находит подходящее оружие в инвентаре криттера для совершения атаки по цели
	static fo::GameObject* FindSafeWeaponAttack(fo::GameObject* source, fo::GameObject* target, fo::GameObject* hWeapon, fo::AttackType &outHitMode);

	static bool AITryReloadWeapon(fo::GameObject* critter, fo::GameObject* weapon, fo::GameObject* ammo);

	static fo::GameObject* AIRetrieveCorpseItem(fo::GameObject* source, fo::GameObject* itemRetrive);

	// Ищет предмет типа патронов на карте подходящих к указанному оружию
	static fo::GameObject* ai_search_environ_ammo(fo::GameObject* critter, fo::GameObject* weapon);

	// Аналог функции ai_search_environ_, только с той разницей, что ищет требуемый предмет на карте в инвентаре убитых криттеров
	static long ai_search_environ_corpse(fo::GameObject* source, long itemType, fo::GameObject* &itemGround, fo::GameObject* weapon);
};

}
