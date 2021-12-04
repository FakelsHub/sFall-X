/*
 *    sfall
 *    Copyright (C) 2008 - 2021 Timeslip and sfall team
 *
 */

#pragma once

namespace sfall
{

class DirectDraw {
public:
	static void init(long);

	#pragma pack(push, 1)
	struct PALCOLOR {
		union {
			DWORD xRGB;
			struct {
				BYTE R;
				BYTE G;
				BYTE B;
				BYTE x;
			};
		};
	};
	#pragma pack(pop)
};

}