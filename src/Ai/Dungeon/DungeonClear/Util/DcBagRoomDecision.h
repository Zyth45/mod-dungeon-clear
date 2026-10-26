/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DC_BAG_ROOM_DECISION_H
#define _PLAYERBOT_DC_BAG_ROOM_DECISION_H

#include <cstdint>
#include <tuple>
#include <vector>

// Which bag item a bot gives up so the clear can hand it the item an event needs.
//
// Night of 26/10 on the NUC: three Deadmines runs stalled on the Iron Clad Door
// because the tank's bags were full and the Defias Gunpowder could not be granted
// (tr-20261026-005403-18, -021102-140, -060600-252), and two BRD runs stalled in
// the Lyceum because the tank, standing on the Flame Keeper's corpse, looted the
// Shadowforge Torch three hundred times with EQUIP_ERR_INVENTORY_FULL (result
// 50; tr-20261026-060531-247). A player in that spot drops a grey and goes on; a
// bot never does, so the event waits on a slot that never comes.
//
// The pick, cheapest first: a grey (poor) item before anything else, then the
// lowest quality, then the lowest vendor value of the whole stack. Never given
// up: quest items, keys, containers, timed items (a Shadowforge Torch runs a
// 300s clock and IS what the event is waiting for), epic and better, and the item
// being made room for.
namespace DcBagRoom
{
    // Mirrors of the core's ItemClass / ItemQualities values the pick reads, so
    // this header stays core-free for the unit tests.
    constexpr uint32_t CLASS_CONTAINER = 1;
    constexpr uint32_t CLASS_QUEST = 12;
    constexpr uint32_t CLASS_KEY = 13;
    constexpr uint32_t QUALITY_POOR = 0;
    constexpr uint32_t QUALITY_EPIC = 4;

    struct BagItem
    {
        uint32_t entry = 0;
        uint32_t itemClass = 0;
        uint32_t quality = 0;
        uint32_t sellPrice = 0;  // per unit
        uint32_t count = 1;
        bool timed = false;      // the template carries a Duration
    };

    inline bool Expendable(BagItem const& it, uint32_t neededEntry)
    {
        if (it.entry == 0 || it.entry == neededEntry || it.timed)
            return false;
        if (it.itemClass == CLASS_QUEST || it.itemClass == CLASS_KEY ||
            it.itemClass == CLASS_CONTAINER)
            return false;
        return it.quality < QUALITY_EPIC;
    }

    // Index of the item to destroy, or -1 when nothing in the bags may go.
    inline int32_t PickSacrifice(std::vector<BagItem> const& items, uint32_t neededEntry)
    {
        int32_t best = -1;
        auto key = [](BagItem const& it)
        {
            uint64_t const value = static_cast<uint64_t>(it.sellPrice) * (it.count ? it.count : 1);
            return std::make_tuple(it.quality == QUALITY_POOR ? 0 : 1, it.quality, value);
        };
        for (size_t i = 0; i < items.size(); ++i)
        {
            if (!Expendable(items[i], neededEntry))
                continue;
            if (best < 0 || key(items[i]) < key(items[static_cast<size_t>(best)]))
                best = static_cast<int32_t>(i);
        }
        return best;
    }
}

#endif
