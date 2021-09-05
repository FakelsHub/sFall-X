/*
 *    sfall
 *    Copyright (C) 2008 - 2021 Timeslip and sfall team
 *
 */

#pragma once

namespace game
{
namespace ai
{

class AIHelpers {
public:

	// Returns the friendly critter or any blocking object in the line of fire
	static fo::GameObject* CheckShootAndTeamCritterOnLineOfFire(fo::GameObject* object, long targetTile, long team);

	// Returns the friendly critter in the line of fire
	static fo::GameObject* CheckFriendlyFire(fo::GameObject* target, fo::GameObject* attacker);

	// Return of the friendly critter that are located on the line of fire or any other non-shooting object
	// destTile - the tile from which the line will be checked
	static fo::GameObject* CheckFriendlyFire(fo::GameObject* target, fo::GameObject* attacker, long destTile);

	static bool AttackInRange(fo::GameObject* source, fo::GameObject* weapon, long distance);
	static bool AttackInRange(fo::GameObject* source, fo::GameObject* weapon, fo::GameObject* target);
};

}
}