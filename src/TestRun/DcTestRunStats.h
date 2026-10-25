#ifndef _PLAYERBOT_DC_TEST_RUN_STATS_H
#define _PLAYERBOT_DC_TEST_RUN_STATS_H

#include "Define.h"

// Damage and healing done by the members of live test runs, for the per-member
// DPS / HPS of the run record. Fed by a UnitScript (DcTestRunStatsScript) on the
// map threads; only registered characters are counted, so the realm's other
// players and bots cost one locked set lookup per hit.
namespace DcTestRunStats
{
    struct Counters
    {
        uint64 damageDone = 0;
        uint64 healingDone = 0;  // effective: overhealing is left out
        uint64 damageTaken = 0;  // from creatures, before death
        uint64 absorbDone = 0;   // damage soaked by the member's absorb shields, on anyone
    };

    void Track(uint32 guidLow);
    // Hands back what the character did since Track and stops counting it.
    Counters Release(uint32 guidLow);
}

void AddSC_dc_test_run_stats();

#endif
