/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCSELFFOUNDPLANNER_H
#define _PLAYERBOT_DCSELFFOUNDPLANNER_H

#include "Define.h"

// Self-found groups (CoA 2.8.2). playerbots' AiPlayerbot.CoaSelfFoundBotsPercent
// makes a share of the random bots self-found: from level 10 they keep the gear
// they loot, nothing re-gears them. This planner gives them dungeons to loot:
// every DungeonClear.SelfFound.IntervalS it forms ONE party of five such bots
// (a tank, a healer, three dps by their specs, one faction, all within
// SelfFound.LevelWindow levels of the dungeon's recommended level) and starts it
// as a hand-picked `.dc test` run (DcTestRunManager::StartRoster, planId
// "selffound") under the headless driver. No respec, no regear, no relevel:
// the run is the same one `.dc test start <d> party=...` makes, normal loot
// included. At the end the harness revives the party, sends it to its bind
// point and logs it out; the random-bot rotation logs it in again later.
//
// Who is taken: online random bots of the share (PlayerbotFactory::
// IsSelfFoundBot), alive, in the open world, in no group, invite, queue,
// battleground or flight, with no master, not offered to a player by the LFG
// chat, not held by a bench and not in another run. A player's character, alt
// or group member is never one of them.
//
// Load: at most SelfFound.MaxGroups parties at once (default 2), the bots are
// already logged in, and TestRun.MaxConcurrent still caps every run. The bots of
// a party are held (RandomBotLevelMgr::SetHeld) while it runs, so neither the
// level brackets nor the orphan-group sweep of playerbots take them out of it.
//
// World thread only.
namespace DcSelfFoundPlanner
{
    void Tick(uint32 diff);
}

#endif  // _PLAYERBOT_DCSELFFOUNDPLANNER_H
