/*
 *    sfall
 *    Copyright (C) 2008 - 2021 Timeslip and sfall team
 *
 */

#include "..\FalloutEngine\Fallout2.h"
#include "..\main.h"
#include "..\Utils.h"
#include "..\Modules\Graphics.h"

#include "Image.h"

#include "SplashScreen.h"

namespace HRP
{

// 0 - image will display at its original size
// 1 - image will stretch to fit the screen while maintaining its aspect ratio
// 2 - image will stretch to fill the screen
long SplashScreen::SPLASH_SCRN_SIZE;

static unsigned short rixWidth;
static unsigned short rixHeight;
static BYTE* rixBuffer;

static void __cdecl game_splash_screen_hack_scr_blit(BYTE* srcPixels, long srcWidth, long srcHeight, long srcX, long srcY, long width, long height, long x, long y) {
	RECT rect;
	long w = Setting::ScreenWidth();
	long h = Setting::ScreenHeight();

	// TODO: загрузка альтернативного 32-битного изображения формата BMP или текcтуры DirectX
	// растянуть текстурой для DirectX

	if (rixBuffer) {
		srcWidth = rixWidth;
		srcHeight = rixHeight;
		srcPixels = rixBuffer;
	}

	if (SplashScreen::SPLASH_SCRN_SIZE || srcWidth > w || srcHeight > h) {
		if (SplashScreen::SPLASH_SCRN_SIZE == 1) {
			x = 0;
			Image::GetAspectSize(srcWidth, srcHeight, &x, &y, w, h);

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
	} else {
		// original size to center screen

		rect.left = ((Setting::ScreenWidth() - srcWidth) / 2) + x;
		rect.right = (rect.left + srcWidth) - 1;

		rect.top = ((Setting::ScreenHeight() - srcHeight) / 2) + y;
		rect.bottom = (rect.top + srcHeight) - 1;

		sfall::Graphics::UpdateDDSurface(srcPixels, srcWidth, srcHeight, srcWidth, &rect);
	}
	if (rixBuffer) {
		delete[] rixBuffer;
		rixBuffer = nullptr;
	}
}

// Fixes colored border of screen when the 0-index of the palette contains the color of a non-black (zero) value
static void Clear(fo::PALETTE* palette) {
	long index = Image::GetDarkColor(palette);
	if (index != 0) sfall::Graphics::BackgroundClearColor(index);
}

static fo::DbFile* __fastcall ReadRIX(fo::DbFile* file, fo::PALETTE* palette) {
	fo::func::db_fseek(file, 4, SEEK_SET);
	fo::func::db_freadShort(file, &rixWidth);
	fo::func::db_freadShort(file, &rixHeight);

	rixWidth = sfall::ByteSwapW(rixWidth);
	rixHeight = sfall::ByteSwapW(rixHeight);

	if (rixWidth != 640 || rixHeight != 480) {
		size_t size = rixWidth * rixHeight;
		rixBuffer = new BYTE[size];

		fo::func::db_fseek(file, 4 + 768, SEEK_CUR);
		fo::func::db_fread(rixBuffer, 1, size, file);
	}
	Clear(palette);

	return file;
}

static void __declspec(naked) game_splash_screen_hook() {
	__asm {
		mov  ecx, eax; // file
		mov  edx, ebp; // .rix palette
		call ReadRIX;
		jmp  fo::funcoffs::db_fclose_;
	}
}

void SplashScreen::init() {
	sfall::HookCall(0x4444FC, game_splash_screen_hook);
	sfall::MakeCall(0x44451E, game_splash_screen_hack_scr_blit, 1);
}

}