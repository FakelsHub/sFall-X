/*
 *    sfall
 *    Copyright (C) 2008 - 2021 Timeslip and sfall team
 *
 */

#include <array>

#include "..\FalloutEngine\Fallout2.h"
#include "..\main.h"

#include "tilemap.h"

namespace game
{

namespace sf = sfall;

static fo::GameObject* __fastcall obj_path_blocking_at(fo::GameObject* source, long tile, long elev) {
	if (tile < 0 || tile >= 40000) return nullptr;

	fo::ObjectTable* obj = fo::var::objectTable[tile];
	while (obj)
	{
		if (elev == obj->object->elevation) {
			fo::GameObject* object = obj->object;
			long flags = object->flags;
			if (!(flags & (fo::ObjectFlag::Mouse_3d | fo::ObjectFlag::NoBlock)) && source != object) {
				char type = object->TypeFid();
				if (type == fo::ObjType::OBJ_TYPE_SCENERY || type == fo::ObjType::OBJ_TYPE_WALL) {
					return object;
				}
			}
		}
		obj = obj->nextObject;
	}
	// check for multi-hex objects on the tile
	long direction = 0;
	do {
		long _tile = fo::func::tile_num_in_direction(tile, direction, 1);
		if (_tile >= 0 && _tile < 40000) {
			obj = fo::var::objectTable[_tile];
			while (obj)
			{
				if (elev == obj->object->elevation) {
					fo::GameObject* object = obj->object;
					long flags = object->flags;
					if (flags & fo::ObjectFlag::MultiHex && !(flags & (fo::ObjectFlag::Mouse_3d | fo::ObjectFlag::NoBlock)) && source != object) {
						char type = object->TypeFid();
						if (type == fo::ObjType::OBJ_TYPE_SCENERY || type == fo::ObjType::OBJ_TYPE_WALL) {
							return object;
						}
					}
				}
				obj = obj->nextObject;
			}
		}
	} while (++direction < 6);
	return nullptr;
}

void __declspec(naked) Tilemap::obj_path_blocking_at_() {
	__asm {
		push ecx;
		push ebx;
		mov  ecx, eax;
		call obj_path_blocking_at;
		pop  ecx;
		retn;
	}
}

//static std::vector<int> buildLineTiles;

//static bool IsExistTile(long tile) {
//	return (std::find(buildLineTiles.cbegin(), buildLineTiles.cend(), tile) != buildLineTiles.cend());
//}

// Fixed and improved implementation of the tile_num_beyond_ engine function
// - correctly gets the tile from the constructed line
// - fixed the range when the target tile was positioned at the maximum distance
long __fastcall Tilemap::tile_num_beyond(long sourceTile, long targetTile, long maxRange) {
	if (maxRange <= 0 || sourceTile == targetTile) return sourceTile;

	/*if (buildLineTiles.empty()) {
		buildLineTiles.reserve(100);
	} else {
		buildLineTiles.clear();
	}*/

	long currentRange = fo::func::tile_dist(sourceTile, targetTile);
	//fo::func::debug_printf("\ntile_dist: %d", currentRange);
	if (currentRange == maxRange) maxRange++; // increase the range, if the target is located at a distance equal to maxRange [fix range]

	long lastTile = targetTile;
	long source_X, source_Y, target_X, target_Y;

	fo::func::tile_coord(sourceTile, &source_X, &source_Y);
	fo::func::tile_coord(targetTile, &target_X, &target_Y);

	// set the point to the center of the hexagon
	source_X += 16;
	source_Y += 8;
	target_X += 16;
	target_Y += 8;

	long diffX = target_X - source_X;
	long addToX = -1;
	if (diffX >= 0) addToX = (diffX > 0);

	long diffY = target_Y - source_Y;
	long addToY = -1;
	if (diffY >= 0) addToY = (diffY > 0);

	long diffX_x2 = 2 * std::abs(diffX);
	long diffY_x2 = 2 * std::abs(diffY);

	const int step = 4;
	long stepCounter = 1;

	if (diffX_x2 > diffY_x2) {
		long stepY = diffY_x2 - (diffX_x2 >> 1);
		while (true)
		{
			if (!--stepCounter) {
				long tile = fo::func::tile_num(target_X, target_Y);
				//fo::func::debug_printf("\ntile_num: %d [x:%d y:%d]", tile, target_X, target_Y);
				if (tile != lastTile) {
					//if (!IsExistTile(tile)) {
						long dist = fo::func::tile_dist(targetTile, tile);
						if ((dist + currentRange) >= maxRange || fo::func::tile_on_edge(tile)) return tile;
					//	buildLineTiles.push_back(tile);
					//}
					lastTile = tile;
				}
				stepCounter = step;
			}
			if (stepY >= 0) {
				stepY -= diffX_x2;
				target_Y += addToY;
			}
			stepY += diffY_x2;
			target_X += addToX;

			/// Example of an algorithm constructing a straight line from point A to B
			/// source: x = 784(800) y = 278(286)
			/// target: x = 640(656) y = 314(322) - the target is located to the left and below
			/// 0. x = 800 y = 286 stepY = -72
			/// 1. x = 799 y = 286 stepY = -72+72=0
			/// 2. x = 798 y = 287 stepY =  0-288+72=-216
			/// 3. x = 797 y = 287 stepY = -216+72=-144
			/// 4. x = 796 y = 287 stepY = -144+72=-72
			/// 5. x = 795 y = 287 stepY = -72+72=0
			/// 6. x = 794 y = 288 stepY =  0-288+72=-216
		}
	} else {
		long stepX = diffX_x2 - (diffY_x2 >> 1);
		while (true)
		{
			if (!--stepCounter) {
				long tile = fo::func::tile_num(target_X, target_Y);
				//fo::func::debug_printf("\ntile_num: %d [x:%d y:%d]", tile, target_X, target_Y);
				if (tile != lastTile) {
					//if (!IsExistTile(tile)) {
						long dist = fo::func::tile_dist(targetTile, tile);
						if ((dist + currentRange) >= maxRange || fo::func::tile_on_edge(tile)) return tile;
					//	buildLineTiles.push_back(tile);
					//}
					lastTile = tile;
				}
				stepCounter = step;
			}
			if (stepX >= 0) {
				stepX -= diffY_x2;
				target_X += addToX;
			}
			stepX += diffX_x2;
			target_Y += addToY;
		}
	}
}

static void __declspec(naked) tile_num_beyond_replacement() {
	__asm { //push ecx;
		push ebx;
		mov  ecx, eax;
		call Tilemap::tile_num_beyond;
		pop  ecx;
		retn;
	}
}

struct sfChild {
	long tile;
	long from_tile;
	long distance;
	uint16_t accumulator; // the higher the value, the less likely it is to use this tile to build a path
	char rotation;
};

static std::array<sfChild, 2000> m_pathData;
static std::array<sfChild, 2000> m_dadData;
static std::array<uint8_t, 5000> seenTile;

static __forceinline fo::GameObject* CheckTileBlocking(void* blockFunc, long tile, fo::GameObject* object) {
	using namespace fo::Fields;
	__asm {
		mov  eax, object;
		mov  edx, tile;
		mov  ebx, [eax + elevation];
		call blockFunc;
		//mov  object, eax;
	}
	//return object;
}

// same as idist_
static __inline long DistanceFromPositions(long sX, long sY, long tX, long tY) {
	long diffX = std::abs(tX - sX);
	long diffY = std::abs(tY - sY);
	long minDiff = (diffX <= diffY) ? diffX : diffY;
	return (diffX + diffY) - (minDiff >> 1);
}

// optimized version with added 'type' arg
// type: 1 - tile path, 0 - rotation path
long __fastcall Tilemap::make_path_func(fo::GameObject* srcObject, long sourceTile, long targetTile, long type, void* arrayRef, long checkTargetTile, void* blockFunc) {

	if (checkTargetTile && fo::func::obj_blocking_at_wrapper(srcObject, targetTile, srcObject->elevation, blockFunc)) return 0;

	bool inCombat = fo::var::combat_state & fo::CombatStateFlag::InCombat;
	int8_t critterType = fo::KillType::KILL_TYPE_men;

	bool isCritter = srcObject->IsCritter();
	if (isCritter) {
		critterType = static_cast<int8_t>(fo::func::critter_kill_count_type(srcObject));
	}

	seenTile.fill(0);
	seenTile[sourceTile >> 3] = 1 << (sourceTile & 7);

	auto& it = m_pathData.begin();
	it->tile = sourceTile;
	it->from_tile = -1;
	it->distance = fo::func::tile_idistance(sourceTile, targetTile); // maximum distance
	it->rotation = 0;
	it->accumulator = 0;
	while (++it != m_pathData.end()) it->tile = -1;

	long targetX, targetY;
	fo::func::tile_coord(targetTile, &targetX, &targetY);

	auto& dadData = m_dadData.begin();
	size_t pathCounter = 1;
	sfChild childData;

	// search path tiles
	while (true)
	{
		auto& pathData = m_pathData.begin();
		if (pathCounter > 0) {
			size_t counter = 0;
			long prevDistance;

			// search for the element with the smallest distance
			for (auto currData = pathData; currData != m_pathData.end(); ++currData)
			{
				if (currData->tile != -1) {
					long currDistance = currData->distance + currData->accumulator;
					if (counter == 0 || currDistance < prevDistance) {
						prevDistance = currDistance;
						pathData = currData;
					}
					if (++counter >= pathCounter) break;
				}
			}
		}
		childData = *pathData; // copy element data
		pathData->tile = -1;   // set a free element
		pathCounter--;

		if (childData.tile == targetTile) break; // exit the loop path is built

		*dadData = childData;
		if (++dadData == m_dadData.end()) {
			return 0; // path can't be built reached the end of the array (limit the maximum path length)
		}

		char rotation = 0;
		do {
			long tile = fo::func::tile_num_in_direction(childData.tile, rotation, 1);

			long seenIndex = tile >> 3;
			uint8_t seenMask = 1 << (tile & 7);
			if (!(seenTile[seenIndex] & seenMask)) {
				seenTile[seenIndex] |= seenMask;

				if (tile != targetTile) {
					fo::GameObject* objBlock = CheckTileBlocking(blockFunc, tile, srcObject);
					if (objBlock) {
						// [FIX] Fixes building the path to the central hex of a multihex object (from BugFixes.cpp)
						if (objBlock->tile != targetTile) {
							if (!fo::func::anim_can_use_door(srcObject, objBlock)) continue; // block - next rotation
						} else {
							targetTile = tile; // replace the target tile (where the multihex object is located) with the current tile
						}
					}
				}
				if (++pathCounter >= 2000) { // ограничение максимальной длины пути?
					return 0;
				}

				pathData = m_pathData.begin();
				while (pathData->tile != -1) ++pathData; // search the first free element

				pathData->tile = tile;
				pathData->from_tile = childData.tile;
				pathData->rotation = rotation;
				pathData->accumulator = childData.accumulator + 50; // here childData.accumulator has the value

				if (!inCombat && rotation != childData.rotation) pathData->accumulator += 10;

				long x, y;
				fo::func::tile_coord(tile, &x, &y);
				pathData->distance = DistanceFromPositions(x, y, targetX, targetY);

				if (isCritter) {
					fo::GameObject* object = fo::func::obj_find_first_at_tile(srcObject->elevation, tile);
					while (object) {
						// TODO: add check other PIDs
						if (object->protoId >= fo::ProtoID::PID_RAD_GOO_1 && object->protoId <= fo::ProtoID::PID_RAD_GOO_4) {
							pathData->accumulator += (critterType == fo::KillType::KILL_TYPE_gecko) ? 100 : 400;
							break;
						}
						object = fo::func::obj_find_next_at_tile();
					}
				}
			}
		} while (++rotation < 6);
		if (!pathCounter) return 0; // the path can't be built
	}

	uint8_t* arrayR = reinterpret_cast<uint8_t*>(arrayRef);
	uint16_t* arrayT = reinterpret_cast<uint16_t*>(arrayRef);
	size_t pathLen = 0;

	// building and calculating the path length
	do {
		if (childData.tile == sourceTile) break; // reached the source tile
		if (arrayRef) {
			if (type) {
				*arrayT++ = (uint16_t)childData.tile;
			} else {
				*arrayR++ = childData.rotation;
			}
		}
		while (childData.from_tile != dadData->tile) --dadData; // search a linked tile 'from -> tile'
		childData = *dadData;
	} while (++pathLen < 800);

	if (arrayRef && pathLen > 1) {
		// reverse the array values
		size_t count = pathLen >> 1;
		if (type) {
			uint16_t* arrayFront = reinterpret_cast<uint16_t*>(arrayRef);;
			do {
				uint16_t last = *--arrayT;
				*arrayT = *arrayFront; // last < front
				*arrayFront++ = last;
			} while (--count);
		} else {
			uint8_t* arrayFront = reinterpret_cast<uint8_t*>(arrayRef);;
			do {
				uint8_t last = *--arrayR;
				*arrayR = *arrayFront; // last < front
				*arrayFront++ = last;
			} while (--count);
		}
	}
	return pathLen;
}

//static void __declspec(naked) make_path_func_replacement() {
//	__asm {
//		xchg [esp], ecx; // ret addr <> array
//		push 0;          // type
//		push ebx;        // target tile
//		push ecx;        // ret addr
//		mov  ecx, eax;
//		jmp  Tilemap::make_path_func;
//	}
//}

void Tilemap::init() {
	sf::MakeJump(fo::funcoffs::tile_num_beyond_ + 1, tile_num_beyond_replacement); // 0x4B1B84

	// for test in game
	//sf::MakeJump(fo::funcoffs::make_path_func_, make_path_func_replacement); // 0x415EFC
}

}