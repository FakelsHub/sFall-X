﻿/*
 *    sfall
 *    Copyright (C) 2008 - 2021 Timeslip and sfall team
 *
 */

#include <ddraw.h>

#include "..\FalloutEngine\Fallout2.h"
#include "..\main.h"

#include "Image.h"

#include "MoviesScreen.h"

namespace HRP
{

long MoviesScreen::MOVIE_SIZE;

static RECT movieToSize;

static void __fastcall SetMovieSize() {
	long sWidth = Setting::ScreenWidth();
	long sHeight = Setting::ScreenHeight();

	long bH = fo::var::getInt(FO_VAR_mveBH);
	long bW = fo::var::getInt(FO_VAR_mveBW);

	long subtitleHeight = (fo::var::getInt(FO_VAR_subtitles) && fo::var::getInt(FO_VAR_subtitleList)) ? fo::var::getInt(FO_VAR_subtitleH) + 4 : 0;

	if (MoviesScreen::MOVIE_SIZE == 1 || bW > sWidth || bH > sHeight) {
		long aspectW = sWidth;
		long aspectH = sHeight - subtitleHeight;
		long x = 0;
		long y = 0;
		Image::GetAspectSize(bW, bH, &x, &y, aspectW, aspectH);

		movieToSize.left = x;
		movieToSize.top = y;
		movieToSize.right = x + aspectW;
		movieToSize.bottom = y + aspectH;
	}
	else if (MoviesScreen::MOVIE_SIZE == 2) {
		movieToSize.top = 0;
		movieToSize.bottom = Setting::ScreenHeight() - subtitleHeight;

		movieToSize.left = 0;
		movieToSize.right = Setting::ScreenWidth();
	} else {
		// set to center
		movieToSize.top = (Setting::ScreenHeight() - bH) / 2;
		movieToSize.bottom = bH + movieToSize.top;

		movieToSize.left = (Setting::ScreenWidth() - bW) / 2;
		movieToSize.right = bW + movieToSize.left;
	}
}

static void __declspec(naked) nfConfig_hack() {
	__asm {
		call SetMovieSize;
		xor  eax, eax;
		mov  ecx, 27;
		retn;
	}
}

// surface: _nf_mve_buf1
static long __cdecl movie_MVE_ShowFrame(IDirectDrawSurface* surface, int bW, int bH, int x, int y, int w, int h) {
	RECT movieSize;
	movieSize.left = x;
	movieSize.right = x + bW;
	movieSize.top = y;
	movieSize.bottom = y + bH;

	fo::var::setInt(FO_VAR_lastMovieX) = movieToSize.left;
	fo::var::setInt(FO_VAR_lastMovieY) = movieToSize.top;
	fo::var::setInt(FO_VAR_lastMovieW) = movieToSize.right - movieToSize.left;
	fo::var::setInt(FO_VAR_lastMovieH) = movieToSize.bottom - movieToSize.top;
	//FO_VAR_lastMovieBW = bW
	//FO_VAR_lastMovieBH = bH

	return surface->Blt(&movieToSize, surface, &movieSize, MoviesScreen::MOVIE_SIZE, 0); // for sfall DX9
}

// Adjust Y position
static long __fastcall SubtitleAdjustPosition(long уTop, long yBottom) {
	if (yBottom > Setting::ScreenHeight()) return уTop - (yBottom - Setting::ScreenHeight());

	if (MoviesScreen::MOVIE_SIZE == 0 || Setting::ScreenHeight() == 480) return уTop;

	long y = fo::var::getInt(FO_VAR_lastMovieY);
	y += fo::var::getInt(FO_VAR_lastMovieH) + 4;

	if (уTop >= y) return уTop;

	long spaceHeight = Setting::ScreenHeight() - y;
	if (spaceHeight > fo::var::getInt(FO_VAR_subtitleH)) {
		spaceHeight -= fo::var::getInt(FO_VAR_subtitleH);
		y += spaceHeight / 2; // центрируем
	}

	long yMax = Setting::ScreenHeight() - fo::var::getInt(FO_VAR_subtitleH);
	return (y < yMax) ? y : yMax;
}

static void __declspec(naked) doSubtitle_hook() {
	__asm {
		mov  ecx, esi;
		call SubtitleAdjustPosition;
		mov  esi, eax;
		retn;
	}
}

static void __declspec(naked) gmovie_play_hack_begin() {
	__asm {
		cmp  ds:[FO_VAR_wmInterfaceWasInitialized], 1;
		jne  skip;
		mov  eax, fo::funcoffs::wmMouseBkProc_;
		call fo::funcoffs::remove_bk_process_;
skip:
		mov  edx, 1;
		retn;
	}
}

static void __declspec(naked) gmovie_play_hack_end() {
	__asm {
		cmp  ds:[FO_VAR_wmInterfaceWasInitialized], 1;
		jne  skip;
		mov  edx, eax;
		mov  eax, fo::funcoffs::wmMouseBkProc_;
		call fo::funcoffs::add_bk_process_;
		mov  eax, edx;
skip:
		pop  edx; // ret addr
		pop  ebp;
		pop  edi;
		pop  esi;
		pop  ecx;
		pop  ebx;
		jmp  edx;
	}
}

void MoviesScreen::init() {
	namespace sf = sfall;

	if (MOVIE_SIZE < 0) MOVIE_SIZE = 0;
	else
	if (MOVIE_SIZE > 2) MOVIE_SIZE = 2;

	// gmovie_play_ set GNWWin window size
	sf::SafeWrite32(0x44E7D4, Setting::ScreenHeight());
	sf::SafeWrite32(0x44E7D9, Setting::ScreenWidth());
	sf::SafeWrite8(0x44E7D2, fo::WinFlags::Exclusive | fo::WinFlags::MoveOnTop); // fix
	//sf::SafeWrite8(0x44E7DE, 55); // for debug

	// movieStart_
	// Для DirectX9 используется прямой вывод в тектуру, для DirectDraw используется буферризированный метод (вывод в GNWWin окно)
	if (0) {
		sf::SafeWrite8(0x487781, sf::CodeType::JumpShort); // force Buffered
	} else {
		sf::SafeWrite16(0x487781, 0x9090); // force Direct
	}

	sf::SafeWrite32(0x4877D0, (DWORD)&movie_MVE_ShowFrame); // replace movie_MVE_ShowFrame_ to sfall function

	sf::MakeCall(0x4F5D40, nfConfig_hack);

	// openSubtitle_
	sf::HookCall(0x48738E, Setting::ScreenWidth); // replace windowGetXres_

	// doSubtitle_ hacks
	sf::SafeWrite32(0x487580, Setting::ScreenHeight());
	sf::HookCall(0x4875BE, doSubtitle_hook);
	sf::SafeWrite8(0x4875C5, sf::CodeType::JumpShort); // jle > jmp

	// preventing processing scrolling global map during playback
	sf::MakeCall(0x44E6B2, gmovie_play_hack_begin);
	sf::MakeCalls(gmovie_play_hack_end, { 0x44EAC9, 0x44EADD });

	// Disable _MVE_lastBuffer copying function (ненужная опция для движка)
	//sf::SafeWrite8(0x486F2E, sf::CodeType::Jump); // jz > jmp (cleanupMovie_)
}

}