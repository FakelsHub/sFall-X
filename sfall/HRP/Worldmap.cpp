/*
 *    sfall
 *    Copyright (C) 2008 - 2021 Timeslip and sfall team
 *
 */

#include "..\FalloutEngine\Fallout2.h"
#include "..\main.h"

#include "InterfaceBar.h"

#include "Worldmap.h"

namespace HRP
{

static long xPosition;
static long yPosition;

static long __fastcall WorldmapWinAdd(long height, long yPos, long xPos, long width, long color, long flags) {
	fo::func::win_hide(fo::var::getInt(FO_VAR_display_win));
	IFaceBar::Hide();

	yPos += (Setting::ScreenHeight() - height) / 2;
	xPos += (Setting::ScreenWidth() - width) / 2;

	xPosition = xPos;
	yPosition = yPos;

	return fo::func::win_add(xPos, yPos, width, height, color, flags);
}

static void __declspec(naked) wmInterfaceInit_hook_win_add() {
	__asm {
		xchg ebx, [esp]; // width
		push eax;        // xPos
		push ebx;        // ret addr
		jmp  WorldmapWinAdd;
	}
}

static void __declspec(naked) wmInterfaceInit_hook_win_delete() {
	__asm {
		call fo::funcoffs::win_delete_;
		mov  ebx, ecx;
		call IFaceBar::Show;
		mov  ecx, ebx;
		mov  eax, ds:[FO_VAR_display_win];
		jmp  fo::funcoffs::win_show_;
	}
}

static void __declspec(naked) WorldMapMouseGetPositionHook() {
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

static void __declspec(naked) wmWorldMap_hook_mouse_click_in() {
	__asm {
		add  eax, xPosition; // left
		add  ebx, xPosition; // right
		add  edx, yPosition; // top
		add  ecx, yPosition; // bottom
		jmp  fo::funcoffs::mouse_click_in_;
	}
}

void Worldmap::init() {
	sfall::HookCall(0x4C23A7, wmInterfaceInit_hook_win_add);
	sfall::HookCall(0x4C2E86, wmInterfaceInit_hook_win_delete);

	sfall::HookCalls(WorldMapMouseGetPositionHook, { 0x4BFE75, 0x4C3305 }); // wmWorldMap_, wmMouseBkProc_
	sfall::HookCalls(wmWorldMap_hook_mouse_click_in, { 0x4C0167, 0x4C02CD });

	// wmMouseBkProc_
	sfall::SafeWriteBatch<BYTE>(0x7F, { 0x4C3312, 0x4C332D }); // jnz > jg
	sfall::SafeWriteBatch<BYTE>(0x7C, { 0x4C3321, 0x4C333B }); // jnz > jl
}

}