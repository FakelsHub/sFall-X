/*
* sfall
* Copyright (C) 2008-2016 The sfall team
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include <cstdint>

#include "FunctionOffsets.h"
#include "Structs.h"
#include "VariableOffsets.h"

#include "EngineUtils.h"

// TODO: split these functions into several files
namespace fo
{
namespace util
{

static fo::MessageNode messageBuf;

const char* GetMessageStr(const fo::MessageList* file, long messageId) {
	return fo::func::getmsg(file, &messageBuf, messageId);
}

const char* MessageSearch(const fo::MessageList* file, long messageId) {
	messageBuf.number = messageId;
	if (fo::func::message_search(file, &messageBuf) == 1) {
		return messageBuf.message;
	}
	return nullptr;
}

fo::MessageNode* GetMsgNode(fo::MessageList* msgList, int msgNum) {
	if (msgList != nullptr && msgList->numMsgs > 0) {
		fo::MessageNode *msgNode = msgList->nodes;
		long last = msgList->numMsgs - 1;
		long first = 0;
		long mid;

		// Binary Search to find msg
		while (first <= last) {
			mid = (first + last) / 2;
			if (msgNum > msgNode[mid].number)
				first = mid + 1;
			else if (msgNum < msgNode[mid].number)
				last = mid - 1;
			else
				return &msgNode[mid];
		}
	}
	return nullptr;
}

// Alternative version of getmsg_ function
char* GetMsg(fo::MessageList* msgList, int msgNum, int msgType) {
	fo::MessageNode *msgNode = GetMsgNode(msgList, msgNum);
	if (msgNode) {
		if (msgType == 2) {
			return msgNode->message;
		} else if (msgType == 1) {
			return msgNode->audio;
		}
	}
	return nullptr;
}

fo::Queue* QueueFind(fo::GameObject* object, long type) {
	if (fo::var::queue) {
		fo::Queue* queue = fo::var::queue;
		while (queue->object != object && queue->type != type)
		{
			queue = queue->next;
			if (!queue) break;
		}
		return queue;
	}
	return nullptr;
}

long AnimCodeByWeapon(fo::GameObject* weapon) {
	if (weapon != nullptr) {
		fo::Proto* proto;
		if (GetProto(weapon->protoId, &proto) && proto->item.type == fo::item_type_weapon) {
			return proto->item.weapon.animationCode;
		}
	}
	return 0;
}

bool CheckProtoID(unsigned long pid) {
	if (pid == 0) return false;
	long type = pid >> 24;
	if (type > fo::ObjType::OBJ_TYPE_MISC) return false;
	return (static_cast<long>(pid & 0xFFFF) < fo::var::protoLists[type].totalCount);
}

bool GetProto(long pid, fo::Proto** outProto) {
	return (fo::func::proto_ptr(pid, outProto) != -1);
}

bool CritterCopyProto(long pid, long* &proto_dst) {
	fo::Proto* proto;
	bool result = GetProto(pid, &proto);
	if (result) {
		proto_dst = reinterpret_cast<long*>(new int32_t[104]);
		std::memcpy(proto_dst, proto, 416);
	} else {
		proto_dst = nullptr;
	}
	return result;
}

void SkillGetTags(long* result, long num) {
	if (num > 4) num = 4;
	fo::func::skill_get_tags(result, num);
}

void SkillSetTags(long* tags, long num) {
	if (num > 4) num = 4;
	fo::func::skill_set_tags(tags, num);
}

long GetItemType(fo::GameObject* item) {
	return fo::util::GetProto(item->protoId)->item.type;
}

long GetCritterKillType(fo::GameObject* critter) {
	fo::Proto* proto = fo::util::GetProto(critter->protoId);
	return (proto) ? proto->critter.killType : -1;
}

_declspec(noinline) fo::GameObject* GetItemPtrSlot(fo::GameObject* critter, fo::InvenType slot) {
	fo::GameObject* itemPtr = nullptr;
	switch (slot) {
		case fo::InvenType::INVEN_TYPE_LEFT_HAND:
			itemPtr = fo::func::inven_left_hand(critter);
			break;
		case fo::InvenType::INVEN_TYPE_RIGHT_HAND:
			itemPtr = fo::func::inven_right_hand(critter);
			break;
		case fo::InvenType::INVEN_TYPE_WORN:
			itemPtr = fo::func::inven_worn(critter);
			break;
	}
	return itemPtr;
}

long& GetActiveItemMode() {
	return fo::var::itemButtonItems[fo::var::itemCurrentItem].mode;
}

fo::GameObject* GetActiveItem() {
	return fo::var::itemButtonItems[fo::var::itemCurrentItem].item;
}

fo::GameObject* GetInventItem(fo::GameObject* source, long pid) {
	for (long i = 0; i < source->invenSize; i++) {
		fo::GameObject* item = source->invenTable[i].object;
		if (item->protoId == pid) return item;
		item = GetInventItem(item, pid); // for containers
		if (item) return item;
	}
	return nullptr;
}

long GetCurrentAttackMode() {
	long hitMode = -1;
	if (fo::var::interfaceWindow != -1) {
		long activeHand = fo::var::itemCurrentItem; // 0 - left, 1 - right
		switch (fo::var::itemButtonItems[activeHand].mode) {
		case 1:
		case 2: // called shot
			hitMode = fo::var::itemButtonItems[activeHand].primaryAttack;
			break;
		case 3:
		case 4: // called shot
			hitMode = fo::var::itemButtonItems[activeHand].secondaryAttack;
			break;
		case 5: // reload mode
			hitMode = fo::AttackType::ATKTYPE_LWEAPON_RELOAD + activeHand;
		}
	}
	return hitMode;
}

fo::AttackSubType GetWeaponType(unsigned long weaponFlag) {
	static const fo::AttackSubType weapon_types[9] =
	{
		fo::AttackSubType::NONE,
		fo::AttackSubType::UNARMED,
		fo::AttackSubType::UNARMED,
		fo::AttackSubType::MELEE,
		fo::AttackSubType::MELEE,
		fo::AttackSubType::THROWING,
		fo::AttackSubType::GUNS,
		fo::AttackSubType::GUNS,
		fo::AttackSubType::GUNS
	};
	unsigned long type = weaponFlag & 0xF;
	return (type < 9) ? weapon_types[type] : fo::AttackSubType::NONE;
}

bool HeroIsFemale() {
	return (fo::func::stat_level(fo::var::obj_dude, fo::Stat::STAT_gender) == fo::Gender::GENDER_FEMALE);
}

long CheckAddictByPid(fo::GameObject* critter, long pid) {
	__asm {
		mov  eax, pid;
		mov  esi, critter;
		call fo::funcoffs::item_d_check_addict_;
	}
	/* keyword 'return' is not needed, the compiler will do everything correctly */
}

// Checks whether the player is under the influence of negative effects of radiation
long __fastcall IsRadInfluence() {
	fo::QueueRadiationData* queue = (fo::QueueRadiationData*)fo::func::queue_find_first(fo::var::obj_dude, fo::radiation_event);
	while (queue) {
		if (queue->init && queue->level >= 2) return 1;
		queue = (fo::QueueRadiationData*)fo::func::queue_find_next(fo::var::obj_dude, fo::radiation_event);
	}
	return 0;
}

bool IsNpcFlag(fo::GameObject* npc, long flag) {
	fo::Proto* proto;
	if (GetProto(npc->protoId, &proto)) {
		return (proto->critter.critterFlags & (1 << flag)) != 0;
	}
	return false;
}

void ToggleNpcFlag(fo::GameObject* npc, long flag, bool set) {
	fo::Proto* proto;
	if (GetProto(npc->protoId, &proto)) {
		long bit = (1 << flag);
		if (set) {
			proto->critter.critterFlags |= bit;
		} else {
			proto->critter.critterFlags &= ~bit;
		}
	}
}

// Returns the position of party member in the existing table (1 is added to the index position)
long IsPartyMemberByPid(long pid) {
	size_t patryCount = fo::var::partyMemberMaxCount;
	if (patryCount) {
		DWORD* memberPids = fo::var::partyMemberPidList; // pids from party.txt
		for (size_t i = 0; i < patryCount; i++) {
			if (memberPids[i] == pid) return i + 1;
		}
	}
	return 0;
}

// Returns True if the NPC belongs to the player's potential (set in party.txt) party members (analog of broken isPotentialPartyMember_)
bool IsPartyMember(fo::GameObject* critter) {
	if (critter->id < fo::PLAYER_ID) return false;
	return (IsPartyMemberByPid(critter->protoId) > 0);
}

// Returns the number of local variables of the object script
long GetScriptLocalVars(long sid) {
	fo::ScriptInstance* script = nullptr;
	fo::func::scr_ptr(sid, &script);
	return (script) ? script->numLocalVars : 0;
}

fo::GameObject* __fastcall LineOfSight(fo::GameObject* obj) {
	long objTile = obj->tile;
	fo::GameObject* object = fo::func::obj_blocking_at_wrapper(obj, objTile, obj->elevation, (void*)fo::funcoffs::obj_sight_blocking_at_);
	if (object) objTile = fo::func::tile_num_in_direction(objTile, fo::func::tile_dir(objTile, fo::var::obj_dude->tile), 1);
	fo::func::make_straight_path_func(fo::var::obj_dude, fo::var::obj_dude->tile, objTile, 0, (DWORD*)&object, 4, (void*)fo::funcoffs::obj_sight_blocking_at_);
	return object;
}

// Returns window by x/y coordinate (hidden windows are ignored)
fo::Window* __fastcall GetTopWindowAtPos(long xPos, long yPos, bool bypassTrans) {
	long num = fo::var::num_windows - 1;
	if (num) {
		int cflags = fo::WinFlags::Hidden;
		if (bypassTrans) cflags |= fo::WinFlags::Transparent;
		do {
			fo::Window* win = fo::var::window[num];
			if (xPos >= win->wRect.left && xPos <= win->wRect.right && yPos >= win->wRect.top && yPos <= win->wRect.bottom) {
				if (!(win->flags & cflags)) {
					return win;
				}
			}
		} while (--num);
	}
	return fo::var::window[0];
}

static long GetRangeTileNumbers(long sourceTile, long radius, long &outEnd) {
	long hexRadius = 200 * (radius + 1);

	outEnd = sourceTile + hexRadius;
	if (outEnd > 40000) outEnd = 40000;

	long startTile = sourceTile - hexRadius;
	return (startTile < 0) ? 0 : startTile;
}

// Returns an array of objects within the specified radius from the source tile
void GetObjectsTileRadius(std::vector<fo::GameObject*> &objs, long sourceTile, long radius, long elev, long type) {
	long endTile;
	for (long tile = GetRangeTileNumbers(sourceTile, radius, endTile); tile < endTile; tile++)
	{
		fo::GameObject* obj = fo::func::obj_find_first_at_tile(elev, tile);
		while (obj)
		{
			if (type == -1 || type == obj->Type()) {
				bool multiHex = (obj->flags & fo::ObjectFlag::MultiHex) ? true : false;
				if (fo::func::tile_dist(sourceTile, obj->tile) <= (radius + multiHex)) {
					objs.push_back(obj);
				}
			}
			obj = fo::func::obj_find_next_at_tile();
		}
	}
}

// typeMask: AABBCCDD
static bool TypeMask(char type, unsigned long mask) {
	return ((mask & 0xFF) == type || (mask & 0xFF00) >> 8 == type || (mask & 0xFF0000) >> 16 == type || (mask & 0xFF000000) >> 24 == type);
}

// Returns an associative array of objects within the specified radius from the source tile
// the key value is the distance of the object from the source tile
void GetObjectsTileRadius(std::multimap<long, fo::GameObject*> &objs, long sourceTile, long radius, long elev, unsigned long typeMask) {
	long endTile;
	for (long tile = GetRangeTileNumbers(sourceTile, radius, endTile); tile < endTile; tile++)
	{
		fo::GameObject* obj = fo::func::obj_find_first_at_tile(elev, tile);
		while (obj)
		{
			if (typeMask == -1 || TypeMask(obj->Type(), typeMask)) {
				unsigned char multiHex = (obj->flags & fo::ObjectFlag::MultiHex) ? 1 : 0;
				long dist = fo::func::tile_dist(sourceTile, obj->tile);
				if (dist <= (radius + multiHex)) {
					objs.emplace(dist, obj);
				}
			}
			obj = fo::func::obj_find_next_at_tile();
		}
	}
}

// Checks the blocking tiles and returns the first blocking object
fo::GameObject* CheckAroundBlockingTiles(fo::GameObject* source, long dstTile) {
	long rotation = 5;
	do {
		long chkTile = fo::func::tile_num_in_direction(dstTile, rotation, 1);
		fo::GameObject* obj = fo::func::obj_blocking_at(source, chkTile, source->elevation);
		if (obj) return obj;
	} while (--rotation >= 0);

	return nullptr;
}

fo::GameObject* __fastcall MultiHexMoveIsBlocking(fo::GameObject* source, long dstTile) {
	if (fo::func::tile_dist(source->tile, dstTile) > 1) {
		return CheckAroundBlockingTiles(source, dstTile);
	}
	// Checks the blocking arc of adjacent tiles
	long dir = fo::func::tile_dir(source->tile, dstTile);

	long chkTile = fo::func::tile_num_in_direction(dstTile, dir, 1);
	fo::GameObject* obj = fo::func::obj_blocking_at(source, chkTile, source->elevation);
	if (obj) return obj;

	// +1 direction
	long rotation = (dir + 1) % 6;
	chkTile = fo::func::tile_num_in_direction(dstTile, rotation, 1);
	obj = fo::func::obj_blocking_at(source, chkTile, source->elevation);
	if (obj) return obj;

	// -1 direction
	rotation = (dir + 5) % 6;
	chkTile = fo::func::tile_num_in_direction(dstTile, rotation, 1);
	obj = fo::func::obj_blocking_at(source, chkTile, source->elevation);
	if (obj) return obj;

	return nullptr;
}

// Returns the type of the terrain sub tile at the the player's position on the world map
long wmGetCurrentTerrainType() {
	long* terrainId = *(long**)FO_VAR_world_subtile;
	if (terrainId == nullptr) {
		__asm {
			lea  ebx, terrainId;
			mov  edx, dword ptr ds:[FO_VAR_world_ypos];
			mov  eax, dword ptr ds:[FO_VAR_world_xpos];
			call fo::funcoffs::wmFindCurSubTileFromPos_;
		}
	}
	return *terrainId;
}

//---------------------------------------------------------
// copy the area from the interface buffer to the data array
void SurfaceCopyToMem(long fromX, long fromY, long width, long height, long fromWidth, BYTE* fromSurface, BYTE* toMem) {
	fromSurface += fromY * fromWidth + fromX;
	for (long i = 0, h = 0; h < height; h++, i += width) {
		std::memcpy(&toMem[i], fromSurface, width);
		fromSurface += fromWidth;
	}
}

// safe copy data from memory to the area of the interface buffer
void DrawToSurface(long toX, long toY, long width, long height, long toWidth, long toHeight, BYTE* toSurface, BYTE* fromMem) {
	BYTE* _toSurface = toSurface + (toY * toWidth + toX);
	BYTE* endToSurf = (toWidth * toHeight) + toSurface;
	long i = 0;
	for (long h = 0; h < height; h++) {
		for (long w = 0; w < width; w++) {
			if (_toSurface + w > endToSurf) return;
			if (_toSurface >= toSurface) _toSurface[w] = fromMem[i++];
		}
		_toSurface += toWidth;
	}
}

// safe copy data from surface to surface with mask
void DrawToSurface(long width, long height, long fromX, long fromY, long fromWidth, BYTE* fromSurf,
                   long toX, long toY, long toWidth, long toHeight, BYTE* toSurf, int maskRef)
{
	BYTE* _fromSurf = fromSurf + (fromY * fromWidth + fromX);
	BYTE* _toSurf =  toSurf + (toY * toWidth + toX);
	BYTE* endToSurf = (toWidth * toHeight) + toSurf;

	for (long h = 0; h < height; h++) {
		for (long w = 0; w < width; w++) {
			if (_toSurf + w > endToSurf) return;
			if (_toSurf >= toSurf && _fromSurf[w] != maskRef) _toSurf[w] = _fromSurf[w];
		}
		_fromSurf += fromWidth;
		_toSurf += toWidth;
	}
}

// safe copy data from surface to surface
void DrawToSurface(long width, long height, long fromX, long fromY, long fromWidth, BYTE* fromSurf,
                   long toX, long toY, long toWidth, long toHeight, BYTE* toSurf)
{
	BYTE* _fromSurf = fromSurf + (fromY * fromWidth + fromX);
	BYTE* _toSurf = toSurf + (toY * toWidth + toX);
	BYTE* endToSurf = (toWidth * toHeight) + toSurf;

	for (long h = 0; h < height; h++) {
		for (long w = 0; w < width; w++) {
			if (_toSurf + w > endToSurf) return;
			if (_toSurf >= toSurf) _toSurf[w] = _fromSurf[w];
		}
		_fromSurf += fromWidth;
		_toSurf += toWidth;
	}
}

//void TranslucentDarkFill(BYTE* surface, long x, long y, long width, long height, long surfWidth) {
//	BYTE* surf = surface + (y * surfWidth) + x;
//	fo::func::wmInterfaceDrawSubTileRectFogged(surf, width, height, surfWidth);
//}

// Fills the specified interface window with index color
bool WinFillRect(long winID, long x, long y, long width, long height, BYTE indexColor) {
	fo::Window* win = fo::func::GNW_find(winID);
	bool result = false;
	if ((x + width) > win->width) {
		width = win->width - x;
		result = true;
	}
	if ((y + height) > win->height) {
		height = win->height - y;
		result = true;
	}
	BYTE* surf = win->surface + (win->width * y) + x;
	long pitch = win->width - width;
	while (height--) {
		long w = width;
		while (w--) *surf++ = indexColor;
		surf += pitch;
	};
	return result;
}

// Fills the specified interface window with index color 0 (black color)
void ClearWindow(long winID, bool refresh) {
	fo::Window* win = fo::func::GNW_find(winID);
	std::memset(win->surface, 0, win->width * win->height);
	if (refresh) {
		fo::func::GNW_win_refresh(win, &win->rect, nullptr);
	}
}

//---------------------------------------------------------
void PrintFloatText(fo::GameObject* object, const char* text, long colorText, long colorOutline, long font) {
	fo::BoundRect rect;
	if (!fo::func::text_object_create(object, text, font, colorText, colorOutline, &rect)) {
		fo::func::tile_refresh_rect(&rect, object->elevation);
	}
}

// print text to surface
void PrintText(char* displayText, BYTE colorIndex, DWORD xPos, DWORD yPos, DWORD txtWidth, DWORD toWidth, BYTE* toSurface) {
	DWORD posOffset = yPos * toWidth + xPos;
	__asm {
		xor  eax, eax;
		mov  al, colorIndex;
		mov  edx, displayText;
		push eax;
		mov  ebx, txtWidth;
		mov  eax, toSurface;
		mov  ecx, toWidth;
		add  eax, posOffset;
		call dword ptr ds:[FO_VAR_text_to_buf];
	}
}

void PrintTextFM(const char* displayText, BYTE colorIndex, DWORD xPos, DWORD yPos, DWORD txtWidth, DWORD toWidth, BYTE* toSurface) {
	DWORD posOffset = yPos * toWidth + xPos;
	__asm {
		xor  eax, eax;
		mov  al, colorIndex;
		mov  edx, displayText;
		push eax;
		mov  ebx, txtWidth;
		mov  eax, toSurface;
		mov  ecx, toWidth;
		add  eax, posOffset;
		call fo::funcoffs::FMtext_to_buf_;
	}
}

//---------------------------------------------------------
//gets the height of the currently selected font
DWORD GetTextHeight() {
//	DWORD TxtHeight;
	__asm {
		call dword ptr ds:[FO_VAR_text_height]; //get text height
//		mov  TxtHeight, eax;
	}
//	return TxtHeight;
}

//---------------------------------------------------------
//gets the length of a string using the currently selected font
DWORD GetTextWidth(const char* TextMsg) {
	__asm {
		mov  eax, TextMsg;
		call dword ptr ds:[FO_VAR_text_width]; //get text width
	}
}

DWORD GetTextWidthFM(const char* TextMsg) {
	return fo::func::FMtext_width(TextMsg); //get text width
}

//---------------------------------------------------------
//get width of Char for current font
DWORD Get_CharWidth(BYTE charVal) {
	__asm {
		mov  al, charVal;
		call dword ptr ds:[FO_VAR_text_char_width];
	}
}

DWORD GetCharWidthFM(BYTE charVal) {
	__asm {
		mov  al, charVal;
		call fo::funcoffs::FMtext_char_width_;
	}
}

//---------------------------------------------------------
//get maximum string length for current font - if all characters were maximum width
DWORD GetMaxTextWidth(const char* TextMsg) {
//	DWORD msgWidth;
	__asm {
		mov  eax, TextMsg;
		call dword ptr ds:[FO_VAR_text_mono_width];
//		mov  msgWidth, eax;
	}
//	return msgWidth;
}

//---------------------------------------------------------
//get number of pixels between characters for current font
DWORD GetCharGapWidth() {
//	DWORD gapWidth;
	__asm {
		call dword ptr ds:[FO_VAR_text_spacing];
//		mov  gapWidth, eax;
	}
//	return gapWidth;
}

//---------------------------------------------------------
//get maximum character width for current font
DWORD GetMaxCharWidth() {
//	DWORD charWidth = 0;
	__asm {
		call dword ptr ds:[FO_VAR_text_max];
//		mov  charWidth, eax;
	}
//	return charWidth;
}

void RedrawObject(fo::GameObject* obj) {
	fo::BoundRect rect;
	fo::func::obj_bound(obj, &rect);
	fo::func::tile_refresh_rect(&rect, obj->elevation);
}

// Redraws all windows
void RefreshGNW(bool skipOwner) {
	*(DWORD*)FO_VAR_doing_refresh_all = 1;
	for (size_t i = 0; i < fo::var::num_windows; i++) {
		if (skipOwner && fo::var::window[i]->flags & fo::WinFlags::OwnerFlag) continue;
		fo::func::GNW_win_refresh(fo::var::window[i], &fo::var::scr_size, 0);
	}
	*(DWORD*)FO_VAR_doing_refresh_all = 0;
}

/////////////////////////////////////////////////////////////////UNLISTED FRM FUNCTIONS//////////////////////////////////////////////////////////////

static bool LoadFrmHeader(fo::UnlistedFrm *frmHeader, fo::DbFile* frmStream) {
	if (fo::func::db_freadInt(frmStream, &frmHeader->version) == -1)
		return false;
	else if (fo::func::db_freadShort(frmStream, &frmHeader->FPS) == -1)
		return false;
	else if (fo::func::db_freadShort(frmStream, &frmHeader->actionFrame) == -1)
		return false;
	else if (fo::func::db_freadShort(frmStream, &frmHeader->numFrames) == -1)
		return false;
	else if (fo::func::db_freadShortCount(frmStream, frmHeader->xCentreShift, 6) == -1)
		return false;
	else if (fo::func::db_freadShortCount(frmStream, frmHeader->yCentreShift, 6) == -1)
		return false;
	else if (fo::func::db_freadIntCount(frmStream, frmHeader->oriOffset, 6) == -1)
		return false;
	else if (fo::func::db_freadInt(frmStream, &frmHeader->frameAreaSize) == -1)
		return false;

	return true;
}

static bool LoadFrmFrame(fo::UnlistedFrm::Frame *frame, fo::DbFile* frmStream) {
	//FRMframe *frameHeader = (FRMframe*)frameMEM;
	//BYTE* frameBuff = frame + sizeof(FRMframe);

	if (fo::func::db_freadShort(frmStream, &frame->width) == -1)
		return false;
	else if (fo::func::db_freadShort(frmStream, &frame->height) == -1)
		return false;
	else if (fo::func::db_freadInt(frmStream, &frame->size) == -1)
		return false;
	else if (fo::func::db_freadShort(frmStream, &frame->x) == -1)
		return false;
	else if (fo::func::db_freadShort(frmStream, &frame->y) == -1)
		return false;

	frame->indexBuff = new BYTE[frame->size];
	if (fo::func::db_fread(frame->indexBuff, 1, frame->size, frmStream) != frame->size) return false;

	return true;
}

fo::UnlistedFrm *LoadUnlistedFrm(char *frmName, unsigned int folderRef) {
	if (folderRef > fo::OBJ_TYPE_SKILLDEX) return nullptr;

	const char *artfolder = fo::var::art[folderRef].path; // address of art type name
	char frmPath[MAX_PATH];

	if (fo::var::use_language) {
		sprintf_s(frmPath, MAX_PATH, "art\\%s\\%s\\%s", (const char*)fo::var::language, artfolder, frmName);
	} else {
		sprintf_s(frmPath, MAX_PATH, "art\\%s\\%s", artfolder, frmName);
	}

	fo::UnlistedFrm *frm = new fo::UnlistedFrm;

	auto frmStream = fo::func::db_fopen(frmPath, "rb");

	if (!frmStream && fo::var::use_language) {
		sprintf_s(frmPath, MAX_PATH, "art\\%s\\%s", artfolder, frmName);
		frmStream = fo::func::db_fopen(frmPath, "rb");
	}

	if (frmStream != nullptr) {
		if (!LoadFrmHeader(frm, frmStream)) {
			fo::func::db_fclose(frmStream);
			delete frm;
			return nullptr;
		}

		DWORD oriOffset_1st = frm->oriOffset[0];
		DWORD oriOffset_new = 0;
		frm->frames = new fo::UnlistedFrm::Frame[6 * frm->numFrames];
		for (int ori = 0; ori < 6; ori++) {
			if (ori == 0 || frm->oriOffset[ori] != oriOffset_1st) {
				frm->oriOffset[ori] = oriOffset_new;
				for (int fNum = 0; fNum < frm->numFrames; fNum++) {
					if (!LoadFrmFrame(&frm->frames[oriOffset_new + fNum], frmStream)) {
						fo::func::db_fclose(frmStream);
						delete frm;
						return nullptr;
					}
				}
				oriOffset_new += frm->numFrames;
			} else {
				frm->oriOffset[ori] = 0;
			}
		}

		fo::func::db_fclose(frmStream);
	} else {
		delete frm;
		return nullptr;
	}
	return frm;
}

/* DONE
bool IsExistProto(long pid) {
	long type = pid >> 24;
	if (type > fo::ObjType::OBJ_TYPE_MISC) return false;

	ProtoListBlock* listBlock = fo::var::protoLists[type].block;
	while (listBlock)
	{
		for (long i = 0; i < listBlock->count; i++) {
			if (*listBlock->protoMem[i] == pid) return true;
		}
		listBlock = listBlock->next;
	}

	char pathProFile[MAX_PATH];
	fo::func::proto_make_path(pathProFile, pid);

	long endPos = std::strlen(pathProFile);
	char* d_pathProFile = &pathProFile[endPos + 1];
	char* s_pathProFile = &pathProFile[endPos - 2];

	while (*s_pathProFile != '\\') s_pathProFile--;
	while (*++s_pathProFile) *d_pathProFile++ = *s_pathProFile;

	pathProFile[endPos] = '\\';
	std::strcpy(d_pathProFile, ".lst");

	fo::DbFile* file = fo::func::db_fopen(pathProFile, "rt"); // open <type>.lst file
	if (!file) return false;

	char getLine[16];
	long i, pidLine = 1;
	for (i = pid & 0xFFFFFF; pidLine <= i; ++pidLine) {
		if (!fo::func::db_fgets(getLine, 16, file)) break;
	}
	if (pidLine - 1 != i) return false;

	char* _getLine = getLine;
	char chr = *getLine;
	while(chr) {
		if (chr == '\n'|| chr == ' ') {
			*_getLine = '\0';
			break;
		}
		chr = *++_getLine;
	}
	std::strcpy(&pathProFile[endPos + 1], getLine);
	return fo::func::db_access(pathProFile);
}
*/
}
}
