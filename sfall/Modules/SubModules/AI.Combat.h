/*
 *    sfall
 *    Copyright (C) 2021  The sfall team
 *
 */

#pragma once

namespace sfall
{

class AICombat {
public:
	static void init(bool);

	static void AttackerSetHitMode(fo::AttackType mode);
	static fo::AttackType AttackerHitMode();
	static long AttackerBonusAP();
};

}
