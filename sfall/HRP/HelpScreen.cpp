/*
 *    sfall
 *    Copyright (C) 2008 - 2021 Timeslip and sfall team
 *
 */

#include "..\FalloutEngine\Fallout2.h"
#include "..\main.h"

#include "Image.h"

#include "HelpScreen.h"

namespace HRP
{

// 0 - background image will display at its original size
// 1 - background image will stretch to fit the screen while maintaining its aspect ratio
// 2 - background image will stretch to fill the screen
long HelpScreen::HELP_SCRN_SIZE;

static long __fastcall game_help_hook_win_add(long height, long y, long color, long flags) {
	__asm mov  eax, 0x502220; // art\intrface\helpscrn.pal
	__asm call fo::funcoffs::loadColorTable_;

	color = Image::GetDarkColor((fo::PALETTE*)FO_VAR_cmap);
	return fo::func::win_add(0, 0, Setting::ScreenWidth(), Setting::ScreenHeight(), color, flags);
}

static void __cdecl game_help_hook_buf_to_buf(fo::FrmData* frm, long w, long h, long srcW, BYTE* dst, long dstW) {
	long width = frm->frame.width;
	long height = frm->frame.height;

	w = Setting::ScreenWidth();
	h = Setting::ScreenHeight();

	if (HelpScreen::HELP_SCRN_SIZE || width > w || height > h) {
		if (HelpScreen::HELP_SCRN_SIZE == 1) {
			long x = Image::GetAspectSize(w, h, (float)width, (float)height);
			long y = 0;
			if (x >= w) { // extract x/y image position
				y = x / w;
				x -= y * w;
			}
			if (x || y) dst += x + (y * Setting::ScreenWidth());
		}
		Image::Scale(frm->frame.data, width, height, dst, w, h, Setting::ScreenWidth());
		return;
	}

	long y = (h - height) / 2;
	long x = (w - width) / 2;
	if (x || y) dst += x + (y * Setting::ScreenWidth());

	fo::func::buf_to_buf(frm->frame.data, width, height, width, dst, Setting::ScreenWidth());
}

void HelpScreen::init() {
	sfall::HookCall(0x443FB2, game_help_hook_win_add);
	sfall::HookCall(0x44401D, game_help_hook_buf_to_buf);

	// game_help_
	sfall::HookCall(0x443FEE, (void*)fo::funcoffs::art_ptr_lock_); // replace art_ptr_lock_data_ to art_ptr_lock_
	sfall::SafeWrite16(0x443FEC, 0xCA89); // mov edx, ecx

	// game_help_
	sfall::BlockCall(0x444039); // block loadColorTable_
}

}