/*
 *    sfall
 *    Copyright (C) 2008 - 2021  The sfall team
 *
 */

#pragma once

#include "..\OpcodeContext.h"

namespace sfall
{
namespace script
{

// Kill counters
void SetExtraKillCounter(bool value);

void __declspec() op_get_kill_counter();

void __declspec() op_mod_kill_counter();

void op_set_object_knockback(OpcodeContext&);

void op_remove_object_knockback(OpcodeContext&);

void __declspec() op_get_bodypart_hit_modifier();

void __declspec() op_set_bodypart_hit_modifier();

void op_get_attack_type(OpcodeContext&);

void __declspec() op_force_aimed_shots();

void __declspec() op_disable_aimed_shots();

void __declspec() op_get_last_attacker();

void __declspec() op_get_last_target();

void __declspec() op_block_combat();

void mf_attack_is_aimed(OpcodeContext&);

void mf_combat_data(OpcodeContext&);

}
}
