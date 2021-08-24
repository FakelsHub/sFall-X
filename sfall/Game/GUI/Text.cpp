/*
 *    sfall
 *    Copyright (C) 2008 - 2021 Timeslip and sfall team
 *
 */

#include "..\..\FalloutEngine\Fallout2.h"

#include "..\..\main.h"

#include "Text.h"

namespace game
{
namespace gui
{

// Returns the position of the newline character, or the position of the character within the specified width (implementation from HRP)
static long GetPositionWidth(const char* text, long width) {
	long gapWidth;
	__asm {
		call dword ptr ds:[FO_VAR_text_spacing];
		mov  gapWidth, eax;
	}

	long wordCharCount = 0;
	long position = 0;
	long w = 0;

	char c = text[position];
	while (c)
	{
		if (c == '\\' && text[position + 1] == 'n') return position;

		if (c != ' ') wordCharCount++; else wordCharCount = 0;

		w += gapWidth + fo::util::Get_CharWidth(c);
		if (w > width) {
			// set the position to the beginning of the current word
			if (wordCharCount > 1 && ((wordCharCount - 1) != position)) {
				position -= wordCharCount; // position on the space char
			}
			break;
		}
		c = text[++position];
	}
	return position;
}

// Implementation of the display_print_ function from HRP with support for the control character '\n' for line wrapping
// Only for HRP 4.1.8
static void __fastcall DisplayPrintLineBreak(const char* message) {
	if (*message == 0 || !fo::var::GetInt(FO_VAR_disp_init)) return;

	const long max_disp_chars = 256; // HRP value  (vanilla 80)
	const long max_lines = 100;      // aka FO_VAR_max

	unsigned char bulletChar = 149;
	long wChar = fo::util::Get_CharWidth(bulletChar);
	long width = sfall::GetIntHRPValue(HRP_VAR_disp_width) - wChar - fo::var::GetInt(FO_VAR_max_disp);
	const char* text = message;

	long font = fo::var::curr_font_num;
	fo::func::text_font(101);

	if (!(fo::var::combat_state & 1)) {
		long time = fo::var::GetInt(FO_VAR_bk_process_time);
		if ((time - fo::var::GetInt(FO_VAR_lastTime)) >= 500) {
			*fo::var::SetInt(FO_VAR_lastTime) = time;
			fo::func::gsound_play_sfx_file((const char*)0x50163C); // "monitor"
		}
	}

	// array size 100x256, allocated by HRP
	char* display_string_buf_addr = (char*)sfall::HRPAddress(HRP_VAR_display_string_buf);
	do {
		char* display_string_buf = &display_string_buf_addr[max_disp_chars * fo::var::GetInt(FO_VAR_disp_start)];

		long pos = GetPositionWidth(text, width);

		if (bulletChar) {
			*display_string_buf = bulletChar;
			display_string_buf++;
			bulletChar = 0;
			width += wChar;
		}

		std::strncpy(display_string_buf, text, pos);
		display_string_buf[pos] = 0;

		if (text[pos] == ' ') {
			pos++;
		} else if (text[pos] == '\\' && text[pos + 1] == 'n') {
			pos += 2; // position behind the 'n' character
		}
		text += pos;

		*fo::var::SetInt(FO_VAR_disp_start) = (fo::var::GetInt(FO_VAR_disp_start) + 1) % max_lines;
	} while (*text);

	*fo::var::SetInt(FO_VAR_disp_curr) = fo::var::GetInt(FO_VAR_disp_start);

	fo::func::text_font(font);
    __asm call fo::funcoffs::display_redraw_;
}

static void __declspec(naked) sf_display_print() {
	__asm {
		push ecx;
		mov  ecx, eax; // message
		call DisplayPrintLineBreak;
		pop  ecx;
		retn;
	}
}

static void __stdcall InvenDisplayLineBreak(char* message) {
	char* text = message;
	while (*text)
	{
		if (text[0] == '\\' && text[1] == 'n') {
			*text = 0; // set "End of Line"

			__asm mov  eax, message;
			__asm call fo::funcoffs::inven_display_msg_;

			*text = '\\';
			text += 2; // position behind the 'n' character
			message = text;
		} else {
			text++;
		}
	}
	// print the last line or the all text if there was no line break
	if (message != text) {
		__asm mov  eax, message;
		__asm call fo::funcoffs::inven_display_msg_;
	}
}

static void __declspec(naked) sf_inven_display_msg() {
	__asm {
		push  ecx;
		push  eax; // message
		call  InvenDisplayLineBreak;
		pop   ecx;
		retn;
	}
}

void Text::init() {

	// Support for the line break control character '\n' to describe the prototypes in game\pro_*.msg files
	if (sfall::hrpVersionValid) { 
		sfall::SafeWriteBatch<DWORD>((DWORD)sf_display_print, { 0x46ED87, 0x49AD7A }); // setup_inventory_, obj_examine_
		sfall::SafeWrite32(0x472F9A, (DWORD)sf_inven_display_msg); // inven_obj_examine_func_
	}
}

}
}