#pragma once

#include "Module.h"

namespace sfall
{

class CritterPoison : public Module {
public:
	const char* name() { return "CritterPoison"; }
	void init();

	static long adjustPoisonHP_Default;
	static long adjustPoisonHP; // temp value from HOOK_ADJUSTPOISON

	static void SetDefaultAdjustPoisonHP(long value);
};

extern void critter_adjust_poison_hack_fix();

}