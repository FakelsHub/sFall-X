/*
 *    sfall
 *    Copyright (C) 2008 - 2021  The sfall team
 *
 */

#pragma once

namespace sfall
{

class ObjectName
{
public:
	static void init();

	static const char* __stdcall GetName(fo::GameObject* object);
	static void SetName(long sid, const char* name);
};

}
