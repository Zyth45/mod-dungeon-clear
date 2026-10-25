/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _DC_RUN_TRAVEL_GUARD_H
#define _DC_RUN_TRAVEL_GUARD_H

#include <cstdint>

// Keeps an all-bot party's members inside the dungeon while their run is on.
//
// "The tank left the instance" ended 21 NUC runs on 25/10, none of them a
// defeat. 18 went to a battleground: the tank is the party's leader, its
// playerbots master (the harness GM) is cleared now and then, and in that
// window stock BGJoinAction::isUseful has nothing left to refuse on — the "do
// not try if in dungeon" check is commented out — so the tank queued the whole
// party (tr-20261025-144339-97: "queued AV", "joined BG - AV", party=21/22 on
// map 30; -152214-121 to WSG after 33 minutes and one boss). The other three
// were pulled out to a capital within a minute of the start by whatever else
// held the character (tr-20261025-181129-4: the CoA heal bench formed a team
// around the same tank in the same second).
//
// Nothing a bot does on its own should take it out of a run in progress, so
// the guard refuses, for a living bot of an all-bot party whose run is enabled
// on the map it stands on:
//   * joining a battleground or arena queue, alone or as the group;
//   * entering a battleground it was already invited to;
//   * any teleport to another map.
// Same-map teleports (stranded recovery, the wipe regroup at the entrance) and
// the dead (ghost release, corpse runs) pass. A human in the party passes
// everything: they summon and hearth whoever they like. The module's own
// cross-map moves (harness teardown, the fills' eviction) open an AllowScope.
namespace DcRunTravelGuard
{
    struct Facts
    {
        bool isBot = false;        // a PlayerbotAI that is not a self-bot
        bool partyAllBots = false; // no human-controlled member in its group
        bool alive = false;        // alive and not a released ghost
        bool runOnThisMap = false; // its party's run is enabled on the dungeon it stands in
    };

    constexpr bool Guards(Facts const& f)
    {
        return f.isBot && f.partyAllBots && f.alive && f.runOnThisMap;
    }

    constexpr bool VetoTeleport(Facts const& f, std::uint32_t fromMap, std::uint32_t toMap,
                                bool allowed)
    {
        return !allowed && fromMap != toMap && Guards(f);
    }

    constexpr bool VetoBattlegroundQueue(Facts const& f)
    {
        return Guards(f);
    }

    // BattleFieldPort action 1 is "enter"; 0 leaves the queue and must pass.
    constexpr bool VetoBattlefieldPort(Facts const& f, std::uint8_t action)
    {
        return action == 1 && Guards(f);
    }

    // Opened around a teleport the module issues on purpose. World-thread or
    // map-thread alike: the hook runs synchronously inside TeleportTo on the
    // caller's own thread.
    class AllowScope
    {
    public:
        AllowScope() { ++Depth(); }
        ~AllowScope() { --Depth(); }
        AllowScope(AllowScope const&) = delete;
        AllowScope& operator=(AllowScope const&) = delete;

        static bool Active() { return Depth() > 0; }

    private:
        static int& Depth()
        {
            thread_local int depth = 0;
            return depth;
        }
    };
}

#endif  // _DC_RUN_TRAVEL_GUARD_H
