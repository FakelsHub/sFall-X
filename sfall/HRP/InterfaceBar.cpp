﻿/*
 *    sfall
 *    Copyright (C) 2008 - 2021 Timeslip and sfall team
 *
 */

#include "..\FalloutEngine\Fallout2.h"
#include "..\main.h"
#include "..\Modules\LoadGameHook.h"

//#include "Image.h"
#include "Init.h"

#include "InterfaceBar.h"

namespace HRP
{

long IFaceBar::IFACE_BAR_MODE; // 1 - the bottom of the map view window extends to the base of the screen and is overlapped by the IFACE Bar
long IFaceBar::IFACE_BAR_SIDE_ART;
long IFaceBar::IFACE_BAR_WIDTH;
bool IFaceBar::IFACE_BAR_SIDES_ORI; // 1 - Iface-bar side graphics extend from the Screen edges to the Iface-Bar
long IFaceBar::ALTERNATE_AMMO_METRE;

static long xPosition;
static long yPosition;
static long xOffset;
static long xyOffsetCBtn;
static long xyOffsetAP;

bool IFaceBar::UseExpandAPBar = false;

long IFaceBar::display_width = 0; // width of the area for text output
char* IFaceBar::display_string_buf = (char*)FO_VAR_display_string_buf;

static class Panels {
	long leftBarID;
	long rightBarID;

	void LoadFRMImage(char* name, long winId) {
		auto* frm = fo::util::LoadUnlistedFrm(name, fo::ArtType::OBJ_TYPE_INTRFACE);
		if (!frm) return;

		fo::Window* win = fo::func::GNW_find(winId);

		long width = win->width;
		if (width > frm->frames->width) width = frm->frames->width;
		BYTE* scr = frm->frames->indexBuff;

		// set the position to the right side
		if (!IFaceBar::IFACE_BAR_SIDES_ORI && win->wRect.left <= 0) scr += (frm->frames->width - win->width);
		if (IFaceBar::IFACE_BAR_SIDES_ORI && win->wRect.left > xPosition) scr += (frm->frames->width - win->width);

		fo::func::cscale(scr, width, frm->frames->height, frm->frames->width, win->surface, win->width, win->height, win->width);

		delete frm;
	}

public:
	Panels(long idL, long idR) : leftBarID(idL), rightBarID(idR)
	{
		char file[33];
		std::sprintf(file, "HR_IFACELFT%d.frm", IFaceBar::IFACE_BAR_SIDE_ART);
		LoadFRMImage(file, leftBarID);

		std::sprintf(file, "HR_IFACERHT%d.frm", IFaceBar::IFACE_BAR_SIDE_ART);
		LoadFRMImage(file, rightBarID);
	}

	void Show() {
		fo::func::win_show(leftBarID);
		fo::func::win_show(rightBarID);
	}

	void Hide() {
		fo::func::win_hide(leftBarID);
		fo::func::win_hide(rightBarID);
	}

} *panels;

static long __fastcall IntfaceWinCreate(long height, long yPos, long xPos, long width, long color, long flags) {
	if (width != IFaceBar::IFACE_BAR_WIDTH) width = IFaceBar::IFACE_BAR_WIDTH;

	yPos += Setting::ScreenHeight() - 479; // yPos:379 = 479-100
	xPos += (Setting::ScreenWidth() - width) / 2;

	xPosition = xPos;
	yPosition = yPos;

	flags |= fo::WinFlags::DontMoveTop;

	if (IFaceBar::IFACE_BAR_MODE == 0 && IFaceBar::IFACE_BAR_SIDE_ART && Setting::ScreenWidth() > IFaceBar::IFACE_BAR_WIDTH) {
		long leftID = fo::func::win_add(0, yPos, xPos, height, 0, flags);
		long rightID = fo::func::win_add(xPos + width, yPos, Setting::ScreenWidth() - (xPos + width), height, 0, flags);

		panels = new Panels(leftID, rightID);
	}
	return fo::func::win_add(xPos, yPos, width, height, color, flags);
}

static void __declspec(naked) intface_init_hook_win_add() {
	__asm {
		pop	 ebp; // ret addr
		push ebx; // width
		push eax; // xPos
		push ebp;
		jmp  IntfaceWinCreate;
	}
}

static void __fastcall InterfaceShow(long winID) {
	fo::func::win_show(winID);
	if (panels) panels->Show();
}

static void __declspec(naked) intface_show_hook_win_show() {
	__asm {
		push ecx;
		push edx;
		mov  ecx, eax;
		call InterfaceShow;
		pop  edx;
		pop  ecx;
		retn;
	}
}

static void __fastcall InterfaceHide(long winID) {
	fo::func::win_hide(winID);
	if (panels) panels->Hide();
}

static void __declspec(naked) intface_win_hide() {
	__asm {
		push ecx;
		push edx;
		mov  ecx, eax;
		call InterfaceHide;
		pop  edx;
		pop  ecx;
		retn;
	}
}

static void __declspec(naked) refresh_box_bar_win_hook_win_add() {
	__asm {
		lea  edx, [edx - 358 - 21];
		add  eax, xPosition;
		add  edx, yPosition;
		jmp  fo::funcoffs::win_add_;
	}
}

static void __declspec(naked) skilldex_start_hook_win_add() {
	__asm {
		lea  eax, [eax - 640];
		add  eax, xPosition;
		lea  edx, [edx - 379];
		add  eax, IFaceBar::IFACE_BAR_WIDTH;
		add  edx, yPosition;
		jmp  fo::funcoffs::win_add_;
	}
}

// Scales the interface image to the width of IFACE_BAR_WIDTH
// exception: the first 30 pixels and the tail of 460 pixels wide are not scaled
static void InterfaceArtScale(BYTE* scr, long w, long h, BYTE* dst, long dh) {
	// copy the beginning with a width of 30 pixels
	fo::func::buf_to_buf(scr, 30, dh, w, dst, IFaceBar::IFACE_BAR_WIDTH);

	// copy the tail of 460 pixels wide
	long sxOffset = w - 460;                         // offset where to get the tail
	long dxOffset = IFaceBar::IFACE_BAR_WIDTH - 460; // offset where to insert the tail

	fo::func::buf_to_buf(scr + sxOffset, 460, dh, w, dst + dxOffset, IFaceBar::IFACE_BAR_WIDTH);

	// set source and destination rectangle widths
	long dw = dxOffset - 29;
	long sw = sxOffset - 30;

	// scale the rest of the display area rectangle
	fo::func::cscale(scr + 30, sw, h, w, dst + 30, dw, dh, IFaceBar::IFACE_BAR_WIDTH); // TODO: Заменить алгоритм масштабирования на более качественный
}

static long __cdecl InterfaceArt(BYTE* scr, long w, long h, long srcWidth, BYTE* dst, long dstWidth) {
	if (Setting::ScreenWidth() >= IFaceBar::IFACE_BAR_WIDTH) {
		xOffset = IFaceBar::IFACE_BAR_WIDTH - 640;
		xyOffsetAP = 15 * xOffset;
		xyOffsetCBtn = 39 * xOffset;

		fo::var::endWindowRect.x += xOffset;
		fo::var::endWindowRect.offx += xOffset;
		fo::var::movePointRect.x += xOffset;
		fo::var::movePointRect.offx += xOffset;
		fo::var::itemButtonRect.x += xOffset;
		fo::var::itemButtonRect.offx += xOffset;

		char file[33];
		std::sprintf(file, "HR_IFACE_%i%s.frm", IFaceBar::IFACE_BAR_WIDTH, ((IFaceBar::UseExpandAPBar) ? "E" : ""));

		auto* frm = fo::util::LoadUnlistedFrm(file, fo::ArtType::OBJ_TYPE_INTRFACE);
		if (frm && frm->frames->width == IFaceBar::IFACE_BAR_WIDTH) {
			h = frm->frames->height;
			if (h > 100) h = 100;

			fo::func::buf_to_buf(frm->frames->indexBuff, frm->frames->width, h, frm->frames->width, dst, IFaceBar::IFACE_BAR_WIDTH);

			delete frm;
			return 0;
		}

		// no required file, use the default one provided by HRP
		if (!frm) frm = fo::util::LoadUnlistedFrm(((IFaceBar::UseExpandAPBar) ? "HR_IFACE_800E.frm" : "HR_IFACE_800.frm"), fo::ArtType::OBJ_TYPE_INTRFACE);

		if (frm) {
			// scale the 800px wide interface to the width of IFACE_BAR_WIDTH
			InterfaceArtScale(frm->frames->indexBuff, frm->frames->width, frm->frames->height, dst, h);
			delete frm;
		} else {
			// scale the vanilla interface to 640px wide (640-460=180)
			InterfaceArtScale(scr, w, h, dst, h);
		}
		return 0;
	}
	return -1;
}

static void __declspec(naked) intface_init_hook_buf_to_buf_ART() {
	__asm {
		pop  ebx;
		call InterfaceArt;
		test eax, eax;
		jnz  default;
		jmp  ebx;
default:
		push ebx;
		jmp  fo::funcoffs::buf_to_buf_;
	}
}

static void __declspec(naked) intface_init_hook_buf_to_buf() {
	__asm {
		mov  eax, IFaceBar::IFACE_BAR_WIDTH;
		mov  [esp + 0xC + 4], eax; // from width
		mov  eax, xyOffsetAP;
		add  [esp + 4], eax;       // from += 15 * (IFaceBar::IFACE_BAR_WIDTH - 640)
		jmp  fo::funcoffs::buf_to_buf_;
	}
}

static void __declspec(naked) intface_update_move_points_hook_buf_to_buf() {
	__asm {
		mov  eax, IFaceBar::IFACE_BAR_WIDTH;
		mov  [esp + 0x14 + 4], eax; // to width
		mov  eax, xyOffsetAP;
		add  [esp + 0x10 + 4], eax; // to += 15 * (IFaceBar::IFACE_BAR_WIDTH - 640)
		jmp  fo::funcoffs::buf_to_buf_;
	}
}

static void __declspec(naked) combat_buttons_buf_to_buf() {
	__asm {
		mov  eax, IFaceBar::IFACE_BAR_WIDTH;
		mov  [esp + 0x14 + 4], eax; // to width
		mov  eax, xyOffsetCBtn;
		add  [esp + 0x10 + 4], eax; // to += 39 * (IFaceBar::IFACE_BAR_WIDTH - 640)
		jmp  fo::funcoffs::buf_to_buf_;
	}
}

static void __declspec(naked) combat_buttons_trans_buf_to_buf() {
	__asm {
		mov  eax, IFaceBar::IFACE_BAR_WIDTH;
		mov  [esp + 0x14 + 4], eax; // to width
		mov  eax, xyOffsetCBtn;
		add  [esp + 0x10 + 4], eax; // to += 39 * (IFaceBar::IFACE_BAR_WIDTH - 640)
		jmp  fo::funcoffs::trans_buf_to_buf_;
	}
}

static void __declspec(naked) intface_win_register_button() {
	__asm {
		add  edx, xOffset;
		jmp  fo::funcoffs::win_register_button_;
	}
}

//////////////////// Message Display Hacks ////////////////////

static void __declspec(naked) display_init_hack() {
	__asm {
		mov  eax, IFaceBar::display_width;
		imul eax, 60; // height
		jmp  fo::funcoffs::mem_malloc_;
	}
}

static void __cdecl display_init_hook_buf_to_buf(BYTE* scr, long w, long h, long srcWidth, BYTE* dispBuff, long dstWidth) {
	fo::var::setInt(FO_VAR_intface_full_width) = IFaceBar::IFACE_BAR_WIDTH;
	fo::var::disp_rect.offx = IFaceBar::IFACE_BAR_WIDTH - 451;

	fo::Window* ifaceWin = fo::func::GNW_find(fo::var::interfaceWindow);

	// save the interface background to the buffer (dispBuff) for redrawing
	fo::func::buf_to_buf(ifaceWin->surface + 23 + (24 * IFaceBar::IFACE_BAR_WIDTH), IFaceBar::display_width, h, IFaceBar::IFACE_BAR_WIDTH, dispBuff, IFaceBar::display_width);
}

static void __declspec(naked) display_init_hook_win_register_button() {
	__asm {
		mov  ecx, IFaceBar::display_width;
		jmp  fo::funcoffs::win_register_button_;
	}
}

static void __declspec(naked) display_redraw_hack() {
	__asm {
		idiv ebx;
		imul edx, 256; // was 80
		retn;
	}
}

static void __declspec(naked) DisplayReset() {
	__asm { // ebx: 100, ecx: 0
		mov  eax, IFaceBar::display_string_buf;
jloop:
		mov  [eax], cl;
		lea  eax, [eax + 256];
		dec  ebx;
		jnz  jloop;
		jmp  fo::funcoffs::display_redraw_;
	}
}

static void __declspec(naked) intface_rotate_numbers_hack() {
	__asm {
		imul edx, IFaceBar::IFACE_BAR_WIDTH; // y * width
		mov  eax, xOffset;
		add  [esp + 0x1C + 4], eax; // x + offset
		mov  eax, edx;
		retn;
	}
}

static void __declspec(naked) intface_rotate_numbers_hook_buf_to_buf() {
	__asm {
		mov  eax, IFaceBar::IFACE_BAR_WIDTH;
		mov  [esp + 0x14 + 4], eax; // to width
		jmp  fo::funcoffs::buf_to_buf_;
	}
}

static void __declspec(naked) intface_draw_ammo_lights_hack() {
	__asm {
		add  eax, xOffset;
		mov  esi, eax;
		test dl, 1;
		retn;
	}
}

void IFaceBar::Hide() {
	InterfaceHide(fo::var::getInt(FO_VAR_interfaceWindow));
}
void IFaceBar::Show() {
	InterfaceShow(fo::var::getInt(FO_VAR_interfaceWindow));
}

void IFaceBar::init() {
	namespace sf = sfall;

	if (IFACE_BAR_WIDTH < 640) {
		IFACE_BAR_WIDTH = 640;
	} else if (IFACE_BAR_WIDTH > 640) {
		display_width = IFaceBar::IFACE_BAR_WIDTH - 473; // message display width (800-473=327)
		display_string_buf = new char[100 * 256];

		sf::HookCall(0x45D950, intface_init_hook_buf_to_buf_ART);
		sf::HookCall(0x45E35C, intface_init_hook_buf_to_buf);

		sf::HookCalls(intface_update_move_points_hook_buf_to_buf, { 0x45EE43, 0x45EEDF, 0x45EF2D });

		sf::HookCalls(intface_win_register_button, {
			0x45DA0E, 0x45DAED, 0x45DC0D, 0x45DD44, 0x45DE33, 0x45DF22, 0x45E0B5, 0x45E1EF, // intface_init_
			0x460883, // intface_create_end_turn_button_
			0x4609E3, // intface_create_end_combat_button_
		});

		sf::HookCalls(combat_buttons_buf_to_buf, {
			0x45FA2A, 0x45FA7B, // intface_end_window_open_
			0x45FB87, 0x45FBD1, // intface_end_window_close_
		});
		// intface_end_buttons_enable_, intface_end_buttons_disable_
		sf::HookCalls(combat_buttons_trans_buf_to_buf, { 0x45FC72, 0x45FD06 });

		// display_init_  hacks
		sf::MakeCall(0x43166F, display_init_hack);
		sf::HookCall(0x431704, display_init_hook_buf_to_buf);
		sf::SafeWrite32(0x43172A, display_width);
		sf::SafeWrite32(0x431770, display_width);

		sf::HookCalls(DisplayReset, {
			0x4317EE, // display_init_
			0x431841  // display_reset_
		});
		// jle > jmp
		sf::SafeWrite8(0x4317C1, sf::CodeType::JumpShort); // display_init_
		sf::SafeWrite8(0x431814, sf::CodeType::JumpShort); // display_reset_

		// display_redraw_ hacks
		sf::MakeCall(0x431B19, display_redraw_hack);
		sf::SafeWrite32(0x431AB9, display_width);
		sf::SafeWrite32(0x431AC0, display_width);
		sf::SafeWrite32(0x431B3B, display_width);
		sf::SafeWrite32(0x431B36, (DWORD)display_string_buf);

		// rotate numbers hacks
		sf::MakeCall(0x460BF3, intface_rotate_numbers_hack, 4);
		// remove shl eax, 7
		sf::SafeWrite16(0x460C02, 0x9090);
		sf::SafeWrite8(0x460C04, 0x90);

		sf::HookCalls(intface_rotate_numbers_hook_buf_to_buf, {
			0x460CC4, 0x460CF8, 0x460D2C, 0x460D75, 0x460EA1, 0x460EF1, 0x460F47,
			0x460FA0, 0x460FDD, 0x461010, 0x461060, 0x461085, 0x4610AB, 0x4610EC
		});

		if (ALTERNATE_AMMO_METRE == 0) {
			// intface_draw_ammo_lights_ hacks
			sf::MakeCall(0x460AA6, intface_draw_ammo_lights_hack);
			sf::SafeWriteBatch<DWORD>(IFACE_BAR_WIDTH, { 0x460AC8, 0x460AD8, 0x460AE3 });
			sf::SafeWrite32(0x460AB4, 26 * IFACE_BAR_WIDTH); // y position
		}
	}

	sf::HookCall(0x45D8BC, intface_init_hook_win_add);
	sf::HookCall(0x4615FF, refresh_box_bar_win_hook_win_add);
	sf::HookCall(0x4AC260, skilldex_start_hook_win_add);

	sf::HookCall(0x45EA48, intface_show_hook_win_show);
	sf::HookCalls(intface_win_hide, {
		0x45E3F5, // intface_reset_
		0x45E8FB, // intface_load_
		0x45E9FD  // intface_hide_
	});

	if (ALTERNATE_AMMO_METRE > 0) {
	}

	if (IFACE_BAR_MODE > 0) {
		// Set view map height to game resolution
		// replace subtract 99 to add 1
		sf::SafeWrite8(0x481CDF, 1);
		sf::SafeWrite8(0x481E2E, 1);
		// replace subtract 99 to add 1
		sf::SafeWrite8(0x481DC3, -1);
		sf::SafeWrite8(0x4827A9, -1);
		// remove subtract 100
		sf::SafeWrite8(0x48284F, 0);
	}

	sf::LoadGameHook::OnBeforeGameClose() += []() {
		if (IFACE_BAR_WIDTH > 640) {
			delete[] display_string_buf;
			delete panels;
		}
	};
}

}