/*
 *    sfall
 *    Copyright (C) 2008 - 2021 Timeslip and sfall team
 *
 */

#pragma once

namespace HRP
{

class EdgeBorder {
public:
	class Edge {
	public:
		POINT center;    // x/y center of current map screen?
		RECT borderRect; // right is less than left
		RECT rect_2;     //
		RECT tileRect;
		RECT squareRect;    // angel clipping
		long clipData;      // angel clip type
		Edge* prevEdgeData; // unused (use in 3.06)
		Edge* nextEdgeData;

		void Release() {
			Edge* edge = nextEdgeData;
			while (edge) {
				Edge* edgeNext = edge->nextEdgeData;
				delete edge;
				edge = edgeNext;
			};
		}

		~Edge() {
			Release();
		}
	};

	static void init();

	static Edge* CurrentMapEdge();
	static long EdgeBorder::EdgeVersion();

	static long GetCenterTile(long tile, long mapLevel);
	static long CheckBorder(long tile);
};

}