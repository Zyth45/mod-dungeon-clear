/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"

#include "DcRunTravelGuard.h"

namespace
{
    DcRunTravelGuard::Facts InRun()
    {
        DcRunTravelGuard::Facts f;
        f.isBot = true;
        f.partyAllBots = true;
        f.alive = true;
        f.runOnThisMap = true;
        return f;
    }
}

// tr-20261025-144339-97: the Stratholme tank queued the party for Alterac Valley.
TEST(DcRunTravelGuard, BotInARunCannotQueueOrEnterABattleground)
{
    EXPECT_TRUE(DcRunTravelGuard::VetoBattlegroundQueue(InRun()));
    EXPECT_TRUE(DcRunTravelGuard::VetoBattlefieldPort(InRun(), 1)) << "enter";
    EXPECT_FALSE(DcRunTravelGuard::VetoBattlefieldPort(InRun(), 0)) << "leaving the queue stays possible";
}

// tr-20261025-181129-4: the Wailing Caverns tank landed in Orgrimmar 16s in.
TEST(DcRunTravelGuard, BotInARunCannotLeaveTheMap)
{
    EXPECT_TRUE(DcRunTravelGuard::VetoTeleport(InRun(), 43, 1, false));
    EXPECT_FALSE(DcRunTravelGuard::VetoTeleport(InRun(), 43, 43, false)) << "same-map moves are the run's own";
    EXPECT_FALSE(DcRunTravelGuard::VetoTeleport(InRun(), 43, 1, true)) << "module teleport in an AllowScope";
}

TEST(DcRunTravelGuard, OutsideTheGuardEverythingPasses)
{
    DcRunTravelGuard::Facts f = InRun();
    f.alive = false;
    EXPECT_FALSE(DcRunTravelGuard::VetoTeleport(f, 43, 1, false)) << "a ghost releases to its graveyard";

    f = InRun();
    f.partyAllBots = false;
    EXPECT_FALSE(DcRunTravelGuard::VetoTeleport(f, 43, 1, false)) << "a human in the party decides";
    EXPECT_FALSE(DcRunTravelGuard::VetoBattlegroundQueue(f));

    f = InRun();
    f.runOnThisMap = false;
    EXPECT_FALSE(DcRunTravelGuard::VetoTeleport(f, 43, 1, false)) << "no run on, or a run elsewhere";

    f = InRun();
    f.isBot = false;
    EXPECT_FALSE(DcRunTravelGuard::VetoBattlegroundQueue(f));
}

TEST(DcRunTravelGuard, AllowScopeNests)
{
    EXPECT_FALSE(DcRunTravelGuard::AllowScope::Active());
    {
        DcRunTravelGuard::AllowScope const outer;
        {
            DcRunTravelGuard::AllowScope const inner;
            EXPECT_TRUE(DcRunTravelGuard::AllowScope::Active());
        }
        EXPECT_TRUE(DcRunTravelGuard::AllowScope::Active());
    }
    EXPECT_FALSE(DcRunTravelGuard::AllowScope::Active());
}
