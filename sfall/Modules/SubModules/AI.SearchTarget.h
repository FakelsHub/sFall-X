/*
 *    sfall
 *    Copyright (C) 2021  The sfall team
 *
 */

#pragma once

namespace game
{
namespace imp_ai
{

class AISearchTarget {
public:
	static void init(bool);

	static fo::GameObject* __fastcall AIDangerSource_Extended(fo::GameObject* source, long type);
	static fo::GameObject* AIDangerSource(fo::GameObject* source, long type);

	static fo::GameObject* __fastcall RevertTarget(fo::GameObject* source, fo::GameObject* target);
};

}
}
