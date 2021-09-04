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

#ifndef NDEBUG
#define DEV_PRINTF(info)			fo::func::debug_printf(info)
#define DEV_PRINTF1(info, a)		fo::func::debug_printf(info, a)
#define DEV_PRINTF2(info, a, b)		fo::func::debug_printf(info, a, b)
#define DEV_PRINTF3(info, a, b, c)	fo::func::debug_printf(info, a, b, c)
#else
#define DEV_PRINTF(info)
#define DEV_PRINTF1(info, a)
#define DEV_PRINTF2(info, a, b)
#define DEV_PRINTF3(info, a, b, c)
#endif

class AIBehavior {
public:
	static void init(bool);

	enum class AttackResult : long
	{
		Default      = -1,
		TargetDead   = 4, // цель была убита
		NoMovePoints = 8, // нет очков для передвижения
		LostWeapon   = 9, // оружие упало
		ReTryAttack  = 10,
		BadToHit     = 11,
		MoveAway
	};

	static long __fastcall AICheckBeforeWeaponSwitch(fo::GameObject* target, long &hitMode, fo::GameObject* source, fo::GameObject* weapon);
};

constexpr int pickupCostAP = 3; // engine default cost

}
}