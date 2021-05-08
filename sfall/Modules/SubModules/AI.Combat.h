/*
 *    sfall
 *    Copyright (C) 2021  The sfall team
 *
 */

#pragma once

namespace sfall
{

enum CombatShootResult : long {
	Ok            = 0,
	NoAmmo        = 1,
	OutOfRange    = 2,
	NotEnoughAPs  = 3,
	TargetDead    = 4,
	ShootBlock    = 5,
	CrippledHand  = 6,
	CrippledHands = 7,
	NoActionPoint = 8, // add ext
};

enum class CombatDifficulty : long
{
	Easy   = 0,
	Normal = 1,
	Hard   = 2
};

class AICombat {
public:
	static void init(bool);

	static bool AICombat::npcPercentMinHP;
	static CombatDifficulty AICombat::combatDifficulty;

	static CombatShootResult combat_check_bad_shot(fo::GameObject* source, fo::GameObject* target, fo::AttackType hitMode, long isCalled);

	static void AttackerSetHitMode(fo::AttackType mode);
	static fo::AttackType AttackerHitMode();
	static long AttackerBonusAP();
	static long AttackerBodyType();
	static bool AttackerIsHumanoid();
	static fo::AIcap* AttackerAI();
};

}
