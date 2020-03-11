/*
 *    sfall
 *    Copyright (C) 2008 - 2020  The sfall team
 *
 */

#pragma once

#include "Module.h"

namespace sfall
{

class Sound : public Module {
public:
	const char* name() { return "Sounds"; }
	void init();
	void exit() override;

	static void* PlaySfallSound(const char* path, bool loop);
	static void __stdcall StopSfallSound(void* ptr);
};

}
