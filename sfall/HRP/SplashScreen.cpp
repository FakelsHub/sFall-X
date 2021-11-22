/*
 *    sfall
 *    Copyright (C) 2008 - 2021 Timeslip and sfall team
 *
 */

#include "..\FalloutEngine\Fallout2.h"
#include "..\main.h"
#include "..\Modules\Graphics.h"

#include "Image.h"

#include "SplashScreen.h"

namespace HRP
{

// 0 - image will display at its original size
// 1 - image will stretch to fit the screen while maintaining its aspect ratio
// 2 - image will stretch to fill the screen
long SplashScreen::SPLASH_SCRN_SIZE;

static void __cdecl game_splash_screen_hack_scr_blit(BYTE* srcPixels, long srcWidth, long srcHeight, long srcX, long srcY, long width, long height, long x, long y) {
	RECT rect;
	long w = Setting::ScreenWidth();
	long h = Setting::ScreenHeight();

	// TODO: загрузка альтернативного 32-битного изображения формата BMP или текcтуры DirectX
	// растянуть текстурой для DirectX

	if (SplashScreen::SPLASH_SCRN_SIZE || srcWidth > w || srcHeight > h) {
		if (SplashScreen::SPLASH_SCRN_SIZE == 1) {
			x = Image::GetAspectSize(w, h, (float)srcWidth, (float)srcHeight);
			if (x >= w) { // extract x/y image position
				y = x / w;
				x -= y * w;
			}

			rect.top = y;
			rect.bottom = (y + h) - 1;
			rect.left = x;
			rect.right = (rect.left + w) - 1;
		} else {
			rect.top = 0;
			rect.left = 0;
			rect.right = w - 1;
			rect.bottom = h - 1;
		}
		BYTE* resizeBuff = new BYTE[w * h];
		Image::Scale(srcPixels, srcWidth, srcHeight, resizeBuff, w, h);

		sfall::Graphics::UpdateDDSurface(resizeBuff, w, h, w, &rect);

		delete[] resizeBuff;
		return;
	}
	// original size to center screen

	rect.left = ((Setting::ScreenWidth() - srcWidth) / 2) + x;
	rect.right = (rect.left + srcWidth) - 1 ;

	rect.top = ((Setting::ScreenHeight() - srcHeight) / 2) + y;
	rect.bottom = (rect.top + srcHeight) - 1;

	sfall::Graphics::UpdateDDSurface(srcPixels, srcWidth, srcHeight, srcWidth, &rect);
}

// Fixes colored border of screen when the 0-index of the palette contains the color of a non-black (zero) value
static void __fastcall Clear(fo::PALETTE* palette) {
	long index = Image::GetDarkColor(palette);
	if (index != 0) sfall::Graphics::BackgroundClearColor(index);
}

static void __declspec(naked) game_splash_screen_hook() {
	__asm {
		call fo::funcoffs::db_fclose_;
		mov  ecx, ebp; // .rix palette
		jmp  Clear;
	}
}

void SplashScreen::init() {
	sfall::HookCall(0x4444FC, game_splash_screen_hook);
	sfall::MakeCall(0x44451E, game_splash_screen_hack_scr_blit, 1);
}

}