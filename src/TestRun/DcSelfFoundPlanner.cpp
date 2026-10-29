/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "TestRun/DcSelfFoundPlanner.h"

#include <algorithm>
#include <array>
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
    // With DiagWarn, a tick that launched nothing is reported at most this often.
    constexpr time_t kQuietDiagS = 300;

    struct Party
    {
        std::string runId;
        std::vector<ObjectGuid> members;
    };

    std::vector<Party> _parties;  // world thread only
    uint32 _accumMs = 0;
    bool _configLogged = false;
    time_t _lastQuietDiag = 0;

    // After a failed run (wipe, no progress...) its members rest for
    // SelfFound.WipeCooldownMin; and whatever the result, the same five are
    // not sent together again before that long. Unix seconds.
    std::unordered_map<ObjectGuid::LowType, time_t> _cooldownUntil;
    std::map<std::string, time_t> _recentParties;  // sorted guid list -> until

    uint32 Key(char const* key)
    {
        return DcSettings::GetUInt(ObjectGuid::Empty, key);
    }

    // DungeonClear.SelfFound.DiagWarn: launcher installs log playerbots at WARN
    // only (and merge-conf drops the DungeonClear logger), so the planner's own
    // lines are raised to WARN there to reach Server.log.
    void Say(std::string const& line)
    {
        if (DcSettings::GetBool(ObjectGuid::Empty, "SelfFound.DiagWarn"))
            LOG_WARN("playerbots.dungeonclear", "{}", line);
        else
            LOG_INFO("playerbots.dungeonclear", "{}", line);
    }

    // The per-tick summary of a tick that launched nothing: every tick at INFO,
    // at most every kQuietDiagS with DiagWarn (a WARN Server.log is read by
    // people).
    void SayQuiet(std::string const& line)
    {
        if (DcSettings::GetBool(ObjectGuid::Empty, "SelfFound.DiagWarn"))
        {
            time_t const now = GameTime::GetGameTime().count();
            if (now - _lastQuietDiag < kQuietDiagS)
                return;
            _lastQuietDiag = now;
        }
        Say(line);
    }

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
        bool canTank;  // could switch to a tank specialization (CoaCanSwitchRole)
        bool canHeal;  // could switch to a healer specialization
        bool queued;   // in a battleground or Dungeon Finder queue: taken out of it at launch
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

    // Who the planner looked at this tick, filter by filter (the diag line).
    struct Census
    {
        uint32 online = 0;     // random bots in the holder
        uint32 selfFound = 0;  // ...self-found now (share, or the realm's mode)
        uint32 notReady = 0;   // logging in, teleporting, dead, flying
        uint32 combat = 0;
        uint32 grouped = 0;    // group, invite or master
        uint32 instance = 0;   // in a dungeon, raid or battleground
        uint32 lfg = 0;        // Dungeon Finder past the queue (proposal, in a dungeon...)
        uint32 cooldown = 0;   // after a failed run
        uint32 busy = 0;       // held, in another run, offered to a player by "lfg bot"
        uint32 queued = 0;     // free, but in a BG/LFG queue (taken out at launch)
        uint32 free = 0;
    };

    // Self-found now: a random bot of the share (IsSelfFoundBot), or any random
    // bot of a self-found realm (AiPlayerbot.CoaSelfFound up to its max level).
    // On such a realm every bot is self-found already and the share does not
    // pick who goes: CoaSelfFoundBotsPercent is then the part of the online bots
    // kept in dungeons (see TryLaunch).
    bool SelfFoundNow(Player* bot)
    {
        return PlayerbotFactory::IsSelfFoundBot(bot) || PlayerbotFactory::SelfFoundAt(bot->GetLevel());
    }

    // A self-found random bot that may leave what it is doing for a dungeon:
    // never one a player has a hand on (group, invite, master, LFG offer). A bot
    // that only queued itself for a battleground or the Dungeon Finder is taken
    // (the queues are left at launch): at levels 10-19 most of them are queued
    // for the battlegrounds at any time.
    bool Free(Player* bot, Census& census, bool& queued)
    {
        queued = false;
        if (!bot->IsInWorld() || bot->IsBeingTeleported() || !bot->IsAlive() || bot->IsInFlight() ||
            !bot->GetMap() || !bot->GetSession() || bot->GetSession()->isLogingOut())
        {
            ++census.notReady;
            return false;
        }
        if (bot->InBattleground() || bot->GetMap()->Instanceable())
        {
            ++census.instance;
            return false;
        }
        if (bot->IsInCombat())
        {
            ++census.combat;
            return false;
        }
        PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
        if (bot->GetGroup() || bot->GetGroupInvite() || !botAI || botAI->GetMaster())
        {
            ++census.grouped;
            return false;
        }

        ObjectGuid const guid = bot->GetGUID();
        if (auto const it = _cooldownUntil.find(guid.GetCounter());
            it != _cooldownUntil.end() && it->second > GameTime::GetGameTime().count())
        {
            ++census.cooldown;
            return false;
        }
        lfg::LfgState const lfgState = sLFGMgr->GetState(guid);
        if (lfgState != lfg::LFG_STATE_NONE && lfgState != lfg::LFG_STATE_QUEUED)
        {
            ++census.lfg;
            return false;
        }
        if (RandomBotLevelMgr::IsHeld(guid) || DcTestRunManager::Instance().IsReserved(guid) ||
            CoaLfgOfferedToOther(bot, nullptr))
        {
            ++census.busy;
            return false;
        }

        queued = bot->InBattlegroundQueue() || lfgState == lfg::LFG_STATE_QUEUED;
        if (queued)
            ++census.queued;
        ++census.free;
        return true;
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
            Say(Acore::StringFormat("SELFFOUND RELEASED {} ({} self-found parties left)", it->runId,
                                    _parties.size() - 1));
            it = _parties.erase(it);
        }
    }

    uint32 Distance(Candidate const* a, Candidate const* b)
    {
        return uint32(std::abs(int32(a->level) - int32(b->level)));
    }

    // Moves the member of `from` that `fits` nearest in level to `anchor` into
    // `party`.
    template <typename Fits>
    Candidate const* TakeNearest(std::vector<Candidate const*>& from, Candidate const* anchor, Fits fits,
                                 std::vector<Candidate const*>& party)
    {
        auto best = from.end();
        for (auto it = from.begin(); it != from.end(); ++it)
            if (fits(*it) && (best == from.end() || Distance(*it, anchor) < Distance(*best, anchor)))
                best = it;
        if (best == from.end())
            return nullptr;
        Candidate const* taken = *best;
        party.push_back(taken);
        from.erase(best);
        return taken;
    }

    struct Option
    {
        Dungeon const* dungeon;
        TeamId team;
        std::vector<Candidate const*> pool;
        uint32 tanks = 0;
        uint32 heals = 0;
        uint32 canTank = 0;  // non-tanks that could switch to a tank specialization
        uint32 canHeal = 0;  // non-healers that could switch to a healer specialization
        bool fallback = false;  // RoleFallbackMaxLevel applies to this dungeon
        char const* blocker = nullptr;  // why no party, nullptr = feasible
    };

    struct Built
    {
        std::vector<Candidate const*> party;
        Role switchLead = Role::Dps;  // Tank when the leader is switched to a tank specialization
        bool switchHeal = false;      // the healer is switched to a healer specialization
        bool sturdyLead = false;      // no tank at all: the sturdiest member leads
    };

    // Leader: the highest-level tank by spec; else (fallback dungeons) the
    // highest-level bot that can switch to tank, else the sturdiest non-healer.
    // Healer: by spec nearest the leader's level, else (fallback) one that can
    // switch. Then dps, spare tanks, anyone, nearest the leader's level.
    Built BuildParty(Option const& option)
    {
        Built out;
        std::vector<Candidate const*> pool = option.pool;

        auto pickLead = [&](auto fits, auto better) -> bool
        {
            auto lead = pool.end();
            for (auto it = pool.begin(); it != pool.end(); ++it)
                if (fits(*it) && (lead == pool.end() || better(*it, *lead)))
                    lead = it;
            if (lead == pool.end())
                return false;
            out.party.push_back(*lead);
            pool.erase(lead);
            return true;
        };
        auto higher = [](Candidate const* a, Candidate const* b) { return a->level > b->level; };
        auto sturdier = [](Candidate const* a, Candidate const* b)
        { return a->level > b->level || (a->level == b->level && a->maxHealth > b->maxHealth); };

        if (option.tanks)
            pickLead([](Candidate const* c) { return c->role == Role::Tank; }, higher);
        else if (option.fallback && option.canTank &&
                 pickLead([](Candidate const* c) { return c->role == Role::Dps && c->canTank; }, higher))
            out.switchLead = Role::Tank;
        else if (option.fallback && pickLead([](Candidate const* c) { return c->role != Role::Heal; }, sturdier))
            out.sturdyLead = true;
        if (out.party.empty())
            return {};

        Candidate const* anchor = out.party.front();
        if (!TakeNearest(pool, anchor, [](Candidate const* c) { return c->role == Role::Heal; }, out.party))
        {
            if (!option.fallback ||
                !TakeNearest(pool, anchor, [](Candidate const* c) { return c->canHeal; }, out.party))
                return {};
            out.switchHeal = true;
        }
        auto any = [](Candidate const*) { return true; };
        while (out.party.size() < 5 &&
               TakeNearest(pool, anchor, [](Candidate const* c) { return c->role == Role::Dps; }, out.party))
            ;
        while (out.party.size() < 5 &&
               TakeNearest(pool, anchor, [](Candidate const* c) { return c->role == Role::Tank; }, out.party))
            ;
        while (out.party.size() < 5 && TakeNearest(pool, anchor, any, out.party))
            ;
        if (out.party.size() < 5)
            return {};
        return out;
    }

    // One tick's attempt. Returns the reason nothing was launched ("" when a
    // party started) and fills the diagnostic lines.
    std::string TryLaunch(Player* driver, std::string& levelsLine, std::string& dungeonsLine)
    {
        uint32 const window = Key("SelfFound.LevelWindow");
        uint32 const fallbackMax = Key("SelfFound.RoleFallbackMaxLevel");
        uint32 const strictBelow = Key("SelfFound.StrictMinBelowLevel");
        time_t const now = GameTime::GetGameTime().count();
        PruneCooldowns(now);

        Census census;
        std::vector<Candidate> candidates;
        for (auto const& [guid, bot] : sRandomPlayerbotMgr.GetAllBots())
        {
            if (!bot)
                continue;
            ++census.online;
            if (!sRandomPlayerbotMgr.IsRandomBot(bot) || !SelfFoundNow(bot))
                continue;
            ++census.selfFound;
            bool queued = false;
            if (!Free(bot, census, queued))
                continue;
            Role const role = PlayerbotAI::IsTank(bot, true)   ? Role::Tank
                              : PlayerbotAI::IsHeal(bot, true) ? Role::Heal
                                                                : Role::Dps;
            candidates.push_back({bot, bot->GetLevel(), bot->GetTeamId(), role, bot->GetMaxHealth(),
                                  role != Role::Tank && CoaCanSwitchRole(bot, CoaRole::Tank),
                                  role != Role::Heal && CoaCanSwitchRole(bot, CoaRole::Heal), queued});
        }

        std::string const censusText = Acore::StringFormat(
            "online {} > self-found {} > free {} (queued {}; out: not ready {}, combat {}, grouped/master {}, "
            "instance/BG {}, lfg {}, cooldown {}, busy {})",
            census.online, census.selfFound, census.free, census.queued, census.notReady, census.combat,
            census.grouped, census.instance, census.lfg, census.cooldown, census.busy);

        // Free bots per level and faction: tanks / healers / others (+ could
        // switch to healer).
        {
            std::map<std::pair<uint32, int>, std::array<uint32, 4>> byLevel;
            for (Candidate const& c : candidates)
            {
                std::array<uint32, 4>& n = byLevel[{c.level, c.team == TEAM_ALLIANCE ? 0 : 1}];
                ++n[c.role == Role::Tank ? 0 : c.role == Role::Heal ? 1 : 2];
                n[3] += c.canHeal ? 1 : 0;
            }
            for (auto const& [key, n] : byLevel)
                levelsLine += Acore::StringFormat("{}{}{}:t{}h{}d{}+h{}", levelsLine.empty() ? "" : " ", key.first,
                                                  key.second ? "H" : "A", n[0], n[1], n[2], n[3]);
        }

        // The share: CoaSelfFoundBotsPercent of the online random bots in
        // dungeons, MaxGroups bounding it.
        uint32 const target = census.online * sPlayerbotAIConfig.coaSelfFoundBotsPercent / 100;
        if (_parties.size() * 5 >= target)
            return Acore::StringFormat("{} | share reached ({} bots in dungeons, target {})", censusText,
                                       _parties.size() * 5, target);
        if (candidates.size() < 5)
            return Acore::StringFormat("{} | fewer than 5 free self-found bots", censusText);
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
                Option option{&dungeon, team, {}};
                option.fallback = dungeon.hi <= fallbackMax;
                for (Candidate const& c : candidates)
                    if (c.team == team && c.level >= dungeon.lo && c.level <= dungeon.hi)
                    {
                        option.pool.push_back(&c);
                        option.tanks += c.role == Role::Tank ? 1 : 0;
                        option.heals += c.role == Role::Heal ? 1 : 0;
                        option.canTank += c.canTank ? 1 : 0;
                        option.canHeal += c.canHeal ? 1 : 0;
                    }
                if (option.pool.empty())
                    continue;
                if (option.pool.size() < 5)
                    option.blocker = "<5";
                else if (!option.heals && !(option.fallback && option.canHeal))
                    option.blocker = option.fallback ? "no healer" : "no healer by spec";
                else if (!option.tanks && !option.fallback)
                    option.blocker = "no tank by spec";

                dungeonsLine += Acore::StringFormat(
                    "{}{}{} {}-{} n{} t{} h{} +t{} +h{}{}{}", dungeonsLine.empty() ? "" : " ", dungeon.row->token,
                    team == TEAM_ALLIANCE ? "A" : "H", dungeon.lo, dungeon.hi, option.pool.size(), option.tanks,
                    option.heals, option.canTank, option.canHeal, option.blocker ? " " : " ok",
                    option.blocker ? option.blocker : "");
                if (option.blocker)
                    continue;
                totalWeight += uint32(option.pool.size());
                options.push_back(std::move(option));
            }
        }
        if (dungeonsLine.empty())
            dungeonsLine = "no classic dungeon's level range holds a free bot";
        if (options.empty())
            return Acore::StringFormat("{} | no dungeon can be filled", censusText);

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

        Built const built = BuildParty(*chosen);
        if (built.party.size() != 5)
            return Acore::StringFormat("{} | {}: no party could be built", censusText, chosen->dungeon->row->token);

        std::vector<ObjectGuid::LowType> lows;
        for (Candidate const* member : built.party)
            lows.push_back(member->bot->GetGUID().GetCounter());
        std::string const signature = Signature(lows);
        if (_recentParties.count(signature))
            return Acore::StringFormat("{} | {}: same five as a recent run", censusText, chosen->dungeon->row->token);

        // Roles the party lacks, taken the way the addon recruit takes them: a
        // specialization of the role (gear untouched). CoA bots of levels 10-20
        // seldom hold a healer or tank one.
        Candidate const* lead = built.party[0];
        Candidate const* healer = built.party[1];
        if (built.switchLead == Role::Tank && !CoaSwitchRole(lead->bot, CoaRole::Tank))
            return Acore::StringFormat("{} | {} could not switch to a tank specialization", censusText,
                                       lead->bot->GetName());
        if (built.switchHeal && !CoaSwitchRole(healer->bot, CoaRole::Heal))
            return Acore::StringFormat("{} | {} could not switch to a healer specialization", censusText,
                                       healer->bot->GetName());

        // Roles are positional: tank, healer, then the three dps.
        std::string spec;
        float ilvl = 0.f;
        uint32 minLevel = lead->level;
        uint32 maxLevel = minLevel;
        for (Candidate const* member : built.party)
        {
            if (member->queued)
                CoaLeaveBattlegroundQueues(member->bot);
            spec += (spec.empty() ? "" : ",") + member->bot->GetName();
            ilvl += AscensionAverageItemLevel::Equipped(member->bot);
            minLevel = std::min(minLevel, member->level);
            maxLevel = std::max(maxLevel, member->level);
        }
        ilvl /= float(built.party.size());

        DcTestDungeonRegistry::Row const* row = chosen->dungeon->row;
        std::string msg;
        std::string runId;
        DcTestRunManager::StartErr err = DcTestRunManager::StartErr::None;
        if (!DcTestRunManager::Instance().StartRoster(driver, row->token, spec, /*heroic*/ false, &msg,
                                                      DcSelfFoundPlanner::kPlanId, &err, &runId,
                                                      /*coaDifficulty*/ 0, /*regear*/ false,
                                                      /*onlineRandomBots*/ true))
            return Acore::StringFormat("{} | not started ({} in {}): {}", censusText, spec, row->token, msg);

        // Cautious pulls and full rests for this run only: a self-found party
        // wears what it found, often next to nothing (ilvl 5 at level 11). The
        // weaker its gear for its level, the smaller the pulls.
        bool const weak = ilvl * 100.f < float(maxLevel * Key("SelfFound.GearedIlvlPct"));
        std::vector<std::pair<std::string, double>> overrides;
        if (DcSettings::GetBool(ObjectGuid::Empty, "SelfFound.Cautious"))
        {
            overrides.emplace_back("PullDynamicMaxLeeroyMobs",
                                   Key(weak ? "SelfFound.PullMaxMobs" : "SelfFound.PullMaxMobsGeared"));
            overrides.emplace_back("PullForceAdvanced", weak ? 1.0 : 0.0);
            overrides.emplace_back("PullEnRouteAvoid", 1.0);
            overrides.emplace_back("SmartRest", 1.0);
            overrides.emplace_back("SmartRestHealthPct", Key("SelfFound.RestHealthPct"));
            overrides.emplace_back("SmartRestDpsManaPct", Key("SelfFound.RestManaPct"));
            overrides.emplace_back("SmartRestHealerManaPct", Key("SelfFound.RestHealerManaPct"));
        }
        DcTestRunManager::Instance().SetRunOptions(runId, std::move(overrides), Key("SelfFound.WipeRetries"));
        _recentParties[signature] = now + time_t(Key("SelfFound.WipeCooldownMin")) * MINUTE;

        Party started;
        started.runId = runId;
        for (Candidate const* member : built.party)
        {
            RandomBotLevelMgr::SetHeld(member->bot->GetGUID(), true);
            started.members.push_back(member->bot->GetGUID());
        }
        _parties.push_back(std::move(started));
        Say(Acore::StringFormat(
            "SELFFOUND START {} dungeon={} levels={}-{} faction={} ilvl={:.1f} gear={} tank={} healer={} party={} "
            "(pool {} of {} free, {} options; {} self-found parties)",
            runId, row->token, minLevel, maxLevel, chosen->team == TEAM_ALLIANCE ? "alliance" : "horde", ilvl,
            weak ? "weak" : "ok",
            built.switchLead == Role::Tank ? "switched" : built.sturdyLead ? "sturdiest" : "spec",
            built.switchHeal ? "switched" : "spec", spec, chosen->pool.size(), candidates.size(), options.size(),
            _parties.size()));
        return "";
    }

    void LogConfig()
    {
        Say(Acore::StringFormat(
            "SELFFOUND config: CoaSelfFoundBotsPercent={} CoaSelfFound={} (max level {}) MaxGroups={} IntervalS={} "
            "LevelWindow={} StrictMinBelowLevel={} RoleFallbackMaxLevel={} Cautious={} PullMaxMobs={}/{} "
            "GearedIlvlPct={} Rest={}/{}/{} WipeRetries={} WipeCooldownMin={} DiagWarn={} TestRun.MaxConcurrent={}",
            sPlayerbotAIConfig.coaSelfFoundBotsPercent, sPlayerbotAIConfig.coaSelfFound ? 1 : 0,
            sPlayerbotAIConfig.coaSelfFoundMaxLevel, Key("SelfFound.MaxGroups"), Key("SelfFound.IntervalS"),
            Key("SelfFound.LevelWindow"), Key("SelfFound.StrictMinBelowLevel"), Key("SelfFound.RoleFallbackMaxLevel"),
            DcSettings::GetBool(ObjectGuid::Empty, "SelfFound.Cautious") ? 1 : 0, Key("SelfFound.PullMaxMobs"),
            Key("SelfFound.PullMaxMobsGeared"), Key("SelfFound.GearedIlvlPct"), Key("SelfFound.RestHealthPct"),
            Key("SelfFound.RestManaPct"), Key("SelfFound.RestHealerManaPct"), Key("SelfFound.WipeRetries"),
            Key("SelfFound.WipeCooldownMin"), DcSettings::GetBool(ObjectGuid::Empty, "SelfFound.DiagWarn") ? 1 : 0,
            Key("TestRun.MaxConcurrent")));
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
            time_t const until =
                GameTime::GetGameTime().count() + time_t(Key("SelfFound.WipeCooldownMin")) * MINUTE;
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

        Say(Acore::StringFormat(
            "SELFFOUND END {} dungeon={} level={} result={} bosses={}/{} wipeRetries={} duration={}s "
            "ilvl={:.1f}->{:.1f} members=[{}] ({} self-found parties left)",
            rec.runId, rec.dungeon, rec.level, rec.result, rec.bossesKilled, rec.bossesTotal, rec.wipeRetries,
            rec.durationS, rec.gearIlvlBefore, rec.gearIlvlAfter, members, _parties.size()));
    }

    void Tick(uint32 diff)
    {
        if (!_parties.empty())
            Release();

        uint32 const maxGroups = Key("SelfFound.MaxGroups");
        if (!sPlayerbotAIConfig.coaSelfFoundBotsPercent || !maxGroups)
            return;

        if (!_configLogged)
        {
            _configLogged = true;
            LogConfig();
        }

        _accumMs += diff;
        uint32 const intervalMs = Key("SelfFound.IntervalS") * IN_MILLISECONDS;
        if (_accumMs < intervalMs)
            return;
        _accumMs = 0;

        if (_parties.size() >= maxGroups)
        {
            SayQuiet(Acore::StringFormat("SELFFOUND tick: MaxGroups reached ({} parties in dungeons)",
                                         _parties.size()));
            return;
        }

        std::string why;
        DcTestDriver::Readiness const ready = DcTestDriver::Ensure(&why);
        if (ready != DcTestDriver::Readiness::Ready)
        {
            if (ready == DcTestDriver::Readiness::PendingLogin && intervalMs > kDriverRetryMs)
                _accumMs = intervalMs - kDriverRetryMs;
            SayQuiet("SELFFOUND tick: no driver: " + why);
            return;
        }
        Player* driver = DcTestDriver::Get();
        if (!driver)
        {
            SayQuiet("SELFFOUND tick: no driver character in the world");
            return;
        }

        std::string levels;
        std::string dungeons;
        std::string const blocked = TryLaunch(driver, levels, dungeons);
        if (blocked.empty())
            return;
        // Nothing launched: the census and the reason, then the free bots by
        // level and faction, then the dungeons looked at.
        SayQuiet("SELFFOUND tick: nothing launched: " + blocked +
                 (levels.empty() ? std::string() : "\nSELFFOUND levels: " + levels) +
                 (dungeons.empty() ? std::string() : "\nSELFFOUND dungeons: " + dungeons));
    }
}
