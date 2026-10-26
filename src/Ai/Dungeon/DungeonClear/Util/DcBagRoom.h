/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DC_BAG_ROOM_H
#define _PLAYERBOT_DC_BAG_ROOM_H

#include <vector>

#include "Bag.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "Player.h"

#include "DcBagRoomDecision.h"

// IMPURE side of DcBagRoomDecision: before the clear grants or loots an item an
// event needs, make sure the bot has a slot for it, destroying the cheapest
// expendable bag item when it has none. See DcBagRoomDecision.h for the runs that
// stalled on full bags and for what is never given up.
namespace DcBagRoom
{
    inline bool HasRoomFor(Player* bot, uint32 itemEntry)
    {
        ItemPosCountVec dest;
        return bot->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, itemEntry, 1) == EQUIP_ERR_OK;
    }

    // True when `bot` can take one `itemEntry` (it already could, or a slot was
    // freed). `why` names the event in the INFO line a sacrifice leaves.
    inline bool EnsureRoomFor(Player* bot, uint32 itemEntry, char const* why)
    {
        if (!bot)
            return false;
        if (HasRoomFor(bot, itemEntry))
            return true;

        struct Where
        {
            uint8 bag;
            uint8 slot;
        };
        std::vector<BagItem> items;
        std::vector<Where> where;
        auto visit = [&](Item* item, uint8 bag, uint8 slot)
        {
            if (!item)
                return;
            ItemTemplate const* proto = item->GetTemplate();
            if (!proto)
                return;
            BagItem it;
            it.entry = item->GetEntry();
            it.itemClass = proto->Class;
            it.quality = proto->Quality;
            it.sellPrice = proto->SellPrice;
            it.count = item->GetCount();
            it.timed = proto->Duration != 0;
            items.push_back(it);
            where.push_back({ bag, slot });
        };
        for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
            visit(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot), INVENTORY_SLOT_BAG_0, slot);
        for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
            if (Bag* b = bot->GetBagByPos(bag))
                for (uint32 slot = 0; slot < b->GetBagSize(); ++slot)
                    visit(b->GetItemByPos(static_cast<uint8>(slot)), bag, static_cast<uint8>(slot));

        int32 const pick = PickSacrifice(items, itemEntry);
        if (pick < 0)
        {
            LOG_INFO("playerbots.dungeonclear",
                     "[DC:{}] bags full and nothing expendable in them: no room for item {} ({})",
                     bot->GetName(), itemEntry, why);
            return false;
        }

        BagItem const& gone = items[static_cast<size_t>(pick)];
        Where const at = where[static_cast<size_t>(pick)];
        bot->DestroyItem(at.bag, at.slot, /*update*/ true);
        LOG_INFO("playerbots.dungeonclear",
                 "[DC:{}] bags full: destroyed {}x item {} (quality {}) to make room for item {} ({})",
                 bot->GetName(), gone.count, gone.entry, gone.quality, itemEntry, why);
        return HasRoomFor(bot, itemEntry);
    }
}

#endif
