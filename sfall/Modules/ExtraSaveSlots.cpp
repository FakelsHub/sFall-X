/*
 *    sfall
 *    Copyright (C) 2009, 2010  Mash (Matt Wells, mashw at bigpond dot net dot au)
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>

#include "..\main.h"
#include "..\FalloutEngine\Fallout2.h"

#include "ExtraSaveSlots.h"

namespace sfall
{

static long LSPageOffset = 0;
static long LSButtDN = 0;
static BYTE* SaveLoadSurface = nullptr;

static const char* filename = "%s\\savegame\\slotdat.ini";

long ExtraSaveSlots::GetSaveSlot() {
	return LSPageOffset + fo::var::slot_cursor;
}

void ExtraSaveSlots::SetSaveSlot(long page, long slot) {
	if (GetQuickSavePage() >= 0 && page >= 0 && page <= 9990) LSPageOffset = page - (page % 10);;
	if (slot >= 0 && slot < 10) fo::var::slot_cursor = slot;
}

// save last slot position values to file
static long save_page_offsets() {
	char SavePath[MAX_PATH], buffer[6];

	sprintf_s(SavePath, MAX_PATH, filename, fo::var::patches);

	_itoa_s(fo::var::slot_cursor, buffer, 10);
	WritePrivateProfileStringA("POSITION", "ListNum", buffer, SavePath);

	_itoa_s(LSPageOffset, buffer, 10);
	WritePrivateProfileStringA("POSITION", "PageOffset", buffer, SavePath);

	return fo::var::lsgwin; // restore original code
}

static void LoadPageOffsets() {
	char LoadPath[MAX_PATH];

	sprintf_s(LoadPath, MAX_PATH, filename, fo::var::patches);

	fo::var::slot_cursor = IniReader::GetInt("POSITION", "ListNum", 0, LoadPath);
	if (fo::var::slot_cursor > 9) {
		fo::var::slot_cursor = 0;
	}
	LSPageOffset = IniReader::GetInt("POSITION", "PageOffset", 0, LoadPath);
	if (LSPageOffset > 9990) {
		LSPageOffset = 0;
	}
}

static void __declspec(naked) load_page_offsets(void) {
	__asm {
		// load last slot position values from file
		call LoadPageOffsets;
		mov  edx, 0x50A480;  // ASCII "SAV" (restore original code)
		retn;
	}
}

static long create_page_buttons() {
	DWORD winRef = fo::var::lsgwin;

	// left button -10                   | X | Y | W | H |HOn |HOff |BDown |BUp |PicUp |PicDown |? |ButType
	fo::func::win_register_button(winRef, 100, 60, 24, 20, -1, 0x500, 0x54B, 0x14B, 0, 0, 0, 32);
	// left button -100
	fo::func::win_register_button(winRef,  68, 60, 24, 20, -1, 0x500, 0x549, 0x149, 0, 0, 0, 32);
	// right button +10
	fo::func::win_register_button(winRef, 216, 60, 24, 20, -1, 0x500, 0x54D, 0x14D, 0, 0, 0, 32);
	// right button +100
	fo::func::win_register_button(winRef, 248, 60, 24, 20, -1, 0x500, 0x551, 0x151, 0, 0, 0, 32);
	// Set Number button
	fo::func::win_register_button(winRef, 140, 60, 60, 20, -1, -1, 'p', -1, 0, 0, 0, 32);

	return 101; // restore original value
}

static void SetPageNum() {
	DWORD winRef = fo::var::lsgwin; // load/save winref
	if (winRef == 0) {
		return;
	}
	fo::Window *SaveLoadWin = fo::func::GNW_find(winRef);
	if (SaveLoadWin->surface == nullptr) {
		return;
	}

	BYTE ConsoleGold = fo::var::YellowColor; // palette offset stored in mem - text colour

	char tempText[32];
	unsigned int TxtMaxWidth = fo::GetMaxCharWidth() * 6; // GetTextWidth(TempText);
	unsigned int HalfMaxWidth = TxtMaxWidth / 2;
	unsigned int TxtWidth = 0;

	DWORD NewTick = 0, OldTick = 0;
	int button = 0, exitFlag = 0, numpos = 0;
	char Number[4], blip = '_';

	DWORD tempPageOffset = -1;

	char* EndBracket = "]";
	int width = fo::GetTextWidth(EndBracket);

	while (!exitFlag) {
		NewTick = GetTickCount(); // timer for redraw
		if (OldTick > NewTick) {
			OldTick = NewTick;
		}
		if (NewTick - OldTick > 166) { // time to draw
			OldTick = NewTick;

			blip = (blip == '_') ? ' ' : '_';

			if (tempPageOffset == -1) {
				sprintf_s(tempText, 32, "[ %c ]", '_');
			} else {
				sprintf_s(tempText, 32, "[ %d%c ]", tempPageOffset / 10, '_');
			}
			TxtWidth = fo::GetTextWidth(tempText);

			if (tempPageOffset == -1) {
				sprintf_s(tempText, 32, "[ %c", blip);
			} else {
				sprintf_s(tempText, 32, "[ %d%c", tempPageOffset / 10, blip);
			}

			int z = 0;
			// paste image part from buffer into text area
			for (int y = SaveLoadWin->width * 62; y < SaveLoadWin->width * 74; y += SaveLoadWin->width) {
				memcpy(SaveLoadWin->surface + y + (170 - HalfMaxWidth), SaveLoadSurface + (100 - HalfMaxWidth) + (200 * z++), TxtMaxWidth);
			}

			int HalfTxtWidth = TxtWidth / 2;

			fo::PrintText(tempText, ConsoleGold, 170 - HalfTxtWidth, 64, TxtWidth, SaveLoadWin->width, SaveLoadWin->surface);
			fo::PrintText(EndBracket, ConsoleGold, (170 - HalfTxtWidth) + TxtWidth - width, 64, width, SaveLoadWin->width, SaveLoadWin->surface);
			fo::func::win_draw(winRef);
		}

		button = fo::func::get_input();
		if (button >= '0' && button <= '9') {
			if (numpos < 3) {
				Number[numpos] = button;
				Number[numpos + 1] = '\0';
				numpos++;
				if (Number[0] == '0') {
					numpos = 0;
					tempPageOffset = 0;
				} else {
					tempPageOffset = (atoi(Number)) * 10;
				}
			}
			//else exitFlag=-1;
		} else if (button == 0x08 && numpos) {
			numpos--;
			Number[numpos] = '\0';
			if (!numpos) {
				tempPageOffset = -1;
			} else {
				tempPageOffset = (atoi(Number)) * 10;
			}
		} else if (button == 0x0D || button == 0x20 || button == 'p' || button == 'P') {
			exitFlag = -1; // Enter, Space or P Keys
		} else if (button == 0x1B) {
			tempPageOffset = -1, exitFlag = -1; // Esc key
		}
	}

	if (tempPageOffset != -1 && tempPageOffset <= 9990) {
		LSPageOffset = tempPageOffset;
	}

	SaveLoadWin = nullptr;
}

static long __fastcall CheckPage(long button) {
	switch (button) {
		case 0x14B: // left button
			LSPageOffset -= 10;
			if (LSPageOffset < 0) LSPageOffset += 10000; // to Last Page
			__asm call fo::funcoffs::gsound_red_butt_press_;
			break;
		case 0x149: // fast left PGUP button
			LSPageOffset -= 100;
			if (LSPageOffset < 0) LSPageOffset += 10000;
			__asm call fo::funcoffs::gsound_red_butt_press_;
			break;
		case 0x14D: // right button
			LSPageOffset += 10;
			if (LSPageOffset >= 10000) LSPageOffset -= 10000; // to First Page
			__asm call fo::funcoffs::gsound_red_butt_press_;
			break;
		case 0x151: // fast right PGDN button
			LSPageOffset += 100;
			if (LSPageOffset >= 10000) LSPageOffset -= 10000;
			__asm call fo::funcoffs::gsound_red_butt_press_;
			break;
		case 'p': // p/P button pressed - start SetPageNum func
		case 'P':
			SetPageNum();
			break;
		default:
			if (button < 0x500) return 1; // button in down state
	}

	LSButtDN = button;
	return 0;
}

static void __declspec(naked) check_page_buttons(void) {
	__asm {
		push eax;
		push ecx;
		mov  ecx, eax;
		call CheckPage;
		test eax, eax;
		pop  ecx;
		pop  eax;
		jnz  checkUp;
		add  dword ptr ds:[esp], 26;        // set return to button pressed code
		jmp  fo::funcoffs::GetSlotList_;    // reset page save list func
checkUp:
		// restore original code
		cmp  eax, 0x148;                    // up button
		retn;
	}
}

static void DrawPageText() {
	if (fo::var::lsgwin == 0) {
		return;
	}
	fo::Window *SaveLoadWin = fo::func::GNW_find(fo::var::lsgwin);
	if (SaveLoadWin->surface == nullptr) {
		return;
	}

	int z = 0;
	if (SaveLoadSurface == nullptr) {
		SaveLoadSurface = new BYTE[2400];
		// save part of original image to buffer
		for (int y = SaveLoadWin->width * 62; y < SaveLoadWin->width * 74; y += SaveLoadWin->width) {
			memcpy(SaveLoadSurface + (200 * z++), SaveLoadWin->surface + 74 + y, 200);
		}
	} else {
		// paste image from buffer into text area
		for (int y = SaveLoadWin->width * 62; y < SaveLoadWin->width * 74; y += SaveLoadWin->width) {
			memcpy(SaveLoadWin->surface + 74 + y, SaveLoadSurface + (200 * z++), 200);
		}
	}

	BYTE ConsoleGreen = fo::var::GreenColor; // palette offset stored in mem - text colour
	BYTE ConsoleGold = fo::var::YellowColor; // palette offset stored in mem - text colour
	BYTE Colour = ConsoleGreen;

	char tempText[32];
	sprintf_s(tempText, 32, "[ %d ]", LSPageOffset / 10);

	unsigned int TxtWidth = fo::GetTextWidth(tempText);
	fo::PrintText(tempText, Colour, 170 - TxtWidth / 2, 64, TxtWidth, SaveLoadWin->width, SaveLoadWin->surface);

	if (LSButtDN == 0x549) {
		Colour = ConsoleGold;
	} else {
		Colour = ConsoleGreen;
	}
	std::strcpy(tempText, "<<");
	TxtWidth = fo::GetTextWidth(tempText);
	fo::PrintText(tempText, Colour, 80 - TxtWidth / 2, 64, TxtWidth, SaveLoadWin->width, SaveLoadWin->surface);

	if (LSButtDN == 0x54B) {
		Colour = ConsoleGold;
	} else {
		Colour = ConsoleGreen;
	}
	std::strcpy(tempText, "<");
	TxtWidth = fo::GetTextWidth(tempText);
	fo::PrintText(tempText, Colour, 112 - TxtWidth / 2, 64, TxtWidth, SaveLoadWin->width, SaveLoadWin->surface);

	if (LSButtDN == 0x551) {
		Colour = ConsoleGold;
	} else {
		Colour = ConsoleGreen;
	}
	std::strcpy(tempText, ">>");
	TxtWidth = fo::GetTextWidth(tempText);
	fo::PrintText(tempText, Colour, 260 - TxtWidth / 2, 64, TxtWidth, SaveLoadWin->width, SaveLoadWin->surface);

	if (LSButtDN == 0x54D) {
		Colour = ConsoleGold;
	} else {
		Colour = ConsoleGreen;
	}
	std::strcpy(tempText, ">");
	TxtWidth = fo::GetTextWidth(tempText);
	fo::PrintText(tempText, Colour, 228 - TxtWidth / 2, 64, TxtWidth, SaveLoadWin->width, SaveLoadWin->surface);

	SaveLoadWin = nullptr;
}

static void __declspec(naked) draw_page_text(void) {
	__asm {
		push eax;
		call DrawPageText;
		pop  eax;
		mov  ebp, 87; // restore original code
		retn;
	}
}

// add page num offset when reading and writing various save data files
static void __declspec(naked) add_page_offset_hack1(void) {
	__asm {
		mov  eax, dword ptr ds:[FO_VAR_slot_cursor]; // list position 0-9
		add  eax, LSPageOffset;                      // add page num offset
		dec  eax; // align the numbering of slots in page (starting from the zero slot)
		retn;
	}
}

// getting info for the 10 currently displayed save slots from save.dats
static void __declspec(naked) add_page_offset_hack2(void) {
	__asm {
		push 0x50A514;          // ASCII "SAVE.DAT"
		mov  eax, ebx;          // was lea  eax, [ebx + 1]; -remove for starting from the zero slot
		add  eax, LSPageOffset; // add page num offset
		mov  edx, 0x47E5E9;     // ret addr
		jmp  edx;
	}
}

// printing current 10 slot numbers
static void __declspec(naked) add_page_offset_hack3(void) {
	__asm {
//		inc  eax; -remove for starting from the zero slot
		add  eax, LSPageOffset;            // add page num offset
		mov  bl, byte ptr ss:[esp + 0x10]; // add 4 bytes - func ret addr
		retn;
	}
}

static void EnableSuperSaving() {

	// save/load button setup func
	MakeCall(0x47D80D, create_page_buttons); // LSGameStart_

	// Draw button text
	MakeCall(0x47E6E8, draw_page_text); // ShowSlotList_

	// check save/load buttons
	MakeCalls(check_page_buttons, {0x47BD49, 0x47CB1C}); // SaveGame_, LoadGame_

	// save current page and list positions to file on load/save scrn exit
	MakeCall(0x47D828, save_page_offsets); // LSGameEnd_

	// load saved page and list positions from file
	MakeCall(0x47B82B, load_page_offsets); // InitLoadSave_

	// Add Load/Save page offset to Load/Save folder number
	MakeCalls(add_page_offset_hack1, {
		0x47B929, // SaveGame_
		0x47D8DB, 0x47D9B0, 0x47DA34, 0x47DABF, 0x47DB58, 0x47DBE9, // SaveSlot_
		0x47EC77, // LoadTumbSlot_
		0x47DC9C, // LoadSlot_
		0x47F5AB, 0x47F694, 0x47F6EB, 0x47F7FB, 0x47F892, // GameMap2Slot_
		0x47FB86, 0x47FC3A, 0x47FCF2,           // SlotMap2Game_
		0x480117, 0x4801CF, 0x480234, 0x480310, // SaveBackup_
		0x4803F3, 0x48049F, 0x480512, 0x4805F2, // RestoreSave_
		0x480767, 0x4807E6, 0x480839, 0x4808D3  // EraseSave_
	});

	MakeJump(0x47E5E1, add_page_offset_hack2); // GetSlotList_

	MakeCall(0x47E756, add_page_offset_hack3); // ShowSlotList_
}

static void GetSaveFileTime(char* filename, FILETIME* ftSlot) {
	char fname[65];
	sprintf_s(fname, "%s\\%s", fo::var::patches, filename);

	HANDLE hFile = CreateFile(fname, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile != INVALID_HANDLE_VALUE) {
		GetFileTime(hFile, NULL, NULL, ftSlot);
		CloseHandle(hFile);
	} else {
		ftSlot->dwHighDateTime = 0;
		ftSlot->dwLowDateTime = 0;
	};
}

static const char* autoFmt  = "AUTO: %02d/%02d/%d - %02d:%02d:%02d";
static const char* quickFmt = "QUICK: %02d/%02d/%d - %02d:%02d:%02d";

static void CreateSaveComment(char* bufstr, bool isAuto) {
	SYSTEMTIME stUTC, stLocal;
	GetSystemTime(&stUTC);
	SystemTimeToTzSpecificLocalTime(NULL, &stUTC, &stLocal);

	const char* fmt = (isAuto) ? autoFmt : quickFmt;
	sprintf_s(bufstr, 30, fmt, stLocal.wDay, stLocal.wMonth, stLocal.wYear, stLocal.wHour, stLocal.wMinute, stLocal.wSecond);
}

static long quickSavePageInit = -1;
static long quickSavePageCount;
static long currentPageCount;

static long quickSavePage = -1;
static long quickSaveSlot = 0;

static long dontCheckSlot = 0;
static bool qFirst = true;

static FILETIME ftPrevSlot;

static DWORD __stdcall QuickSaveGame(fo::DbFile* file, char* filename) {
	long currSlot = quickSaveSlot;

	if (dontCheckSlot) {
		if (file) fo::func::db_fclose(file);
	} else {
	// for quick feature
		if (file) { // This slot is not empty
			fo::func::db_fclose(file);

			FILETIME ftCurrSlot;
			GetSaveFileTime(filename, &ftCurrSlot);

			if (currSlot == 0 ||
			    ftCurrSlot.dwHighDateTime > ftPrevSlot.dwHighDateTime ||
			   (ftCurrSlot.dwHighDateTime == ftPrevSlot.dwHighDateTime && ftCurrSlot.dwLowDateTime > ftPrevSlot.dwLowDateTime))
			{
				ftPrevSlot.dwHighDateTime = ftCurrSlot.dwHighDateTime;
				ftPrevSlot.dwLowDateTime  = ftCurrSlot.dwLowDateTime;

				if (!qFirst && currSlot < 9) {
					fo::var::slot_cursor = ++quickSaveSlot;
					return 0x47B929; // check next slot
				}
				currSlot = 0; // set if currSlot >= 9
				qFirst = false;
			}
		}
		// next save slot
		if (++quickSaveSlot >= 10) {
			quickSaveSlot = 0;
			// next page
			if (quickSavePageCount > 1 && quickSavePageInit != -1) {
				if (++currentPageCount >= quickSavePageCount) {
					currentPageCount = 0;
					quickSavePage = quickSavePageInit;
				} else if (quickSavePage <= 9980) {
					quickSavePage += 10;
				}
			}
		}
	}

	// Save to slot
	fo::var::slot_cursor = currSlot;
	fo::LSData* saveData = (fo::LSData*)FO_VAR_LSData;
	CreateSaveComment(saveData[currSlot].comment, dontCheckSlot != 0);
	fo::var::quick_done = 1;

	return 0x47B9A4; // normal return
}

static void __declspec(naked) SaveGame_hack0() {
	__asm {
		mov  ds:[FO_VAR_flptr], eax;
		push ecx;
		push edi;
		push eax;
		call QuickSaveGame;
		pop  ecx;
		jmp  eax;
	}
}

static void __declspec(naked) SaveGame_hack1() {
	__asm {
		mov eax, quickSaveSlot;
		mov ds:[FO_VAR_slot_cursor], eax;
		mov eax, quickSavePage;
		mov LSPageOffset, eax;
		retn;
	}
}

///////////////////////////////////////////////////////////////////////////////

static void __fastcall SetSaveComment(char* comment) {
	int i = 0;
	const char* mapName = fo::func::map_get_short_name(fo::var::map_number);
	do {
		comment[i] = mapName[i];
	} while (++i < 20 && mapName[i]);
	comment[i++] = ':';
	comment[i] = '\0';
}

static void __declspec(naked) GetComment_hack() {
	__asm {
		cmp [ecx], 0;
		jne notEmpty;
		pushadc;
		call SetSaveComment;
		popadc;
notEmpty:
		push 0x47F031;
		jmp  fo::funcoffs::get_input_str2_;
	}
}

long ExtraSaveSlots::GetQuickSavePage() {
	return quickSavePage;
}

long ExtraSaveSlots::GetQuickSaveSlot() {
	return quickSaveSlot;
}

void ExtraSaveSlots::SetQuickSaveSlot(long page, long slot, long check) {
	if (quickSavePage >= 0 && page >= 0 && page <= 9990) quickSavePage = page - (page % 10);
	if (slot >= 0 && slot < 10) quickSaveSlot = slot;
	dontCheckSlot = check;
}

void ExtraSaveSlots::init() {

	//bool extraSaveSlots = (IniReader::GetConfigInt("Misc", "ExtraSaveSlots", 1) != 0);
	//if (extraSaveSlots) {
		dlog("Applying extra save slots patch.", DL_INIT);
		EnableSuperSaving();
		dlogr(" Done", DL_INIT);
	//}

	quickSavePageCount = IniReader::GetConfigInt("Misc", "AutoQuickSave", 0);
	if (quickSavePageCount > 0) {
		dlog("Applying auto quick save patch.", DL_INIT);
		if (quickSavePageCount > 10) quickSavePageCount = 10;

		quickSavePage = IniReader::GetConfigInt("Misc", "AutoQuickSavePage", 1);
		if (quickSavePage > 999) quickSavePage = 999;

		if (/*extraSaveSlots &&*/ quickSavePage >= 0) {
			quickSavePage *= 10;
			quickSavePageInit = quickSavePage;
			MakeCall(0x47B923, SaveGame_hack1, 1);
		} else { // for quickSavePage = -1
			SafeWrite8(0x47B923, 0x89);
			SafeWrite32(0x47B924, 0x5193B83D); // mov [slot_cursor], edi = 0
		}
		MakeJump(0x47B984, SaveGame_hack0);
		dlogr(" Done", DL_INIT);
	}

	// Adds the city name in the description for empty save slots
	MakeJump(0x47F02C, GetComment_hack);
}

void ExtraSaveSlots::exit() {
	if (SaveLoadSurface) delete[] SaveLoadSurface;
}

}
