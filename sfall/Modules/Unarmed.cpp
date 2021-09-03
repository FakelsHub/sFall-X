/*
 *    sfall
 *    Copyright (C) 2021  The sfall team
 *
 */

#include "..\FalloutEngine\Fallout2.h"
#include "..\main.h"

#include "Unarmed.h"

namespace sfall
{

class Hits {
	struct HitsData {
		long reqLevel = 0;        // intface_update_items_
		long reqSkill = 0;
		long reqStat[7] = {0};
		long minDamage = 1; // default for all
		long maxDamage = 2; // default for all (maxDamage + STAT_melee_dmg)
		long bonusDamage = 0;     // item_w_damage_
		long bonusCrit = 0;       // compute_attack_
		long costAP = 3;          // item_w_mp_cost_
		bool isPenetrate = false; // compute_damage_
	};

public:
	enum Unarmed {
		strong_punch,
		hammer_punch,
		haymaker,
		jab,
		palm_strike,
		piercing_strike,

		strong_kick,
		snap_kick,
		power_kick,
		hip_kick,
		hook_kick,
		piercing_kick
	};

	HitsData hit[12];

	Hits() {
		// pri 1
		hit[strong_punch].reqLevel = 1;
		hit[strong_punch].reqSkill = 55;
		hit[strong_punch].reqStat[fo::Stat::STAT_ag] = 6;
		hit[strong_punch].bonusDamage = 3;
		// sec
		hit[jab].reqLevel = 5;
		hit[jab].reqSkill = 75;
		hit[jab].reqStat[fo::Stat::STAT_st] = 4;
		hit[jab].reqStat[fo::Stat::STAT_ag] = 6;
		hit[jab].bonusDamage = 3;
		hit[jab].bonusCrit = 10;

		// pri 2
		hit[hammer_punch].reqLevel = 6;
		hit[hammer_punch].reqSkill = 75;
		hit[hammer_punch].reqStat[fo::Stat::STAT_st] = 5;
		hit[hammer_punch].reqStat[fo::Stat::STAT_ag] = 6;
		hit[hammer_punch].bonusDamage = 5;
		hit[hammer_punch].bonusCrit = 5;
		// sec
		hit[palm_strike].reqLevel = 12;
		hit[palm_strike].reqSkill = 115;
		hit[palm_strike].reqStat[fo::Stat::STAT_st] = 5;
		hit[palm_strike].reqStat[fo::Stat::STAT_ag] = 7;
		hit[palm_strike].bonusDamage = 7;
		hit[palm_strike].bonusCrit = 20;
		hit[palm_strike].isPenetrate = true;
		hit[palm_strike].costAP = 6;
		// pri 3
		hit[haymaker].reqLevel = 9;
		hit[haymaker].reqSkill = 100;
		hit[haymaker].reqStat[fo::Stat::STAT_st] = 5;
		hit[haymaker].reqStat[fo::Stat::STAT_ag] = 7;
		hit[haymaker].bonusDamage = 7;
		hit[haymaker].bonusCrit = 15;
		// sec
		hit[piercing_strike].reqSkill = 16;
		hit[piercing_strike].reqSkill = 130;
		hit[piercing_strike].reqStat[fo::Stat::STAT_st] = 5;
		hit[piercing_strike].reqStat[fo::Stat::STAT_ag] = 7;
		hit[piercing_strike].bonusDamage = 10;
		hit[piercing_strike].bonusCrit = 40;
		hit[piercing_strike].isPenetrate = true;
		hit[piercing_strike].costAP = 8;

		// kick pri 1
		hit[strong_kick].reqLevel = 1;
		hit[strong_kick].reqSkill = 40;
		hit[strong_kick].reqStat[fo::Stat::STAT_ag] = 6;
		hit[strong_kick].bonusDamage = 5;
		hit[strong_kick].costAP = 4;
		// sec
		hit[hip_kick].reqLevel = 6;
		hit[hip_kick].reqSkill = 60;
		hit[hip_kick].reqStat[fo::Stat::STAT_st] = 6;
		hit[hip_kick].reqStat[fo::Stat::STAT_ag] = 7;
		hit[hip_kick].bonusDamage = 7;
		hit[hip_kick].costAP = 7;

		// pri 2
		hit[snap_kick].reqLevel = 6;
		hit[snap_kick].reqSkill = 60;
		hit[snap_kick].reqStat[fo::Stat::STAT_ag] = 6;
		hit[snap_kick].bonusDamage = 7;
		hit[snap_kick].costAP = 4;
		// sec
		hit[hook_kick].reqLevel = 12;
		hit[hook_kick].reqSkill = 100;
		hit[hook_kick].reqStat[fo::Stat::STAT_st] = 6;
		hit[hook_kick].reqStat[fo::Stat::STAT_ag] = 7;
		hit[hook_kick].bonusDamage = 9;
		hit[hook_kick].bonusCrit = 10;
		hit[hook_kick].isPenetrate = true;
		hit[hook_kick].costAP = 7;

		// pri 3
		hit[power_kick].reqLevel = 9;
		hit[power_kick].reqSkill = 80;
		hit[power_kick].reqStat[fo::Stat::STAT_st] = 6;
		hit[power_kick].reqStat[fo::Stat::STAT_ag] = 6;
		hit[power_kick].bonusDamage = 9;
		hit[power_kick].bonusCrit = 5;
		hit[power_kick].costAP = 4;
		// sec
		hit[piercing_kick].reqLevel = 15;
		hit[piercing_kick].reqSkill = 125;
		hit[piercing_kick].reqStat[fo::Stat::STAT_st] = 6;
		hit[piercing_kick].reqStat[fo::Stat::STAT_ag] = 8;
		hit[piercing_kick].bonusDamage = 12;
		hit[piercing_kick].bonusCrit = 50;
		hit[piercing_kick].isPenetrate = true;
		hit[piercing_kick].costAP = 9;
	}
};

Hits unarmed;

void Unarmed::init() {

	unarmed = Hits();

	auto fileUnarmed = IniReader::GetConfigString("Misc", "UnarmedHits", "", MAX_PATH);
	if (!fileUnarmed.empty()) {
		const char* file = fileUnarmed.insert(0, ".\\").c_str();
		if (!(GetFileAttributes(file) == INVALID_FILE_ATTRIBUTES)) { // check exist file
			char hit[6] ="0\0";
			for (size_t i = 0; i < 12;)
			{
				// [0]
				// Level=
				// Skill=
				// Stat0=
				// ....
				// Stat6=
				unarmed.hit[i].reqLevel    = IniReader::GetInt(hit, "ReqLevel",    unarmed.hit[i].reqLevel, file);
				unarmed.hit[i].reqSkill    = IniReader::GetInt(hit, "SkillLevel",  unarmed.hit[i].reqSkill, file);
				unarmed.hit[i].minDamage   = IniReader::GetInt(hit, "MinDamage",   unarmed.hit[i].minDamage, file);
				unarmed.hit[i].maxDamage   = IniReader::GetInt(hit, "MaxDamage",   unarmed.hit[i].maxDamage, file);
				unarmed.hit[i].bonusDamage = IniReader::GetInt(hit, "BonusDamage", unarmed.hit[i].bonusDamage, file);
				unarmed.hit[i].bonusCrit   = IniReader::GetInt(hit, "BonusCrit",   unarmed.hit[i].bonusCrit, file);
				unarmed.hit[i].costAP      = IniReader::GetInt(hit, "CostAP",      unarmed.hit[i].costAP, file);
				unarmed.hit[i].isPenetrate = IniReader::GetInt(hit, "Penetrate",   unarmed.hit[i].isPenetrate, file) != 0;

				char stat[10] =  "Stat0\0";
				for (size_t s = 0; s < 7;) {
					unarmed.hit[i].reqStat[s] = IniReader::GetInt(hit, stat, unarmed.hit[i].reqStat[s], file);
					if (++s < 7) _itoa(s, &stat[4], 10);
				}
				if (++i < 12) _itoa(i, hit, 10);
			}
		}
	}
}

void Unarmed::exit() {}

}
