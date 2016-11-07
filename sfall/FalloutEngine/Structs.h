/*
* sfall
* Copyright (C) 2008-2016 The sfall team
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include "Enums.h"


/******************************************************************************/
/* FALLOUT2.EXE structs should be placed here  */
/******************************************************************************/

// TODO: make consistent naming for all FO structs

struct TGameObj;

/*   26 */
#pragma pack(push, 1)
struct TInvenRec
{
	TGameObj *object;
	__int32 count;
};
#pragma pack(pop)

/* 15 */
#pragma pack(push, 1)
struct TGameObj
{
	__int32 ID;
	__int32 tile;
	char gap_8[2];
	char field_A;
	char gap_B[17];
	__int32 rotation;
	__int32 artFID;
	char gap_24[4];
	__int32 elevation;
	__int32 invenCount;
	__int32 field_30;
	TInvenRec *invenTablePtr;
	char gap_38[4];
	__int32 itemCharges;
	__int32 critterAP_weaponAmmoPid;
	char gap_44[16];
	__int32 lastTarget;
	char gap_58[12];
	__int32 pid;
	char gap_68[16];
	__int32 scriptID;
	char gap_7C[4];
	__int32 script_index;
	char gap_84[7];
	char field_0;
};
#pragma pack(pop)

/*    9 */
#pragma pack(push, 1)
struct TComputeAttack
{
	TGameObj *attacker;
	char gap_4[4];
	TGameObj *weapon;
	char gap_C[4];
	__int32 damageAttacker;
	__int32 flagsAttacker;
	__int32 rounds;
	char gap_1C[4];
	TGameObj *target;
	__int32 targetTile;
	__int32 bodyPart;
	__int32 damageTarget;
	__int32 flagsTarget;
	__int32 knockbackValue;
};
#pragma pack(pop)


/*   22 */
#pragma pack(push, 1)
struct TScript
{
	__int32 script_id;
	char gap_4[4];
	__int32 elevation_and_tile;
	__int32 spatial_radius;
	char gap_10[4];
	__int32 script_index;
	__int32 program_ptr;
	__int32 self_obj_id;
	char gap_20[8];
	__int32 scr_return;
	char gap_2C[4];
	__int32 fixed_param;
	TGameObj *self_obj;
	TGameObj *source_obj;
	TGameObj *target_obj;
	__int32 script_overrides;
	char field_44;
	char gap_45[15];
	__int32 procedure_table[28];
};
#pragma pack(pop)


/*   25 */
#pragma pack(push, 1)
struct TProgram
{
	const char* fileName;
	__int32 *codeStackPtr;
	char gap_8[8];
	__int32 *codePtr;
	__int32 field_14;
	char gap_18[4];
	__int32 *dStackPtr;
	__int32 *aStackPtr;
	__int32 *dStackOffs;
	__int32 *aStackOffs;
	char gap_2C[4];
	__int32 *stringRefPtr;
	char gap_34[4];
	__int32 *procTablePtr;
};
#pragma pack(pop)

#pragma pack(push, 1)
struct sMessage
{
  __int32 number;
  __int32 flags;
  char* audio;
  char* message;
};
#pragma pack(pop)

struct sArt {
	__int32 flags;
	char path[16];
	char* names;
	__int32 d18;
	__int32 total;
};

struct CritStruct {
	union {
		struct {
			__int32 DamageMultiplier;
			__int32 EffectFlags;
			__int32 StatCheck;
			__int32 StatMod;
			__int32 FailureEffect;
			__int32 Message;
			__int32 FailMessage;
		};
		__int32 values[7];
	};
};

#pragma pack(push, 1)
struct SkillInfo
{
  __int32 name;
  __int32 desc;
  __int32 attr;
  __int32 image;
  __int32 base;
  __int32 statMulti;
  __int32 statA;
  __int32 statB;
  __int32 skillPointMulti;
  __int32 Exp;
  __int32 f;
};
#pragma pack(pop)

//fallout2 path node structure
struct sPath {
	char* path;
	void* pDat;
	__int32 isDat;
	sPath* next;
};

struct sProtoBase {
	__int32 pid;
	__int32 message_num;
	__int32 fid;
};

struct sProtoTile {
	sProtoBase base;	
	__int32 flags;
	__int32 flags_ext;
	__int32 material;
	__int32 field_18;
};

struct sProtoObj {
	sProtoBase base;
	__int32 light_distance;
	__int32 light_intensity;
	__int32 flags;
};

struct sProtoItem {
	sProtoObj obj;	
	__int32 flags_ext;
	__int32 sid;
	ItemType type;
};

struct sProtoWeapon
{
	sProtoItem item;
	__int32 animation_code;
	__int32 min_damage;
	__int32 max_damage;
	__int32 dt;
	__int32 max_range1;
	__int32 max_range2;
	__int32 proj_pid;
	__int32 min_st;
	__int32 mp_cost1;
	__int32 mp_cost2;
	__int32 crit_fail_table;
	__int32 perk;
	__int32 rounds;
	__int32 caliber;
	__int32 ammo_type_pid;
	__int32 max_ammo;
	__int32 sound_id;
	__int32 field_68;
	__int32 material;
	__int32 size;
	__int32 weight;
	__int32 cost;
	__int32 inv_fid;
	__int8 SndID;
};