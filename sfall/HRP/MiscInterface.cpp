/*
 *    sfall
 *    Copyright (C) 2008 - 2021 Timeslip and sfall team
 *
 */

#include "..\FalloutEngine\Fallout2.h"
#include "..\main.h"
#include "..\Modules\LoadGameHook.h"

#include "MiscInterface.h"

namespace HRP
{

static long xPosition;
static long yPosition;

static long __fastcall CommonWinAdd(long height, long width, long color, long flags) {
	xPosition = (Setting::ScreenWidth() - width) / 2;
	yPosition = (Setting::ScreenHeight() - height) / 2;
	if (sfall::IsGameLoaded()) yPosition -= 50;

	return fo::func::win_add(xPosition, yPosition, width, height, color, flags);
}

static void __declspec(naked) CommonWinAddHook() {
	__asm {
		mov  edx, ebx; // width
		jmp  CommonWinAdd;
	}
}

static void __declspec(naked) MouseGetPositionHook() {
	__asm {
		push eax; // outX ref
		push edx; // outY ref
		call fo::funcoffs::mouse_get_position_;
		pop  edx;
		mov  eax, yPosition;
		sub  [edx], eax;
		pop  edx;
		mov  eax, xPosition;
		sub  [edx], eax;
		retn;
	}
}

void MiscInterface::init() {
	sfall::HookCalls(CommonWinAddHook, {
		0x497405, // StartPipboy_
		0x41B979, // automap_
		0x42626B, // get_called_shot_location_
		0x43F560, // elevator_start_
		0x490005, // OptnStart_
		0x490961, // PrefStart_
	});

	sfall::HookCalls(MouseGetPositionHook, {
		0x490EAC, 0x491546, // DoThing_
		0x497092, // pipboy_
		0x49A1B6, // ScreenSaver_
	});
	sfall::BlockCall(0x49A0FB); // ScreenSaver_

	// PauseWindow_
	sfall::SafeWrite32(0x49042A, Setting::ScreenHeight());
	sfall::SafeWrite32(0x490436, Setting::ScreenWidth());
	// ShadeScreen_
	sfall::SafeWrite32(0x49075F, Setting::ScreenWidth());
}

}