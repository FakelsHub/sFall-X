/*
 *    sfall
 *    Copyright (C) 2012  The sfall team
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

#include <unordered_map>

#include "..\main.h"
#include "..\FalloutEngine\Fallout2.h"
#include "LoadGameHook.h"

#include "..\Game\items.h"

#include "SubModules\AI.Behavior.h"
#include "SubModules\AI.FuncHelpers.h"

#include "AI.h"

namespace sfall
{

using namespace fo::Fields;

static std::unordered_map<fo::GameObject*, fo::GameObject*> targets;
static std::unordered_map<fo::GameObject*, fo::GameObject*> sources;

// Returns the friendly critter or any blocking object that located on the line of fire
fo::GameObject* AI::CheckShootAndTeamCritterOnLineOfFire(fo::GameObject* object, long targetTile, long team) {
	if (object && object->IsCritter() && object->critter.teamNum != team) { // is not friendly fire
		long objTile = object->tile;
		if (objTile == targetTile) return nullptr;

		if (object->flags & fo::ObjectFlag::MultiHex) {
			long dir = fo::func::tile_dir(objTile, targetTile);
			objTile = fo::func::tile_num_in_direction(objTile, dir, 1);
			if (objTile == targetTile) return nullptr; // just in case
		}
		// continue checking the line of fire from object tile to targetTile
		fo::GameObject* obj = object; // for ignoring the object (multihex) when building the path
		fo::func::make_straight_path_func(object, objTile, targetTile, 0, (DWORD*)&obj, 32, (void*)fo::funcoffs::obj_shoot_blocking_at_);
		if (!CheckShootAndTeamCritterOnLineOfFire(obj, targetTile, team)) return nullptr;
	}
	return object;
}

// Returns the friendly critter that located on the line of fire
fo::GameObject* AI::CheckFriendlyFire(fo::GameObject* target, fo::GameObject* attacker) {
	fo::GameObject* object = nullptr;
	fo::func::make_straight_path_func(attacker, attacker->tile, target->tile, 0, (DWORD*)&object, 32, (void*)fo::funcoffs::obj_shoot_blocking_at_);
	object = CheckShootAndTeamCritterOnLineOfFire(object, target->tile, attacker->critter.teamNum);
	return (object && object->IsCritter()) ? object : nullptr; // 0 if there are no friendly critters
}

/////////////////////////////////////////////////////////////////////////////////////////

static void __declspec(naked) ai_try_attack_hook_FleeFix() {
	using namespace fo;
	__asm {
		or   byte ptr [esi + combatState], ReTarget; // set CombatStateFlag flag
		jmp  fo::funcoffs::ai_run_away_;
	}
}

static void __declspec(naked) combat_ai_hook_FleeFix() {
	static const DWORD combat_ai_hook_flee_Ret = 0x42B206;
	using namespace fo;
	__asm {
		test byte ptr [ebp], ReTarget; // CombatStateFlag flag (critter.combat_state)
		jnz  reTarget;
		jmp  fo::funcoffs::critter_name_;
reTarget:
		and  byte ptr [ebp], ~(InFlee | ReTarget); // unset CombatStateFlag flags
		xor  edi, edi;
		mov  dword ptr [esi + whoHitMe], edi;
		add  esp, 4;
		jmp  combat_ai_hook_flee_Ret;
	}
}

static void __declspec(naked) ai_try_attack_hook_runFix() {
	__asm {
		mov  ecx, [esi + combatState]; // save combat flags before ai_run_away
		call fo::funcoffs::ai_run_away_;
		mov  [esi + combatState], ecx; // restore state flags
		retn;
	}
}

static bool npcPercentMinHP = false;

static void __declspec(naked) combat_ai_hack() {
	static const DWORD combat_ai_hack_Ret = 0x42B204;
	__asm {
		mov  edx, [ebx + 0x10];             // cap.min_hp
		test npcPercentMinHP, 0xFF;
		jz   skip;
		cmp  dword ptr [ebx + 0x98], -1;    // cap.run_away_mode (none)
		je   skip;
		push eax;                           // current hp
		mov  eax, esi;
		call fo::funcoffs::isPartyMember_;  // TODO replace to sfall IsPartyMember
		test eax, eax;
		pop  eax;
		cmovz edx, [esp + 0x1C - 0x18 + 4]; // calculated min_hp (percent)
		mov  [ebx + 0x10], edx;             // update to cap.min_hp
skip:
		cmp  eax, edx;
		jl   tryHeal; // curr_hp < min_hp
end:
		add  esp, 4;
		jmp  combat_ai_hack_Ret;
tryHeal:
		mov  eax, esi;
		call fo::funcoffs::ai_check_drugs_;
		cmp  [esi + health], edx; // edx - minimum hp, below which NPC will run away
		jge  end;
		retn; // flee
	}
}

static void __declspec(naked) ai_check_drugs_hook() {
	__asm {
		call fo::funcoffs::stat_level_;              // current hp
		mov  edx, dword ptr [esp + 0x34 - 0x1C + 4]; // ai cap
		mov  edx, [edx + 0x10];                      // min_hp
		cmp  eax, edx;                               // curr_hp < cap.min_hp
		cmovl edi, edx;
		retn;
	}
}

/////////////////////////////////////////////////////////////////////////////////////////

static bool __fastcall TargetExistInList(fo::GameObject* target, fo::GameObject** targetList) {
	char i = 4;
	do {
		if (*targetList == target) return true;
		targetList++;
	} while (--i);
	return false;
}

static void __declspec(naked) ai_find_attackers_hack_target2() {
	__asm {
		mov  edi, [esp + 0x24 - 0x24 + 4] // critter (target)
		pushadc;
		lea  edx, [ebp - 4]; // start list of targets
		mov  ecx, edi;
		call TargetExistInList;
		test al, al;
		popadc;
		jnz  skip;
		inc  edx;
		mov  [ebp], edi;
skip:
		retn;
	}
}

static void __declspec(naked) ai_find_attackers_hack_target3() {
	__asm {
		mov  edi, [esp + 0x24 - 0x20 + 4] // critter (target)
		push eax;
		push edx;
		mov  eax, 4; // count targets
		lea  edx, [ebp - 4 * 2]; // start list of targets
continue:
		cmp  edi, [edx];
		je   break;          // target == targetList
		lea  edx, [edx + 4]; // next target in list
		dec  al;
		jnz  continue;
break:
		test al, al;
		pop  edx;
		pop  eax;
		jz   skip;
		xor  edi, edi;
		retn;
skip:
		inc  edx;
		retn;
	}
}

static void __declspec(naked) ai_find_attackers_hack_target4() {
	__asm {
		mov  eax, [ecx + eax]; // critter (target)
		pushadc;
		lea  edx, [esi - 4 * 3]; // start list of targets
		mov  ecx, eax;
		call TargetExistInList;
		test al, al;
		popadc;
		jnz  skip;
		inc  edx;
		mov  [esi], eax;
skip:
		retn;
	}
}

/////////////////////////////////////////////////////////////////////////////////////////

static void __declspec(naked) ai_danger_source_hack_pm_newFind() {
	using namespace fo;
	__asm {
		mov  ecx, [ebp + 0x18]; // source combat_data.who_hit_me
		test ecx, ecx;
		jnz  hasTarget;
		retn;
hasTarget:
		test [ecx + damageFlags], DAM_DEAD;
		jz   isNotDead;
		xor  ecx, ecx;
isNotDead:
		mov  dword ptr [ebp + 0x18], 0; // combat_data.who_hit_me (engine code)
		retn;
	}
}

static long RetryCombatMinAP;

static void __declspec(naked) RetryCombatHook() {
	static DWORD RetryCombatLastAP = 0;
	__asm {
		mov  RetryCombatLastAP, 0;
retry:
		call fo::funcoffs::combat_ai_;
process:
		cmp  dword ptr ds:[FO_VAR_combat_turn_running], 0;
		jle  next;
		call fo::funcoffs::process_bk_;
		jmp  process;
next:
		mov  eax, [esi + movePoints];
		cmp  eax, RetryCombatMinAP;
		jl   end;
		cmp  eax, RetryCombatLastAP;
		je   end;
		mov  RetryCombatLastAP, eax;
		mov  eax, esi;
		xor  edx, edx;
		jmp  retry;
end:
		retn;
	}
}

static long __fastcall ai_weapon_reload_fix(fo::GameObject* weapon, fo::GameObject* ammo, fo::GameObject* critter) {
	fo::Proto* proto = nullptr;
	long result = -1;
	long maxAmmo;

	fo::GameObject* _ammo = ammo;

	while (ammo)
	{
		result = fo::func::item_w_reload(weapon, ammo);
		if (result != 0) return result; // 1 - reload done, -1 - can't reload

		if (!proto) {
			proto = fo::GetProto(weapon->protoId);
			maxAmmo = proto->item.weapon.maxAmmo;
		}
		if (weapon->item.charges >= maxAmmo) break; // magazine is full

		long pidAmmo = ammo->protoId;
		fo::func::obj_destroy(ammo);
		ammo = nullptr;

		DWORD currentSlot = -1; // begin find at first slot
		while (fo::GameObject* ammoFind = fo::func::inven_find_type(critter, fo::item_type_ammo, &currentSlot))
		{
			if (ammoFind->protoId == pidAmmo) {
				ammo = ammoFind;
				break;
			}
		}
	}
	if (_ammo != ammo) {
		fo::func::obj_destroy(ammo);
		return 1; // notifies the engine that the ammo has already been destroyed
	}
	return result;
}

static void __declspec(naked) item_w_reload_hook() {
	using namespace fo;
	__asm {
		cmp  dword ptr [eax + protoId], PID_SOLAR_SCORCHER;
		je   skip;
		push ecx;
		push esi;      // source
		mov  ecx, eax; // weapon
		call ai_weapon_reload_fix; // edx - ammo
		pop  ecx;
		retn;
skip:
		jmp fo::funcoffs::item_w_reload_;
	}
}

static long __fastcall CheckWeaponRangeAndApCost(fo::GameObject* source, fo::GameObject* target) {
	long weaponRange = fo::func::item_w_range(source, fo::ATKTYPE_RWEAPON_SECONDARY);
	long targetDist  = fo::func::obj_dist(source, target);
	if (targetDist > weaponRange) return 0; // don't use secondary mode

	return (source->critter.movePoints >= game::Items::item_w_mp_cost(source, fo::ATKTYPE_RWEAPON_SECONDARY, 0)); // 1 - allow secondary mode
}

static void __declspec(naked) ai_pick_hit_mode_hook() {
	__asm {
		call fo::funcoffs::caiHasWeapPrefType_;
		test eax, eax;
		jnz  evaluation;
		retn;
evaluation:
		mov  edx, edi;
		mov  ecx, esi;
		jmp  CheckWeaponRangeAndApCost;
	}
}

static void __declspec(naked) cai_perform_distance_prefs_hack() {
	using namespace fo;
	__asm {
		mov  ebx, eax; // current distance to target
		mov  ecx, esi;
		push 0;        // no called shot
		mov  edx, ATKTYPE_RWEAPON_PRIMARY;
		call game::Items::item_w_mp_cost;
		mov  edx, [esi + movePoints];
		sub  edx, eax; // ap - cost = free AP's
		jle  moveAway; // <= 0
		lea  edx, [edx + ebx - 1];
		cmp  edx, 5;   // minimum threshold distance
		jge  skipMove; // distance >= 5?
		// check combat rating
		mov  eax, esi;
		call fo::funcoffs::combatai_rating_;
		mov  edx, eax; // source rating
		mov  eax, edi;
		call fo::funcoffs::combatai_rating_;
		cmp  eax, edx; // target vs source rating
		jl   skipMove; // target rating is low
moveAway:
		mov  ebx, 10;  // move away max distance
		retn;
skipMove:
		xor  ebx, ebx; // skip moving away at the beginning of the turn
		retn;
	}
}

static void __declspec(naked) ai_move_away_hook() {
	static const DWORD ai_move_away_hook_Ret = 0x4289DA;
	__asm {
		test ebx, ebx;
		jl   fix; // distance arg < 0
		jmp  fo::funcoffs::ai_cap_;
fix:
		neg  ebx;
		mov  eax, [esi + movePoints]; // Current Action Points
		cmp  ebx, eax;
		cmovg ebx, eax; // if (distance > ap) dist = ap
		add  esp, 4;
		jmp  ai_move_away_hook_Ret;
	}
}

/////////////////////////////////////////////////////////////////////////////////////////

static long hitChance, loopCounter = 0;
static long checkBurstFriendlyFireMode;

static long __fastcall RollFriendlyFire(fo::GameObject* target, fo::GameObject* attacker) {
	if (AI::CheckFriendlyFire(target, attacker)) {
		if (checkBurstFriendlyFireMode > 0) return 1;
		long dice = fo::func::roll_random(1, 10);
		return (fo::func::stat_level(attacker, fo::STAT_iq) >= dice); // 1 - is friendly
	}
	return 0;
}

static void __declspec(naked) combat_safety_invalidate_weapon_func_hook_init() {
	using namespace fo;
	__asm {
		xor  ecx, ecx;
		cmp  edi, ANIM_fire_burst;
		setne cl; // set to 1 to skip check attempts for weapon ANIM_fire_continuous animation
		mov  loopCounter, ecx;
		mov  ecx, ebp;
		jmp  fo::funcoffs::combat_ctd_init_;
	}
}

static void __declspec(naked) combat_safety_invalidate_weapon_func_hook_check() {
	static const DWORD safety_invalidate_weapon_burst_friendly = 0x4216C9;
	__asm {
		pushadc;
		mov  ecx, esi; // target
		call RollFriendlyFire;
		test eax, eax;
		jnz  friendly;
		popadc;
		cmp  checkBurstFriendlyFireMode, 3;
		je   combat_safety_invalidate_weapon_func_hook_init;
		jmp  fo::funcoffs::combat_ctd_init_;
friendly:
		lea  esp, [esp + 8 + 3*4];
		jmp  safety_invalidate_weapon_burst_friendly; // "Friendly was in the way!"
	}
}

static void __declspec(naked) combat_safety_invalidate_weapon_func_hook() {
	__asm {
		mov  hitChance, edx; // keep chance_to_hit
		jmp  fo::funcoffs::compute_spray_;
	}
}

static DWORD safety_invalidate_weapon_burst_loop = 0x42168B;
static DWORD safety_invalidate_weapon_burst_exit = 0x4217AB;

static void __declspec(naked) combat_safety_invalidate_weapon_func_hack1() { // jump
	using namespace fo;
	__asm {
		dec  loopCounter;
		jz   break;
		jl   startLoop;
		mov  eax, hitChance;            // set hit chance for compute_spray_
		jmp  safety_invalidate_weapon_burst_loop;
startLoop:
		mov  eax, [esp + 0xF4 - 0x10];  // attacker
		mov  edx, STAT_iq;
		call fo::funcoffs::stat_level_; // number of checks depends on the attacker's IQ
		shl  eax, 4;                    // multiply by 16 (max 160 check attempts)
		mov  loopCounter, eax;
		mov  eax, hitChance;            // set hit chance for compute_spray_
		jmp  safety_invalidate_weapon_burst_loop;
break:
		jmp  safety_invalidate_weapon_burst_exit;
	}
}

static void __declspec(naked) combat_safety_invalidate_weapon_func_hack2() { // call
	using namespace fo;
	__asm {
		add  ecx, 4;
		cmp  edi, ebp;
		//////////////
		jge  checkLoop; // all targets are checked
break:
		retn; // engine jl
checkLoop:
		dec  loopCounter;
		jz   break;
		lea  esp, [esp + 4];            // destroy ret addr
		jl   startLoop;
		mov  eax, hitChance;            // set hit chance for compute_spray_
		jmp  safety_invalidate_weapon_burst_loop;
startLoop:
		mov  eax, [esp + 0xF4  - 0x10]; // attacker
		mov  edx, STAT_iq;
		call fo::funcoffs::stat_level_; // number of checks depends on the attacker's IQ
		shl  eax, 4;                    // multiply by 16 (max 160 check attempts)
		mov  loopCounter, eax;
		mov  eax, hitChance;            // set hit chance for compute_spray_
		jmp  safety_invalidate_weapon_burst_loop;
	}
}

/////////////////////////////////////////////////////////////////////////////////////////

static void __fastcall CombatAttackHook(fo::GameObject* source, fo::GameObject* target) {
	sources[target] = source; // who attacked the 'target' from the last time
	targets[source] = target; // who was attacked by the 'source' from the last time
}

static void __declspec(naked) combat_attack_hook() {
	__asm {
		push eax;
		push ecx;
		push edx;
		mov  ecx, eax;         // source
		call CombatAttackHook; // edx - target
		pop  edx;
		pop  ecx;
		pop  eax;
		jmp  fo::funcoffs::combat_attack_;
	}
}

static void AICombatClear() {
	targets.clear();
	sources.clear();
}

void AI::init() {

	AIBehavior::init();

	HookCalls(combat_attack_hook, {
		0x426A95, // combat_attack_this_
		0x42A796  // ai_attack_
	});
	LoadGameHook::OnCombatStart() += AICombatClear;
	LoadGameHook::OnCombatEnd() += AICombatClear;

	if (GetConfigInt("CombatAI", "SmartBehavior", 0) == 0) {
		RetryCombatMinAP = GetConfigInt("CombatAI", "NPCsTryToSpendExtraAP", -1);
		if (RetryCombatMinAP == -1) RetryCombatMinAP = GetConfigInt("Misc", "NPCsTryToSpendExtraAP", 0); // compatibility
		if (RetryCombatMinAP > 0) {
			dlog("Applying retry combat patch.", DL_INIT);
			HookCall(0x422B94, RetryCombatHook); // combat_turn_
			dlogr(" Done", DL_INIT);
		}

		// TODO move hack to AIBehavior
		// Enables the use of the RunAwayMode value from the AI-packet for the NPC
		// the min_hp value will be calculated as a percentage of the maximum number of NPC health points, instead of using fixed min_hp values
		//npcPercentMinHP = (GetConfigInt("CombatAI", "NPCRunAwayMode", 0) > 0);
	}

	#ifndef NDEBUG
	if (GetConfigInt("Debugging", "AIBugFixes", 1) == 0) return;
	#endif

	// Fix for NPCs not fully reloading a weapon if it has more ammo capacity than a box of ammo
	HookCalls(item_w_reload_hook, {
		0x42AF15,           // cai_attempt_w_reload_
		0x42A970, 0x42AA56, // ai_try_attack_
	});

	// Adds a check for the weapon range and the AP cost when AI is choosing weapon attack modes
	HookCall(0x429F6D, ai_pick_hit_mode_hook);

	/////////////////////// Combat AI behavior fixes ///////////////////////

	// Fix to reduce friendly fire in burst attacks
	// Modification function of safe use of weapon when the AI uses burst shooting mode
	switch (checkBurstFriendlyFireMode = GetConfigInt("CombatAI", "CheckBurstFriendlyFire", 0)) { // -1 disable fix
	case 3: // both 1 and 2 mode
	case 0: // adds a check/roll for friendly critters in the line of fire when AI uses burst attacks
	case 1: // always prevent a burst shot if there is a friendly NPC on the line of fire
		HookCall(0x421666, combat_safety_invalidate_weapon_func_hook_check);
		if (checkBurstFriendlyFireMode <= 1) break;
	case 2: // adds additional evaluation checks
		if (checkBurstFriendlyFireMode == 2) HookCall(0x421666, combat_safety_invalidate_weapon_func_hook_init);
		HookCall(0X4216A0, combat_safety_invalidate_weapon_func_hook);
		HookCall(0x4216F7, combat_safety_invalidate_weapon_func_hack1); // jle combat_safety_invalidate_weapon_func_hack1
		MakeCall(0x4217A0, combat_safety_invalidate_weapon_func_hack2);
	}

	// Fix for duplicate critters being added to the list of potential targets for AI
	MakeCall(0x428E75, ai_find_attackers_hack_target2, 2);
	MakeCall(0x428EB5, ai_find_attackers_hack_target3);
	MakeCall(0x428EE5, ai_find_attackers_hack_target4, 1);

	// Tweak for finding new targets for party members
	// Save the current target in the "target1" variable and find other potential targets
	MakeCall(0x429074, ai_danger_source_hack_pm_newFind); // (implemented in AIDangerSource_Extended)
	SafeWrite16(0x429074 + 5, 0x47EB); // jmp 0x4290C2

	// Fix to allow fleeing NPC to use drugs
	MakeCall(0x42B1DC, combat_ai_hack);
	// Fix for AI not checking minimum hp properly for using stimpaks (prevents premature fleeing)
	HookCall(0x428579, ai_check_drugs_hook);

	// Fix for NPC stuck in fleeing mode when the hit chance of a target was too low
	HookCall(0x42B1E3, combat_ai_hook_FleeFix);
	HookCalls(ai_try_attack_hook_FleeFix, { 0x42ABA8, 0x42ACE5 });

	// Restore combat flags after fleeing when NPC cannot move closer to target
	HookCall(0x42ADF6, ai_try_attack_hook_runFix);

	// Fix AI behavior for "Snipe" distance preference
	// The attacker will try to shoot the target instead of always running away from it at the beginning of the turn
	MakeCall(0x42B086, cai_perform_distance_prefs_hack);

	// Fix for ai_move_away_ engine function not working correctly in cases when needing to move a distance away from the target
	// now the function also takes the distance argument in a negative value for moving away at a distance
	HookCall(0x4289A7, ai_move_away_hook);
	// also patch combat_safety_invalidate_weapon_func_ for returning out_range argument in a negative value
	SafeWrite8(0x421628, 0xD0);    // sub edx, eax > sub eax, edx
	SafeWrite16(0x42162A, 0xFF40); // lea eax, [edx+1] > lea eax, [eax-1]
}

fo::GameObject* __stdcall AI::AIGetLastAttacker(fo::GameObject* target) {
	const auto itr = sources.find(target);
	return (itr != sources.end()) ? itr->second : 0;
}

fo::GameObject* __stdcall AI::AIGetLastTarget(fo::GameObject* source) {
	const auto itr = targets.find(source);
	return (itr != targets.end()) ? itr->second : 0;
}

}
