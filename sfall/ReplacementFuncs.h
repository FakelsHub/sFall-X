/*
 *    sfall
 *    Copyright (C) 2008 - 2021 Timeslip
 *
 */

#pragma once

#include "Game\GUI\render.h"
#include "Game\GUI\Text.h"

#include "Game\combatAI.h"
#include "Game\inventory.h"
#include "Game\skills.h"
#include "Game\stats.h"
#include "Game\items.h"
#include "Game\tilemap.h"

__inline void InitReplacementHack() {
	game::gui::Render::init();
	game::gui::Text::init();

	game::CombatAI::init();
	game::Inventory::init();
	game::Skills::init();
	game::Stats::init();
	game::Items::init();
	game::Tilemap::init();
}