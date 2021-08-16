/*
 *    sfall
 *    Copyright (C) 2008 - 2021 Timeslip and sfall team
 *
 */

#pragma once

namespace game
{

#include "..\FalloutEngine\Fallout2.h"

class CombatAI {
public:
	static void init();

	static bool CombatAI::ai_can_use_weapon(fo::GameObject* source, fo::GameObject* weapon, long hitMode);

	static void __stdcall CombatAI::ai_check_drugs(fo::GameObject* source);
};

}