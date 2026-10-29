/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "TestRun/DcSelfFoundPlanner.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iterator>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "AscensionAverageItemLevel.h"
#include "CoaSpecialization.h"
#include "Common.h"
#include "GameTime.h"
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
#include "StringFormat.h"

#include "Ai/Dungeon/DungeonClear/Settings/DcSettings.h"
#include "TestRun/DcTestDriver.h"
#include "TestRun/DcTestDungeonRegistry.h"
#include "TestRun/DcTestRunManager.h"

namespace
{
    // How soon to look again when the headless driver is still logging in.
    constexpr uint32 kDriverRetryMs = 15000;

    struct Party
    {
        std::string runId;
        std::vector<ObjectGuid> members;
    };

    std::vector<Party> _parties;  // world thread only
    uint32 _accumMs = 0;

    // After a failed run (wipe, no progress...) its members rest for
    // SelfFound.WipeCooldownMin; and whatever the result, the same five are
    // not sent together again before that long. Unix seconds.
    std::unordered_map<ObjectGuid::LowType, time_t> _cooldownUntil;
    std::map<std::string, time_t> _recentParties;  // sorted guid list -> until

    std::string Signature(std::vector<ObjectGuid::LowType> lows)
    {
        std::sort(lows.begin(), lows.end());
        std::string out;
        for (ObjectGuid::LowType low : lows)
            out += std::to_string(low) + ",";
        return out;
    }

    void PruneCooldowns(time_t now)
    {
        for (auto it = _cooldownUntil.begin(); it != _cooldownUntil.end();)
            it = it->second <= now ? _cooldownUntil.erase(it) : std::next(it);
        for (auto it = _recentParties.begin(); it != _recentParties.end();)
            it = it->second <= now ? _recentParties.erase(it) : std::next(it);
    }

    enum class Role
    {
        Tank,
        Heal,
        Dps
    };

    struct Candidate
    {
        Player* bot;
        uint32 level;
        TeamId team;
        Role role;
        uint32 maxHealth;
    };

    // The level range a player does each classic dungeon at (the dungeon
    // finder's). The registry only has the level a test run is played at, high
    // in the range (Deadmines 24): self-found bots go at the levels players go.
    // A classic row missing here uses its recommended level plus or minus
    // SelfFound.LevelWindow.
    struct Range
    {
        char const* token;
        uint32 lo;
        uint32 hi;
    };

    constexpr Range kRanges[] = {
        {"rfc", 13, 18},         {"wc", 17, 24},       {"deadmines", 17, 26}, {"sfk", 22, 30},
        {"stockade", 22, 30},    {"bfd", 24, 32},      {"sm-gy", 26, 36},     {"rfk", 29, 38},
        {"gnomer", 29, 38},      {"sm-lib", 29, 39},   {"sm-arm", 32, 42},    {"sm-cath", 35, 45},
        {"rfd", 37, 46},         {"uldaman", 41, 51},  {"zf", 44, 54},        {"mara-orange", 46, 55},
        {"mara-purple", 46, 55}, {"mara-pw", 48, 57},  {"maraudon", 48, 57},  {"st", 50, 60},
        {"brd-db", 52, 60},      {"brd-uc", 54, 60},   {"lbrs", 55, 60},      {"dm-east", 55, 60},
        {"dm-west", 57, 60},     {"dm-north", 57, 60}, {"ubrs", 58, 60},      {"scholo", 58, 60},
        {"strat", 58, 60},
    };

    struct Dungeon
    {
        DcTestDungeonRegistry::Row const* row;
        uint32 lo;
        uint32 hi;
    };

    template <typename T>
    void Shuffle(std::vector<T>& v)
    {
        for (std::size_t i = v.size(); i > 1; --i)
            std::swap(v[i - 1], v[urand(0, uint32(i - 1))]);
    }

    // The classic 5-man dungeons with the levels a self-found member may have
    // there: the dungeon's range, opened `window` levels lower, except for the
    // dungeons that start below `strictBelow` (RFC, WC, Deadmines): a bot
    // under their minimum, with the gear it found, only wipes there (Laptop-mini
    // 29/10: two level 11 parties wiped on RFC's first packs).
    std::vector<Dungeon> ClassicDungeons(uint32 window, uint32 strictBelow)
    {
        std::vector<Dungeon> out;
        for (DcTestDungeonRegistry::Row const& row : DcTestDungeonRegistry::All())
        {
            if (DcTestDungeonRegistry::IsScenario(row) || DcTestDungeonRegistry::IsRaid(row) ||
                DcTestDungeonRegistry::ExpansionOf(row) != 0 || !row.recommendedLevel)
                continue;

            uint32 lo = row.recommendedLevel > window ? row.recommendedLevel - window : 1;
            uint32 hi = row.recommendedLevel + window;
            for (Range const& range : kRanges)
                if (!std::strcmp(range.token, row.token))
                {
                    lo = range.lo < strictBelow ? range.lo : range.lo - window;
                    hi = range.hi;
                    break;
                }
            out.push_back({&row, lo, hi});
        }
        return out;
    }

    // A random bot of the self-found share that may leave what it is doing for
    // a dungeon: never one a player has a hand on (group, invite, master, LFG
    // offer). On a self-found realm (AiPlayerbot.CoaSelfFound) the share picks
    // which of the bots, all self-found already, go.
    bool Free(Player* bot)
    {
        if (!bot || !bot->IsInWorld() || bot->IsBeingTeleported() || !bot->IsAlive() || bot->IsInCombat() ||
            bot->GetGroup() || bot->GetGroupInvite() || bot->InBattleground() || bot->InBattlegroundQueue() ||
            bot->IsInFlight() || !bot->GetMap() || bot->GetMap()->Instanceable())
            return false;

        if (!PlayerbotFactory::IsSelfFoundGuid(bot->GetGUID().GetCounter()) || !sRandomPlayerbotMgr.IsRandomBot(bot))
            return false;
        if (!PlayerbotFactory::IsSelfFoundBot(bot) && !PlayerbotFactory::SelfFoundAt(bot->GetLevel()))
            return false;

        ObjectGuid const guid = bot->GetGUID();
        if (auto const it = _cooldownUntil.find(guid.GetCounter());
            it != _cooldownUntil.end() && it->second > GameTime::GetGameTime().count())
            return false;
        if (sLFGMgr->GetState(guid) != lfg::LFG_STATE_NONE || RandomBotLevelMgr::IsHeld(guid) ||
            DcTestRunManager::Instance().IsReserved(guid) || CoaLfgOfferedToOther(bot, nullptr))
            return false;

        PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
        return botAI && !botAI->GetMaster();
    }

    void Unhold(std::vector<ObjectGuid> const& members)
    {
        for (ObjectGuid const& guid : members)
            RandomBotLevelMgr::SetHeld(guid, false);
    }

    // Safety net: a party the run manager dropped without reporting a finished
    // run is given back here once none of its members is reserved any more.
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

            Unhold(it->members);
            LOG_INFO("playerbots.dungeonclear", "SELFFOUND RELEASED {} ({} self-found parties left)", it->runId,
                     _parties.size() - 1);
            it = _parties.erase(it);
        }
    }

    uint32 Distance(Candidate const* a, Candidate const* b)
    {
        return uint32(std::abs(int32(a->level) - int32(b->level)));
    }

    // Moves the member of `from` with `role` (any role when null) nearest in
    // level to `anchor` into `party`.
    bool TakeNearest(std::vector<Candidate const*>& from, Candidate const* anchor, Role const* role,
                     std::vector<Candidate const*>& party)
    {
        auto best = from.end();
        for (auto it = from.begin(); it != from.end(); ++it)
            if ((!role || (*it)->role == *role) &&
                (best == from.end() || Distance(*it, anchor) < Distance(*best, anchor)))
                best = it;
        if (best == from.end())
            return false;
        party.push_back(*best);
        from.erase(best);
        return true;
    }

    struct Option
    {
        Dungeon const* dungeon;
        TeamId team;
        std::vector<Candidate const*> pool;
        bool specTank;
    };

    // Tank (by spec, or the sturdiest non-healer when the option has no spec
    // tank), then the healer and three dps nearest to the tank's level; short
    // of dps, spare tanks then anyone left fill in.
    std::vector<Candidate const*> BuildParty(Option const& option)
    {
        std::vector<Candidate const*> pool = option.pool;
        std::vector<Candidate const*> party;

        // The upper part of the range first: the leader is the highest-level
        // tank (the pool is shuffled, so a random one among equals), or with no
        // tank by spec the highest-level, then sturdiest, non-healer; the rest
        // is taken nearest to its level.
        auto lead = pool.end();
        for (auto it = pool.begin(); it != pool.end(); ++it)
        {
            bool const fits = option.specTank ? (*it)->role == Role::Tank : (*it)->role != Role::Heal;
            if (fits && (lead == pool.end() || (*it)->level > (*lead)->level ||
                         ((*it)->level == (*lead)->level && !option.specTank &&
                          (*it)->maxHealth > (*lead)->maxHealth)))
                lead = it;
        }
        if (lead == pool.end())
            return {};
        party.push_back(*lead);
        pool.erase(lead);

        Role const heal = Role::Heal;
        Role const dps = Role::Dps;
        Role const tank = Role::Tank;
        Candidate const* anchor = party.front();
        if (!TakeNearest(pool, anchor, &heal, party))
            return {};
        while (party.size() < 5 && TakeNearest(pool, anchor, &dps, party))
            ;
        while (party.size() < 5 && TakeNearest(pool, anchor, &tank, party))
            ;
        while (party.size() < 5 && TakeNearest(pool, anchor, nullptr, party))
            ;
        if (party.size() < 5)
            return {};
        return party;
    }

    void TryLaunch(Player* driver)
    {
        uint32 const window = DcSettings::GetUInt(ObjectGuid::Empty, "SelfFound.LevelWindow");
        uint32 const fallbackMax = DcSettings::GetUInt(ObjectGuid::Empty, "SelfFound.RoleFallbackMaxLevel");
        uint32 const strictBelow = DcSettings::GetUInt(ObjectGuid::Empty, "SelfFound.StrictMinBelowLevel");
        time_t const now = GameTime::GetGameTime().count();
        PruneCooldowns(now);

        std::vector<Candidate> candidates;
        for (auto const& [guid, bot] : sRandomPlayerbotMgr.GetAllBots())
        {
            if (!Free(bot))
                continue;
            Role const role = PlayerbotAI::IsTank(bot, true)   ? Role::Tank
                              : PlayerbotAI::IsHeal(bot, true) ? Role::Heal
                                                                : Role::Dps;
            candidates.push_back({bot, bot->GetLevel(), bot->GetTeamId(), role, bot->GetMaxHealth()});
        }
        if (candidates.size() < 5)
            return;
        Shuffle(candidates);

        // Every (dungeon, faction) a party could be formed for, weighted by the
        // bots it could take: the most populated levels get the most parties.
        std::vector<Dungeon> const dungeons = ClassicDungeons(window, strictBelow);
        std::vector<Option> options;
        uint32 totalWeight = 0;
        for (Dungeon const& dungeon : dungeons)
        {
            for (TeamId team : {TEAM_ALLIANCE, TEAM_HORDE})
            {
                Option option{&dungeon, team, {}, false};
                uint32 tanks = 0;
                uint32 heals = 0;
                for (Candidate const& c : candidates)
                    if (c.team == team && c.level >= dungeon.lo && c.level <= dungeon.hi)
                    {
                        option.pool.push_back(&c);
                        tanks += c.role == Role::Tank ? 1 : 0;
                        heals += c.role == Role::Heal ? 1 : 0;
                    }
                option.specTank = tanks > 0;
                if (option.pool.size() < 5 || !heals || (!tanks && dungeon.hi > fallbackMax))
                    continue;
                totalWeight += uint32(option.pool.size());
                options.push_back(std::move(option));
            }
        }
        if (options.empty())
        {
            LOG_DEBUG("playerbots.dungeonclear", "SELFFOUND no party possible ({} free self-found bots)",
                      candidates.size());
            return;
        }

        uint32 pick = urand(0, totalWeight - 1);
        Option const* chosen = &options.back();
        for (Option const& option : options)
        {
            if (pick < option.pool.size())
            {
                chosen = &option;
                break;
            }
            pick -= uint32(option.pool.size());
        }

        std::vector<Candidate const*> const party = BuildParty(*chosen);
        if (party.size() != 5)
            return;

        std::vector<ObjectGuid::LowType> lows;
        for (Candidate const* member : party)
            lows.push_back(member->bot->GetGUID().GetCounter());
        std::string const signature = Signature(lows);
        if (_recentParties.count(signature))
        {
            LOG_DEBUG("playerbots.dungeonclear", "SELFFOUND same party as a recent run, not resent");
            return;
        }

        // Roles are positional: tank, healer, then the three dps.
        std::string spec;
        float ilvl = 0.f;
        uint32 minLevel = party.front()->level;
        uint32 maxLevel = minLevel;
        for (Candidate const* member : party)
        {
            spec += (spec.empty() ? "" : ",") + member->bot->GetName();
            ilvl += AscensionAverageItemLevel::Equipped(member->bot);
            minLevel = std::min(minLevel, member->level);
            maxLevel = std::max(maxLevel, member->level);
        }
        ilvl /= float(party.size());

        DcTestDungeonRegistry::Row const* row = chosen->dungeon->row;
        std::string msg;
        std::string runId;
        DcTestRunManager::StartErr err = DcTestRunManager::StartErr::None;
        if (!DcTestRunManager::Instance().StartRoster(driver, row->token, spec, /*heroic*/ false, &msg,
                                                      DcSelfFoundPlanner::kPlanId, &err, &runId,
                                                      /*coaDifficulty*/ 0, /*regear*/ false,
                                                      /*onlineRandomBots*/ true))
        {
            LOG_INFO("playerbots.dungeonclear", "SELFFOUND not started ({} in {}): {}", spec, row->token, msg);
            return;
        }

        // Cautious pulls and full rests for this run only: a self-found party
        // wears what it found, often next to nothing (ilvl 5 at level 11). The
        // weaker its gear for its level, the smaller the pulls.
        uint32 const level = maxLevel;
        uint32 const gearedPct = DcSettings::GetUInt(ObjectGuid::Empty, "SelfFound.GearedIlvlPct");
        bool const weak = ilvl * 100.f < float(level * gearedPct);
        std::vector<std::pair<std::string, double>> overrides;
        if (DcSettings::GetBool(ObjectGuid::Empty, "SelfFound.Cautious"))
        {
            overrides.emplace_back("PullDynamicMaxLeeroyMobs",
                                   DcSettings::GetUInt(ObjectGuid::Empty, weak ? "SelfFound.PullMaxMobs"
                                                                               : "SelfFound.PullMaxMobsGeared"));
            overrides.emplace_back("PullForceAdvanced", weak ? 1.0 : 0.0);
            overrides.emplace_back("PullEnRouteAvoid", 1.0);
            overrides.emplace_back("SmartRest", 1.0);
            overrides.emplace_back("SmartRestHealthPct",
                                   DcSettings::GetUInt(ObjectGuid::Empty, "SelfFound.RestHealthPct"));
            overrides.emplace_back("SmartRestDpsManaPct",
                                   DcSettings::GetUInt(ObjectGuid::Empty, "SelfFound.RestManaPct"));
            overrides.emplace_back("SmartRestHealerManaPct",
                                   DcSettings::GetUInt(ObjectGuid::Empty, "SelfFound.RestHealerManaPct"));
        }
        DcTestRunManager::Instance().SetRunOptions(runId, std::move(overrides),
                                                   DcSettings::GetUInt(ObjectGuid::Empty, "SelfFound.WipeRetries"));
        _recentParties[signature] =
            now + time_t(DcSettings::GetUInt(ObjectGuid::Empty, "SelfFound.WipeCooldownMin")) * MINUTE;

        Party started;
        started.runId = runId;
        for (Candidate const* member : party)
        {
            RandomBotLevelMgr::SetHeld(member->bot->GetGUID(), true);
            started.members.push_back(member->bot->GetGUID());
        }
        _parties.push_back(std::move(started));
        LOG_INFO("playerbots.dungeonclear",
                 "SELFFOUND START {} dungeon={} levels={}-{} faction={} ilvl={:.1f} gear={} tank={} party={} "
                 "(pool {} of {} free, {} options; {} self-found parties)",
                 runId, row->token, minLevel, maxLevel, chosen->team == TEAM_ALLIANCE ? "alliance" : "horde", ilvl,
                 weak ? "weak" : "ok", chosen->specTank ? "spec" : "fallback", spec, chosen->pool.size(),
                 candidates.size(), options.size(), _parties.size());
    }
}

namespace DcSelfFoundPlanner
{
    void OnRunFinished(DcTestRunRecord::Record const& rec)
    {
        // name level:ilvl before->after/slots changed, per member.
        std::string members;
        for (DcTestRunRecord::CompEntry const& c : rec.comp)
            members += Acore::StringFormat("{}{} {}:{:.1f}->{:.1f}/{}", members.empty() ? "" : " ", c.name, c.level,
                                           c.itemLevel, c.itemLevelAfter, c.itemsChanged);

        // A failed run: its members rest before another dungeon.
        if (rec.result != "success")
        {
            time_t const until = GameTime::GetGameTime().count() +
                                 time_t(DcSettings::GetUInt(ObjectGuid::Empty, "SelfFound.WipeCooldownMin")) * MINUTE;
            for (DcTestRunRecord::CompEntry const& c : rec.comp)
                _cooldownUntil[ObjectGuid(c.guid).GetCounter()] = until;
        }

        for (auto it = _parties.begin(); it != _parties.end(); ++it)
            if (it->runId == rec.runId)
            {
                Unhold(it->members);
                _parties.erase(it);
                break;
            }

        LOG_INFO("playerbots.dungeonclear",
                 "SELFFOUND END {} dungeon={} level={} result={} bosses={}/{} wipeRetries={} duration={}s "
                 "ilvl={:.1f}->{:.1f} members=[{}] ({} self-found parties left)",
                 rec.runId, rec.dungeon, rec.level, rec.result, rec.bossesKilled, rec.bossesTotal, rec.wipeRetries,
                 rec.durationS,
                 rec.gearIlvlBefore, rec.gearIlvlAfter, members, _parties.size());
    }

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

        TryLaunch(driver);
    }
}
