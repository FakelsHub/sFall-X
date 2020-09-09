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

	static long PlaySfallSound(const char* path, long mode);
	static void __stdcall StopSfallSound(uint32_t id);

	static long CalculateVolumeDB(long masterVolume, long passVolume);

	static void SoundLostFocus(long isActive);
};

}
