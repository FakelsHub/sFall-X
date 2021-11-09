/*
 *    sfall
 *    Copyright (C) 2008 - 2021 Timeslip and sfall team
 *
 */

#include "..\FalloutEngine\Fallout2.h"
#include "..\main.h"

#include "init.h"
#include "viewmap\ViewMap.h"

#include "Dialog.h"

namespace sfall
{

static const long width = 640; // art
static long scr_width = 639;

bool Dialog::DIALOG_SCRN_ART_FIX = true;
bool Dialog::DIALOG_SCRN_BACKGROUND = false;

static long xPosition;
static long yPosition;

static long __fastcall CreateWinDialog(long height, long yPos, long xPos, long color, long flags) {
	if (Dialog::DIALOG_SCRN_BACKGROUND) {
		fo::func::win_hide(fo::var::getInt(FO_VAR_display_win));

		// hide panels

		yPos += 50;
	}

	long mapWinW = fo::var::getInt(FO_VAR_buf_width_2) / 2;
	long mapWinH = fo::var::getInt(FO_VAR_buf_length_2) / 2;

	yPos += mapWinH - (height / 2); // yPos:0 = 480-art_frame_length
	xPos += mapWinW - (width / 2);  // xPos:0
	xPosition = xPos;
	yPosition = yPos;

	return fo::func::win_add(xPos, yPos, width, height, color, flags);
}

static void __declspec(naked) gdCreateHeadWindow_hook_win_add() {
	__asm {
		pop	 ebx; // ret addr
		push eax; // xPos
		push ebx;
		jmp  CreateWinDialog;
	}
}

static void ShowMapWindow() {
	fo::func::win_show(fo::var::getInt(FO_VAR_display_win));
	fo::func::win_draw(fo::var::getInt(FO_VAR_display_win));
	// show panels
}

static void __declspec(naked) gdDestroyHeadWindow_hook_win_delete() {
	__asm {
		call fo::funcoffs::win_delete_;
		jmp  ShowMapWindow;
	}
}

static void __declspec(naked) GeneralDialogWinAdd() {
	__asm {
		add  eax, xPosition;
		add  edx, yPosition;
		mov  ebx, width;
		jmp  fo::funcoffs::win_add_;
	}
}

static void __declspec(naked) gdProcess_hook_win_add() {
	__asm {
		add  edx, yPosition;
		add  eax, xPosition;
		jmp  fo::funcoffs::win_add_;
	}
}

static void __declspec(naked) setup_inventory_hook_win_add() {
	__asm {
		add  eax, xPosition;
		add  edx, yPosition;
		jmp  fo::funcoffs::win_add_;
	}
}

static void __declspec(naked) setup_inventory_hack() {
	__asm {
		mov  dword ptr ds:[FO_VAR_i_wid], eax;
		add  ebx, xPosition;
		add  ecx, yPosition;
		retn;
	}
}

static void __declspec(naked) barter_move_hook_mouse_click_in() {
	__asm {
		add  eax, xPosition; // left
		add  ebx, xPosition; // right
		add  edx, yPosition; // top
		add  ecx, yPosition; // bottom
		jmp  fo::funcoffs::mouse_click_in_;
	}
}

static void __declspec(naked) hook_buf_to_buf() {
	__asm {
		imul eax, yPosition, 640;
		add  [esp + 4], eax;
		jmp  fo::funcoffs::buf_to_buf_;
	}
}

// Implementation from HRP by Mash
static void __cdecl gdDisplayFrame_hook_buf_to_buf(BYTE* src, long w, long h, long srcWidth, BYTE* dst, long dstWidth) {
	long cx, cy;
	ViewMap::GetTileCoord(fo::var::getInt(FO_VAR_tile_center_tile), cx, cy);

	fo::GameObject* dialog_target = fo::var::dialog_target;

	long x, y;
	ViewMap::GetTileCoord(dialog_target->tile, x, y);

	long xDist = 16 * (cx - x);
	long yDist = 12 * (cy - y);

	DWORD lockPtr;
	auto frm = fo::func::art_ptr_lock(dialog_target->artFid, &lockPtr);
	long yOffset = (fo::func::art_frame_length(frm, dialog_target->frm, dialog_target->rotation) / 2);
	fo::func::art_ptr_unlock(lockPtr);

	long mapWinH = fo::var::getInt(FO_VAR_buf_length_2) - h;
	y = (mapWinH / 2) + (yDist - yOffset);
	if (y < 0) {
		y = 0;
	} else if (y > mapWinH) {
		y = mapWinH;
	}

	long mapWinW = srcWidth - w; // fo::var::getInt(FO_VAR_buf_width_2);
	x = (mapWinW / 2) + xDist;
	if (x < 0) {
		x = 0;
	} else if (x > (mapWinW)) {
		x = mapWinW;
	}
	return fo::func::buf_to_buf((BYTE*)fo::var::getInt(FO_VAR_display_buf) + x + (y * srcWidth), w, h, srcWidth, dst, width);
}

void Dialog::init() {
	// replace width size for buffers functons
	SafeWriteBatch(&scr_width, {
		0x447201, 0x447248, //0x44716F,         // gdCreateHeadWindow_
		0x4459F0, 0x445A0C, //0x44597A,         // gdReviewInit_
		0x445DA7, 0x445DD1,                     // gdReviewDisplay_
		0x447E43, 0x447E75, 0x447EE2, 0x447EFE, 0x447DB5, // gdialog_scroll_subwin_
		0x44A705, //0x44A6C3,                   // gdialog_window_create_
		0x44AA28,                               // gdialog_window_destroy_
		0x448354, //0x448312,                   // gdialog_barter_create_win_
		0x4485AC,                               // gdialog_barter_destroy_win_
		0x4487FF, //0x4487BD,                   // gdControlCreateWin_
		0x448CA0,                               // gdControlDestroyWin_
		0x4497A0, //0x44975E,                   // gdCustomCreateWin_
		0x449AA0,                               // gdCustomDestroyWin_
		0x449C2C,                               // gdCustomUpdateInfo_
		0x44AB4C, 0x44AB6A,                     // talk_to_refresh_background_window_
		0x44AC23,                               // talkToRefreshDialogWindowRect_
		0x44AE4D, 0x44AF94, 0x44AFD5, 0x44B015, 0x44AD7B, 0x44AE88, // gdDisplayFrame_
		//0x44AADE,                             // talk_to_create_background_window_ (unused)
		// for barter interface
		0x46EE04,                               // setup_inventory_
		0x4753C1, 0x475400, 0x47559F, 0x4755F5, // display_table_inventories_
		0x47005A, 0x47008F,                     // display_inventory_
		0x470401, 0x47043E,                     // display_target_inventory_
		0x47080E,                               // display_body_

		0x474E35, 0x474E76,                     // barter_move_inventory_
		0x4750FE, 0x475139,                     // barter_move_from_table_inventory_
		0x4733DF, 0x47343A, // inven_action_cursor_
	});

	HookCall(0x44718F, gdCreateHeadWindow_hook_win_add);
	HookCalls(GeneralDialogWinAdd, {
		0x445997, // gdReviewInit_
		0x44A6E4, // gdialog_window_create_
		0x448333, // gdialog_barter_create_win_
		0x4487CE, // gdControlCreateWin_
		0x44976F, // gdCustomCreateWin_
	});

	// gdCustomSelect_
	long yoffset = (DIALOG_SCRN_BACKGROUND) ? 100 : 200;
	SafeWrite32(0x44A03E, HRP::ScreenHeight() - yoffset);
	SafeWrite32(0x44A02A, HRP::ScreenWidth());

	HookCall(0x4462A7, gdProcess_hook_win_add);
	HookCall(0x446387, gdProcess_hook_win_add);

	HookCall(0x447900, hook_buf_to_buf); // demo_copy_options_
	HookCall(0x447A46, hook_buf_to_buf); // gDialogRefreshOptionsRect_

	HookCall(0x44AF39, gdDisplayFrame_hook_buf_to_buf);

	// Barter interface hacks
	HookCall(0x46EDC9, setup_inventory_hook_win_add);
	MakeCall(0x46EDD8, setup_inventory_hack);
	HookCalls(barter_move_hook_mouse_click_in, {
		0x474F76, 0x474FF9, // barter_move_inventory_
		0x475241, 0x4752C2, // barter_move_from_table_inventory_
		0x449661            // gdControl_ (custom disposition button)
	});

	if (DIALOG_SCRN_BACKGROUND) {
		HookCall(0x4472D8, gdDestroyHeadWindow_hook_win_delete);
	}

	//Dialog::DIALOG_SCRN_ART_FIX
}

}