/*
 *    sfall
 *    Copyright (C) 2008 - 2021 Timeslip and sfall team
 *
 */

#pragma once

namespace game
{

class Items
{
public:
	static void init();

	// Implementing the item_w_primary_mp_cost and item_w_secondary_mp_cost engine functions in single function with the HOOK_CALCAPCOST hook
	// returns -1 in case of an error
	static long item_weapon_mp_cost(fo::GameObject* source, fo::GameObject* weapon, long hitMode, long isCalled);

	// Implementation of item_w_mp_cost_ engine function with the HOOK_CALCAPCOST hook
	static long __fastcall item_w_mp_cost(fo::GameObject* source, long hitMode, long isCalled);
};

}