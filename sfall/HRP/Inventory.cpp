/*
 *    sfall
 *    Copyright (C) 2008 - 2021 Timeslip and sfall team
 *
 */

#include "..\FalloutEngine\Fallout2.h"
#include "..\main.h"

#include "init.h"

#include "Inventory.h"

namespace sfall
{

static bool setPosition[3];
static long xPosition;
static long yPosition;

static long __fastcall CreateWin(long height, long yPos, long xPos, long width, long mode, long color, long flags) {
	if (!setPosition[mode]) {
		setPosition[mode] = true;
		long x = (HRP::ScreenWidth() - width) / 2;
		long y = (fo::var::getInt(FO_VAR_buf_length_2) - height) / 2;
		fo::var::iscr_data[mode].x = x;
		fo::var::iscr_data[mode].y = y;
	}
	// item move window
	fo::var::iscr_data[4].x = fo::var::iscr_data[mode].x + 60;
	fo::var::iscr_data[4].y = fo::var::iscr_data[mode].y + 80;

	xPos -= 80;
	xPos += fo::var::iscr_data[mode].x;
	yPos += fo::var::iscr_data[mode].y;
	xPosition = xPos - 80;
	yPosition = yPos;

	fo::var::setInt(FO_VAR_i_wid_max_x) = xPos + width;
	fo::var::setInt(FO_VAR_i_wid_max_y) = yPos + height;

	return fo::func::win_add(xPos, yPos, width, height, color, flags);
}

static void __declspec(naked) setup_inventory_hook_win_add() {
	__asm {
		pop	 ebp; // ret addr
		push edi; // mode
		push ebx; // width
		push eax; // xPos
		push ebp;
		jmp  CreateWin;
	}
}

static void __declspec(naked) inventory_hook_mouse_click_in() {
	__asm {
		add  eax, xPosition; // left
		add  ebx, xPosition; // right
		add  edx, yPosition; // top
		add  ecx, yPosition; // bottom
		jmp  fo::funcoffs::mouse_click_in_;
	}
}

void Inventory::init() {
	// set timer window position to center screen
	fo::var::iscr_data[5].x = (HRP::ScreenWidth() - fo::var::iscr_data[5].width) / 2;
	fo::var::iscr_data[5].y = (HRP::ScreenHeight() - fo::var::iscr_data[5].height) / 2;

	HookCall(0x46ED0E, setup_inventory_hook_win_add);
	HookCalls(inventory_hook_mouse_click_in, {
		0x471145, 0x471270, 0x471301, 0x47138B, // inven_pickup_
		0x474959, 0x474A23                      // move_inventory_
	});

	// setup_inventory_
	SafeMemSet(0x46ED23, CodeType::Nop, 6);
	SafeMemSet(0x46ED31, CodeType::Nop, 6);
}

}