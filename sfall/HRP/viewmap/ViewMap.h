/*
 *    sfall
 *    Copyright (C) 2008 - 2021 Timeslip and sfall team
 *
 */

#pragma once

namespace sfall
{

class ViewMap {
public:
	static void init();

	static long SCROLL_DIST_X;
	static long SCROLL_DIST_Y;
	static bool IGNORE_PLAYER_SCROLL_LIMITS;
	static bool IGNORE_MAP_EDGES;
	static bool EDGE_CLIPPING_ON;

	static long mapWidthModSize;
	static long mapHeightModSize;
	static long mapModWidth;
	static long mapModHeight;

	static void GetCoordFromOffset(long &inOutX, long &inOutY);
	static void GetTileCoord(long tile, long &outX, long &outY);
	static void GetTileCoordOffset(long tile, long &outX, long &outY);
	static void GetWinMapHalfSize(long &outW, long &outH);
};

}