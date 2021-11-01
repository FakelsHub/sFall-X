﻿/*
 *    sfall
 *    Copyright (C) 2008 - 2021 Timeslip and sfall team
 *
 */

#include "FalloutEngine\Fallout2.h"
#include "main.h"

#include "WinProc.h"

namespace sfall
{

static long reqGameQuit;
static bool cCursorShow = true;

static int __stdcall WindowProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	RECT rect;
	//POINT point;

	switch (msg) {
	case WM_DESTROY:
		__asm xor  eax, eax;
		__asm call fo::funcoffs::exit_;
	case WM_ERASEBKGND:
		return 1;

	case WM_PAINT:
		if (GetUpdateRect(hWnd, &rect, 0) == 1) {
			rect.right -= 1;
			rect.bottom -= 1;
			__asm {
				//lea  eax, rect;
				//call fo::funcoffs::win_refresh_all_;
			}
		}
		break;

	case WM_ACTIVATE:
		if (!cCursorShow && wParam == WA_INACTIVE) {
			cCursorShow = true;
			ShowCursor(1);
		}
		break;

	case WM_SETCURSOR:
	{
		short type = LOWORD(lParam);
		/*if (type == HTCAPTION || type == HTBORDER || type == HTMINBUTTON || type == HTCLOSE || type == HTMAXBUTTON) {
			if (!cCursorShow) {
				cCursorShow = true;
				ShowCursor(1);
			}
		}
		else*/ if (type == HTCLIENT && fo::var::getInt(FO_VAR_GNW95_isActive)) {
			if (cCursorShow) {
				cCursorShow = false;
				ShowCursor(0);
			}
		}
		return 1;
		//break;
	}
	case WM_SYSCOMMAND:
		if ((wParam & 0xFFF0) == SC_SCREENSAVE || (wParam & 0xFFF0) == SC_MONITORPOWER) return 0;
		break;

	case WM_ACTIVATEAPP:
		fo::var::setInt(FO_VAR_GNW95_isActive) = wParam;
		if (wParam) { // active
			/*point.x = 0;
			point.y = 0;
			ClientToScreen(hWnd, &point);
			GetClientRect(hWnd, &rect);
			rect.left += point.x;
			rect.right += point.x;
			rect.top += point.y;
			rect.bottom += point.y;
			ClipCursor(&rect);*/
			__asm {
				mov  eax, 1;
				call fo::funcoffs::GNW95_hook_input_;
				//mov  eax, FO_VAR_scr_size;
				//call fo::funcoffs::win_refresh_all_;
			}
		} else{
			// ClipCursor(0);
			__asm xor  eax, eax;
			__asm call fo::funcoffs::GNW95_hook_input_;
		}
		return 0;

	case WM_CLOSE:
		__asm {
			call fo::funcoffs::main_menu_is_shown_;
			test eax, eax;
			jnz  skip;
			call fo::funcoffs::game_quit_with_confirm_;
		skip:
			mov  reqGameQuit, eax;
		}
		return 0;
	}
	return DefWindowProcA(hWnd, msg, wParam, lParam);
}

static long __stdcall main_menu_loop_hook() {
	return (!reqGameQuit) ? fo::func::get_input() : 27; // ESC code
}

void WinProc::SetWindowProc() {
	SetWindowLongA((HWND)fo::var::getInt(FO_VAR_GNW95_hwnd), GWL_WNDPROC, (LONG)WindowProc);
}

void WinProc::init() {
	MakeJump(0x4DE9FC, WindowProc); // WindowProc_
	HookCall(0x481B2A, main_menu_loop_hook);

	//SafeWrite8(0x4DEB0D, 1); // for test
}

}