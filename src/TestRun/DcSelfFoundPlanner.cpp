/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "TestRun/DcSelfFoundPlanner.h"

#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include "CoaSpecialization.h"
#include "Common.h"
#include "LFGMgr.h"
#include "Log.h"
#include "Map.h"
#include "ObjectGuid.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotFactory.h"
#include "Playerbots.h"
#include "Random.h"
#include "RandomBotLevelMgr.h"
#include "RandomPlayerbotMgr.h"

#include "Ai/Dungeon/DungeonClear/Settings/DcSettings.h"
#include "TestRun/DcTestDriver.h"
#include "TestRun/DcTestDungeonRegistry.h"
#include "TestRun/DcTestRunManager.h"

namespace
{
    // The planId the self-found runs carry in dc_testruns.jsonl and the live
    // file; no plan of the scheduler has it, so OnRunFinished ignores them.
    char const* const kPlanId = "selffound";

    // How soon to look again when the headless driver is still logging in.
    constexpr uint32 kDriverRetryMs = 15000;

    struct Party
    {
        std::string runId;
        std::vector<ObjectGuid> members;
    };

    std::vector<Party> _parties;  // world thread only
    uint32 _accumMs = 0;

    struct Candidate
    {
        Player* bot;
        uint32 level;
        TeamId team;
    };

    template <typename T>
    void Shuffle(std::vector<T>& v)
    {
        for (std::size_t i = v.size(); i > 1; --i)
            std::swap(v[i - 1], v[urand(0, uint32(i - 1))]);
    }

    bool Near(uint32 level, uint32 recommended, uint32 window)
    {
        return uint32(std::abs(int32(level) - int32(recommended))) <= window;
    }

    // A self-found random bot that may leave what it is doing for a dungeon:
    // never one a player has a hand on (group, invite, master, LFG offer).
    bool Free(Player* bot)
    {
        if (!bot || !bot->IsInWorld() || bot->IsBeingTeleported() || !bot->IsAlive() || bot->IsInCombat() ||
            bot->GetGroup() || bot->GetGroupInvite() || bot->InBattleground() || bot->InBattlegroundQueue() ||
            bot->IsInFlight() || !bot->GetMap() || bot->GetMap()->Instanceable())
            return false;

        ObjectGuid const guid = bot->GetGUID();
        if (sLFGMgr->GetState(guid) != lfg::LFG_STATE_NONE || RandomBotLevelMgr::IsHeld(guid) ||
            DcTestRunManager::Instance().IsReserved(guid) || CoaLfgOfferedToOther(bot, nullptr))
            return false;

        if (!PlayerbotFactory::IsSelfFoundBot(bot) || !sRandomPlayerbotMgr.IsRandomBot(bot))
            return false;

        PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
        return botAI && !botAI->GetMaster();
    }

    // A party whose run is over: the run manager gave its members back.
    void Release()
    {
        for (auto it = _parties.begin(); it != _parties.end();)
        {
            bool live = false;
            for (ObjectGuid const& guid : it->members)
                live = live || DcTestRunManager::Instance().IsReserved(guid);
            if (live)
            {
                ++it;
                continue;
            }

            for (ObjectGuid const& guid : it->members)
                RandomBotLevelMgr::SetHeld(guid, false);
            LOG_INFO("playerbots.dungeonclear", "SELFFOUND END {} ({} self-found parties left)", it->runId,
                     _parties.size() - 1);
            it = _parties.erase(it);
        }
    }

    // Takes the first candidate of `pool` near `recommended` and on `team`,
    // removing it from the pool.
    Player* Take(std::vector<Candidate>& pool, TeamId team, uint32 recommended, uint32 window)
    {
        for (auto it = pool.begin(); it != pool.end(); ++it)
            if (it->team == team && Near(it->level, recommended, window))
            {
                Player* bot = it->bot;
                pool.erase(it);
                return bot;
            }
        return nullptr;
    }

    void TryLaunch(Player* driver, uint32 window)
    {
        std::vector<Candidate> tanks;
        std::vector<Candidate> heals;
        std::vector<Candidate> dps;
        for (auto const& [guid, bot] : sRandomPlayerbotMgr.GetAllBots())
        {
            if (!Free(bot))
                continue;
            Candidate const c{bot, bot->GetLevel(), bot->GetTeamId()};
            if (PlayerbotAI::IsTank(bot, true))
                tanks.push_back(c);
            else if (PlayerbotAI::IsHeal(bot, true))
                heals.push_back(c);
            else
                dps.push_back(c);
        }
        if (tanks.empty() || heals.empty() || dps.size() < 3)
            return;

        // The CoA dungeons of the classic world, the ones Dungeon Clear knows
        // best; no raid, no scenario, no Outland or Northrend.
        std::vector<DcTestDungeonRegistry::Row const*> rows;
        for (DcTestDungeonRegistry::Row const& row : DcTestDungeonRegistry::All())
            if (!DcTestDungeonRegistry::IsScenario(row) && !DcTestDungeonRegistry::IsRaid(row) &&
                DcTestDungeonRegistry::ExpansionOf(row) == 0 && row.recommendedLevel)
                rows.push_back(&row);

        Shuffle(tanks);
        Shuffle(heals);
        Shuffle(dps);
        Shuffle(rows);

        for (Candidate const& tank : tanks)
        {
            for (DcTestDungeonRegistry::Row const* row : rows)
            {
                if (!Near(tank.level, row->recommendedLevel, window))
                    continue;

                std::vector<Candidate> healPool = heals;
                std::vector<Candidate> dpsPool = dps;
                std::vector<Player*> party{tank.bot};
                if (Player* heal = Take(healPool, tank.team, row->recommendedLevel, window))
                    party.push_back(heal);
                while (party.size() >= 2 && party.size() < 5)
                {
                    Player* member = Take(dpsPool, tank.team, row->recommendedLevel, window);
                    if (!member)
                        break;
                    party.push_back(member);
                }
                if (party.size() < 5)
                    continue;

                // Roles are positional: tank, healer, then the three dps.
                std::string spec;
                for (Player* member : party)
                    spec += (spec.empty() ? "" : ",") + member->GetName();

                std::string msg;
                std::string runId;
                DcTestRunManager::StartErr err = DcTestRunManager::StartErr::None;
                if (!DcTestRunManager::Instance().StartRoster(driver, row->token, spec, /*heroic*/ false, &msg,
                                                              kPlanId, &err, &runId, /*coaDifficulty*/ 0,
                                                              /*regear*/ false, /*onlineRandomBots*/ true))
                {
                    LOG_INFO("playerbots.dungeonclear", "SELFFOUND not started ({} in {}): {}", spec, row->token,
                             msg);
                    return;
                }

                Party started;
                started.runId = runId;
                for (Player* member : party)
                {
                    RandomBotLevelMgr::SetHeld(member->GetGUID(), true);
                    started.members.push_back(member->GetGUID());
                }
                _parties.push_back(std::move(started));
                LOG_INFO("playerbots.dungeonclear", "SELFFOUND START {} dungeon={} party={} ({} self-found parties)",
                         runId, row->token, spec, _parties.size());
                return;
            }
        }
    }
}

namespace DcSelfFoundPlanner
{
    void Tick(uint32 diff)
    {
        if (!_parties.empty())
            Release();

        uint32 const maxGroups = DcSettings::GetUInt(ObjectGuid::Empty, "SelfFound.MaxGroups");
        if (!sPlayerbotAIConfig.coaSelfFoundBotsPercent || !maxGroups)
            return;

        _accumMs += diff;
        uint32 const intervalMs = DcSettings::GetUInt(ObjectGuid::Empty, "SelfFound.IntervalS") * IN_MILLISECONDS;
        if (_accumMs < intervalMs)
            return;
        _accumMs = 0;

        if (_parties.size() >= maxGroups)
            return;

        std::string why;
        DcTestDriver::Readiness const ready = DcTestDriver::Ensure(&why);
        if (ready != DcTestDriver::Readiness::Ready)
        {
            if (ready == DcTestDriver::Readiness::PendingLogin && intervalMs > kDriverRetryMs)
                _accumMs = intervalMs - kDriverRetryMs;
            else
                LOG_INFO("playerbots.dungeonclear", "SELFFOUND no driver: {}", why);
            return;
        }
        Player* driver = DcTestDriver::Get();
        if (!driver)
            return;

        TryLaunch(driver, DcSettings::GetUInt(ObjectGuid::Empty, "SelfFound.LevelWindow"));
    }
}
