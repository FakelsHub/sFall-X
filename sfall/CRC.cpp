/*
 *    sfall
 *    Copyright (C) 2009, 2010  The sfall team
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

#include <algorithm>
#include <stdio.h>

#include "main.h"
#include "Version.h"
#include "Logging.h"

#include "CRC.h"

namespace sfall
{

static const DWORD ExpectedSize = 1189888;
static const DWORD ExpectedCRC[] = {
	0xE1680293, // US 1.02d
	0xEF34F989  // US 1.02d + HRP by Mash
};

static void inline ExitMessageFail(const char* a) {
	MessageBoxA(0, a, 0, MB_TASKMODAL | MB_ICONERROR);
	ExitProcess(1);
}

static DWORD CalcCRCInternal(BYTE* data, DWORD size) {
	DWORD tableCRC[256];
	const DWORD Polynomial = 0x1EDC6F41; // Castagnoli

	for (size_t i = 0; i < 256; i++) {
		DWORD r = i;
		for (size_t j = 0; j < 8; j++) {
			r = (r >> 1) ^ (Polynomial & (0 - (r & 1))); // (r & 1) ? (r >> 1) ^ Polynomial : r >> 1;
		}
		tableCRC[i] = r;
	}

	DWORD crc = 0xFFFFFFFF;
	for (DWORD i = 0; i < size; i++) {
		crc = tableCRC[(((BYTE)(crc)) ^ data[i])] ^ (crc >> 8);
	}
	return crc ^ 0xFFFFFFFF;
}

static bool CheckExtraCRC(DWORD crc) {
	auto extraCrcList = IniReader::GetListDefaultConfig("Debugging", "ExtraCRC", "", 512, ',');
	if (!extraCrcList.empty()) {
		return std::any_of(extraCrcList.begin(), extraCrcList.end(), [crc](const std::string& testCrcStr)
			{
				auto testedCrc = strtoul(testCrcStr.c_str(), 0, 16);
				return testedCrc && crc == testedCrc; // return for lambda
			}
		);
	}
	return false;
}

DWORD CRC(const char* filepath) {
	char buf[512];

	HANDLE h = CreateFileA(filepath, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
	if (h == INVALID_HANDLE_VALUE) {
		ExitMessageFail("Cannot open game exe for CRC check.");
		return 0;
	}

	DWORD size = GetFileSize(h, 0);
	if (size < (ExpectedSize / 2)) {
		CloseHandle(h);
		ExitMessageFail("You're trying to use sfall with an incompatible version of Fallout.");
		return 0;
	}

	DWORD crc;
	BYTE* bytes = new BYTE[size];
	ReadFile(h, bytes, size, &crc, 0);
	CloseHandle(h);

	crc = CalcCRCInternal(bytes, size);
	delete[] bytes;

	for (int i = 0; i < sizeof(ExpectedCRC) / 4; i++) {
		if (crc == ExpectedCRC[i]) return -1; // vanilla CRC
	}

Retry:
	bool matchedCRC = CheckExtraCRC(crc);
	if (!matchedCRC) {
		sprintf_s(buf, "You're trying to use sfall with an incompatible version of Fallout.\n"
					   "Was expecting '" TARGETVERSION "'.\n\n"
					   "%s has an unexpected CRC.\nExpected 0x%x but got 0x%x.", filepath, ExpectedCRC[0], crc);

		if (size == ExpectedSize) {
			switch (MessageBoxA(0, buf, "CRC Mismatch", MB_TASKMODAL | MB_ICONWARNING | MB_ABORTRETRYIGNORE)) {
			case IDABORT:
				std::exit(EXIT_FAILURE);
				break;
			case IDRETRY:
				goto Retry;
			}
			return crc;
		}
		ExitMessageFail(buf);
	}
	return (matchedCRC) ? crc : 0;
}

DWORD GetCRC(FILE* fl) {
	std::fseek(fl, 0, SEEK_END);
	size_t size = std::ftell(fl);
	BYTE* bytes = new BYTE[size];

	std::fseek(fl, 0, SEEK_SET);
	std::fread(bytes, 1, size, fl);

	DWORD crc = CalcCRCInternal(bytes, size);
	delete[] bytes;

	return (!CheckExtraCRC(crc)) ? crc : 0; // checks whether the CRC is contained in the list
}

}
