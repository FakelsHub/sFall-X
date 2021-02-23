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

void __declspec() op_active_hand();

void __declspec() op_toggle_active_hand();

void __declspec() op_set_inven_ap_cost();

void mf_get_inven_ap_cost(OpcodeContext&);

void op_obj_is_carrying_obj(OpcodeContext&);

void mf_critter_inven_obj2(OpcodeContext&);

void mf_item_weight(OpcodeContext&);

void mf_get_current_inven_size(OpcodeContext&);

void mf_unwield_slot(OpcodeContext&);

}
}
