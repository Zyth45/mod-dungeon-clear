/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"

#include "DcBagRoomDecision.h"

using DcBagRoom::BagItem;
using DcBagRoom::PickSacrifice;

namespace
{
    BagItem Item(uint32_t entry, uint32_t quality, uint32_t sellPrice, uint32_t itemClass = 4,
                 uint32_t count = 1, bool timed = false)
    {
        BagItem it;
        it.entry = entry;
        it.quality = quality;
        it.sellPrice = sellPrice;
        it.itemClass = itemClass;
        it.count = count;
        it.timed = timed;
        return it;
    }

    constexpr uint32_t GUNPOWDER = 5397;
    constexpr uint32_t TORCH = 11885;
}

// tr-20261026-060531-247: a tank with full bags looted the Lyceum torch three
// hundred times with EQUIP_ERR_INVENTORY_FULL. A grey goes first, whatever it is worth.
TEST(DcBagRoom, GreyGoesBeforeACheaperGreen)
{
    std::vector<BagItem> bags = { Item(100, 2, 5), Item(101, 0, 900), Item(102, 1, 1) };
    EXPECT_EQ(PickSacrifice(bags, GUNPOWDER), 1);
}

TEST(DcBagRoom, NoGreyThenLowestQualityThenLowestStackValue)
{
    std::vector<BagItem> bags = { Item(100, 2, 5), Item(101, 1, 40, 7, 20), Item(102, 1, 50) };
    EXPECT_EQ(PickSacrifice(bags, GUNPOWDER), 2) << "20 x 40 is worth more than 1 x 50";
}

TEST(DcBagRoom, QuestKeysBagsTimedEpicsAndTheNeededItemStay)
{
    std::vector<BagItem> bags = {
        Item(200, 0, 0, DcBagRoom::CLASS_QUEST),
        Item(201, 0, 0, DcBagRoom::CLASS_KEY),
        Item(202, 1, 0, DcBagRoom::CLASS_CONTAINER),
        Item(TORCH, 1, 0, 12, 1, /*timed*/ true),
        Item(203, 4, 1),
        Item(GUNPOWDER, 1, 0, 4),
    };
    EXPECT_EQ(PickSacrifice(bags, GUNPOWDER), -1);
    bags.push_back(Item(204, 3, 10000));
    EXPECT_EQ(PickSacrifice(bags, GUNPOWDER), 6) << "a rare goes when nothing cheaper may";
}

TEST(DcBagRoom, EmptyBags)
{
    EXPECT_EQ(PickSacrifice({}, TORCH), -1);
}
