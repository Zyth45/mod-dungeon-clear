/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCSELFFOUNDPLANNER_H
#define _PLAYERBOT_DCSELFFOUNDPLANNER_H

#include "Define.h"
#include "TestRun/DcTestRunRecord.h"

// Self-found groups (CoA 2.8.2). playerbots' AiPlayerbot.CoaSelfFoundBotsPercent
// picks a share of the random bots (by guid, whatever their level) that wear
// only what they loot. This planner gives them the dungeons of their level, at
// every level: every DungeonClear.SelfFound.IntervalS it forms ONE party of five
// of them and starts it as a hand-picked `.dc test` run (DcTestRunManager::
// StartRoster, planId "selffound") under the headless driver. No respec, no
// regear, no relevel, normal loot.
//
// Which dungeon: every classic 5-man of the registry (RFC, Wailing Caverns,
// Deadmines, SFK, Stockade, BFD, Gnomeregan, RFK, the SM wings, ... up to the
// level 60 ones) with the level range a player gets it at (13-18 for RFC, ...;
// SelfFound.LevelWindow levels lower still). The free candidates are counted per
// (dungeon, faction) and one of the possible pairs is drawn, weighted by how
// many bots it could take: the most populated levels get the most parties, and
// a low-level population (a realm restarted from level 1) gets RFC, WC and DM.
//
// Roles: a healer and a tank by spec are preferred. In dungeons up to
// SelfFound.RoleFallbackMaxLevel a missing one is a bot whose class has that
// role, moved to a specialization of it (CoaSwitchRole, as the addon recruit
// does, gear untouched); with no possible tank, the sturdiest member leads.
//
// Who is taken: online random bots that are self-found now (IsSelfFoundBot, the
// share; or any bot of a self-found realm, SelfFoundAt), alive, in the open
// world, in no group or invite, with no master, not past the Dungeon Finder
// queue, not offered to a player by the LFG chat, not held by a bench, not in
// another run, not cooling down. A bot queued for a battleground or the
// Dungeon Finder is taken out of its queues at launch. A player's character,
// alt or group member is never one of them. CoaSelfFoundBotsPercent of the
// online random bots is the part kept in dungeons (MaxGroups bounds it).
//
// Diagnostics: the config once, then per try the census filter by filter, the
// free bots by level and faction, the dungeons looked at and why nothing
// started (SelfFound.DiagWarn raises them to WARN).
//
// Weak parties: in dungeons starting below SelfFound.StrictMinBelowLevel no
// member is under the range's minimum, and the party is led by its
// highest-level tank. Every run gets per-run DC overrides (SelfFound.Cautious):
// small pulls (one elite at a time when the party's item level is under
// GearedIlvlPct % of its level), no pack pulled en route, a full rest whenever
// health or mana drops under the SelfFound.Rest* triggers; a full wipe is
// retried SelfFound.WipeRetries times (revived at the entrance). After a failed
// run its members wait SelfFound.WipeCooldownMin, and the same five are not
// sent together again before that long.
//
// Load: at most SelfFound.MaxGroups parties at once, the bots are already logged
// in, and TestRun.MaxConcurrent still caps every run. The members of a party are
// held (RandomBotLevelMgr::SetHeld) while it runs. The run record carries the
// item level each member wore going in and coming out (dc_testruns.jsonl,
// schema 15), and so do the SELFFOUND START / END log lines.
//
// World thread only.
namespace DcSelfFoundPlanner
{
    // The planId of the self-found runs (dc_testruns.jsonl, live file).
    inline constexpr char const* kPlanId = "selffound";

    void Tick(uint32 diff);

    // DcTestRunManager, as a run of planId kPlanId is removed: gives its
    // members back and logs SELFFOUND END with the gear before/after.
    void OnRunFinished(DcTestRunRecord::Record const& rec);
}

#endif  // _PLAYERBOT_DCSELFFOUNDPLANNER_H
