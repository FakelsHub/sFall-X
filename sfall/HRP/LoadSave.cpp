/*
 *    sfall
 *    Copyright (C) 2008 - 2021 Timeslip and sfall team
 *
 */

#include "..\FalloutEngine\Fallout2.h"
#include "..\main.h"
#include "..\Modules\LoadGameHook.h"

#include "LoadSave.h"

namespace HRP
{

static long xPosition;
static long yPosition;

static long __fastcall LoadSaveWinAdd(long height, long yPos, long xPos, long width, long color, long flags) {
	if (sfall::IsGameLoaded()) yPos -= 50;

	yPos += (Setting::ScreenHeight() - height) / 2;
	xPos += (Setting::ScreenWidth() - width) / 2;

	xPosition = xPos;
	yPosition = yPos;

	return fo::func::win_add(xPos, yPos, width, height, color, flags);
}

static void __declspec(naked) LSGameStart_hook_win_add() {
	__asm {
		xchg ebx, [esp]; // width
		push eax;        // xPos
		push ebx;        // ret addr
		jmp  LoadSaveWinAdd;
	}
}

static void __declspec(naked) GetComment_hook_win_add() {
	__asm {
		add  edx, yPosition;
		add  eax, xPosition;
		jmp  fo::funcoffs::win_add_;
	}
}

static void __declspec(naked) LoadSaveMouseGetPositionHook() {
	__asm {
		push eax;
		push edx;
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

static void __declspec(naked) QuickSnapShotHook() {
	__asm {
		mov  ebx, ds:[FO_VAR_buf_length_2];
		mov  ecx, ds:[FO_VAR_buf_width_2];
		mov  edx, ecx;
		jmp  fo::funcoffs::cscale_;
	}
}

void LoadSave::init() {
	sfall::HookCall(0x47D529, LSGameStart_hook_win_add);
	sfall::HookCall(0x47ED8C, GetComment_hook_win_add);

	sfall::HookCalls(LoadSaveMouseGetPositionHook, {
		0x47CC0F, // LoadGame_
		0x47BE3D  // SaveGame_
	});

	sfall::HookCalls(QuickSnapShotHook, {
		0x47C627, // QuickSnapShot_
		0x47D42F  // LSGameStart_
	});
}

}