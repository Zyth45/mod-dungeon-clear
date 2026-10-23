#include "TestRun/DcTestRunStats.h"

#include "Player.h"
#include "ScriptMgr.h"
#include "Unit.h"
#include "UnitScript.h"

#include <algorithm>
#include <mutex>
#include <unordered_map>

namespace
{
    std::mutex g_mutex;
    std::unordered_map<uint32, DcTestRunStats::Counters> g_tracked;

    // A pet's or a totem's hits belong to the character that owns it.
    uint32 OwnerGuidLow(Unit* unit)
    {
        if (!unit)
            return 0;
        Player* player = unit->GetCharmerOrOwnerPlayerOrPlayerItself();
        return player ? player->GetGUID().GetCounter() : 0;
    }
}

namespace DcTestRunStats
{
    void Track(uint32 guidLow)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_tracked[guidLow] = Counters{};
    }

    Counters Release(uint32 guidLow)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto itr = g_tracked.find(guidLow);
        if (itr == g_tracked.end())
            return {};
        Counters const out = itr->second;
        g_tracked.erase(itr);
        return out;
    }
}

class DcTestRunStatsScript : public UnitScript
{
public:
    DcTestRunStatsScript() : UnitScript("DcTestRunStatsScript", true, { UNITHOOK_ON_DAMAGE, UNITHOOK_ON_HEAL }) { }

    void OnDamage(Unit* attacker, Unit* victim, uint32& damage) override
    {
        if (!damage || !victim || victim->IsPlayer())
            return;
        uint32 const guid = OwnerGuidLow(attacker);
        if (!guid)
            return;
        std::lock_guard<std::mutex> lock(g_mutex);
        if (auto itr = g_tracked.find(guid); itr != g_tracked.end())
            itr->second.damageDone += std::min<uint32>(damage, victim->GetHealth());
    }

    void OnHeal(Unit* healer, Unit* reciever, uint32& gain) override
    {
        if (!gain || !reciever)
            return;
        uint32 const guid = OwnerGuidLow(healer);
        if (!guid)
            return;
        uint32 const missing = reciever->GetMaxHealth() - std::min(reciever->GetHealth(), reciever->GetMaxHealth());
        std::lock_guard<std::mutex> lock(g_mutex);
        if (auto itr = g_tracked.find(guid); itr != g_tracked.end())
            itr->second.healingDone += std::min(gain, missing);
    }
};

void AddSC_dc_test_run_stats()
{
    new DcTestRunStatsScript();
}
