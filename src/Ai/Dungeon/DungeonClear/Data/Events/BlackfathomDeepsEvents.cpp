/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "Ai/Dungeon/DungeonClear/Data/DungeonClearRouteRegistry.h"
#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonEventTables.h"

// --- Blackfathom Deeps (map 48) ----------------------------------------------
//
// No roster patch and no events: the auto-derived roster is right (Ghamoo-ra,
// Lady Sarevess, Gelihast, Lorgus Jett, Old Serra'kis, Twilight Lord Kelris,
// Aku'mai, in DBC order). What fails is the WALK, and only in water. One clear
// in nineteen finished on the 23-24/10 normal runs:
//
//   * "En route to Lady Sarevess" in the submerged passage, 5 runs, the party
//     drowning at (-366, 298, -73) / (-404, 319, -66) for ten minutes.
//   * "Holding near Gelihast" 25yd from him, the tank in the lake 6yd below his
//     cave, off the mesh.
//   * Kelris: the route ends in the pool UNDER his platform, 5yd from him in
//     plan and 6yd below (z -30.9 against -24.6); he is never engaged.
//   * After Kelris, the whole party off the map on the way to Aku'mai.
//
// WHY. The 048 mmtiles are not one walkable surface. Measured on the repack
// mmaps (Detour polys read straight from 0483231/0483232/0483331/0483332):
// the dungeon's main component holds 2611 polys, and around it sit separate
// islands — Lady Sarevess's island (88 polys, a pool at its south end), a
// piece of water sheet in the passage that leads there (132 polys), a 91-poly
// piece of lake just below Gelihast's cave, and the floors of the deep lakes
// and pools (the 322-poly floor of Old Serra'kis's pool runs on under Kelris's
// platform; the 51-poly water sheet over it touches no walkable ground at all).
// A party standing in one of those has no navmesh route anywhere; the planner
// then falls back to the swim leg (SwimPathfinder), which heads STRAIGHT at the
// boss — through the passage walls to Sarevess, at the 9yd ledge under
// Gelihast's cave, under Kelris's platform to the surface beneath him. None of
// those ends anywhere a party can climb out.
//
// THE FIX is the anchor route (DungeonClearRouteRegistry), one row per leg that
// failed, keyed on the leg's DESTINATION and starting WHERE THE PARTY STANDS
// when it begins (SeedCursor projects the party onto the row from there —
// [[dc-anchor-route-must-cover-where-the-party-stands]]). A registered row is
// always "reachable", so the swim fallback never fires for these four bosses.
//
// HOW THE ANCHORS WERE MADE. A* over the poly graph of the repack mmaps, water
// priced 3x like DungeonClear.WaterPathCost, string-pulled with the portals
// pulled 1.5yd in from each end, then cut to legs under 18yd, far inside
// DungeonPathFollower::RESNAP_RADIUS (45). Every leg except the SWIM legs of
// the first two rows was sampled every 0.5yd: each sample lies on a poly of the
// leg's own component within 1.5yd of the anchor-to-anchor height. Water is
// crossed only where the main mesh crosses it: the shallow sheets of Gelihast's
// cave and of Aku'mai's lair (walked), and at the surface Ghamoo-ra's lake
// and a strip of the pool under Kelris's platform (swum). No anchor
// is below the lakes' surface. Monster spawns confirm the dry stretches: herbs
// (Bruiseweed at (-331.1, 362.2)) and the naga camp on Sarevess's island, the
// Twilight Elementalists and Shadowmages on Kelris's platform (z -25.8), the
// Snapjaws in Aku'mai's corridor.
//
// The boss's own spawn is appended as the goal by StridedPathfinder, so every
// row stops 12-20yd short of it, inside the at-boss handoff range and on the
// boss's own floor.
namespace
{
    constexpr uint32 BFD_MAP = 48;

    constexpr uint32 BFD_LADY_SAREVESS = 4831;
    constexpr uint32 BFD_GELIHAST      = 6243;
    constexpr uint32 BFD_KELRIS        = 4832;
    constexpr uint32 BFD_AKUMAI        = 4829;
}

void RegisterBlackfathomDeepsRoute()
{
    // --- Ghamoo-ra -> Lady Sarevess ------------------------------------------
    // Lady Sarevess (-299.9, 413.8, -57.1) stands on an island the main mesh
    // never reaches. The way in is the passage that opens at (-365, 277):
    // the lake's water sheet stops at (-362.5, 279.4), a second sheet runs to
    // (-350.0, 290.9), and the island's own pool starts behind it, all three at
    // the lake's surface height (z -56) over a channel floor at z -72 to -77.
    // Anchors 8 -> 9 -> 10 are the only legs here that leave a mesh: two short
    // SURFACE swims (13.5 and 16.9yd) through those two breaks. They stay at
    // z -56 on purpose: the swim fallback that dived for the channel floor is
    // what drowned the party at (-366, 298, -73).
    DungeonClearRouteRegistry::Register(BFD_MAP, DUNGEON_DIFFICULTY_NORMAL, BFD_LADY_SAREVESS,
        {
            { -442.40f, 211.80f, -51.69f },  // 0  Ghamoo-ra's spawn, where he dies
            { -428.63f, 211.83f, -52.56f },  // 1  his shore
            { -418.64f, 220.31f, -52.90f },  // 2
            { -409.51f, 230.08f, -53.74f },  // 3  last dry anchor before the lake
            { -403.06f, 234.68f, -55.01f },  // 4  the lake's surface (main mesh)
            { -393.69f, 244.12f, -55.66f },  // 5
            { -384.31f, 253.56f, -55.95f },  // 6
            { -375.83f, 264.14f, -55.96f },  // 7
            { -367.50f, 274.90f, -55.96f },  // 8  last main-mesh water before the passage
            { -357.50f, 284.00f, -55.80f },  // 9  SWIM: the passage's middle sheet
            { -345.00f, 295.40f, -55.96f },  // 10 SWIM: the island's pool
            { -351.21f, 307.22f, -55.93f },  // 11
            { -356.53f, 320.00f, -54.89f },  // 12 the pool's north shore
            { -353.59f, 333.58f, -53.86f },  // 13 dry: the naga camp
            { -350.65f, 347.15f, -52.71f },  // 14
            { -347.71f, 360.73f, -52.99f },  // 15 by the Bruiseweed at (-331.1, 362.2)
            { -344.77f, 374.30f, -53.53f },  // 16
            { -338.25f, 386.17f, -52.86f },  // 17
            { -327.07f, 394.23f, -53.83f },  // 18
            { -315.88f, 402.29f, -54.88f },  // 19 19.7yd short of her, same floor
        });

    // --- Lady Sarevess -> Gelihast -------------------------------------------
    // The same passage back out, then the main mesh's own way to Gelihast's
    // murloc cave: across the lake west of Ghamoo-ra (z -56, swum at the
    // surface), south past the naga camp at (-614, 245), east along the
    // Fallenroot corridor (y -1.5) and into the cave's shallow sheet (z -47.4;
    // the murlocs stand in it at z -48). The cave's sheet ends near y 60, 9yd
    // above the piece of lake that starts at y 77, and that piece is a mesh
    // island of its own: a party that drifts there is the "Holding near
    // Gelihast, 6yd below him" stall. This row never goes near it.
    DungeonClearRouteRegistry::Register(BFD_MAP, DUNGEON_DIFFICULTY_NORMAL, BFD_GELIHAST,
        {
            { -299.90f, 413.80f, -56.49f },  // 0  Lady Sarevess's spawn, where she dies
            { -311.09f, 405.74f, -55.88f },  // 1
            { -322.27f, 397.68f, -54.36f },  // 2
            { -333.46f, 389.62f, -53.68f },  // 3
            { -343.61f, 380.15f, -53.85f },  // 4
            { -346.91f, 366.69f, -53.65f },  // 5
            { -350.20f, 353.22f, -53.16f },  // 6
            { -353.50f, 339.75f, -53.35f },  // 7  the naga camp
            { -356.80f, 326.28f, -54.38f },  // 8  down to the island's pool
            { -354.21f, 312.24f, -55.63f },  // 9
            { -347.76f, 300.45f, -55.96f },  // 10
            { -345.00f, 295.40f, -55.96f },  // 11 the pool's south end
            { -357.50f, 284.00f, -55.80f },  // 12 SWIM: the passage's middle sheet
            { -367.50f, 274.90f, -55.96f },  // 13 SWIM: back on the main mesh
            { -380.11f, 269.10f, -55.96f },  // 14 the lake's surface
            { -392.72f, 263.30f, -55.96f },  // 15
            { -405.33f, 257.50f, -55.96f },  // 16
            { -417.07f, 248.92f, -55.48f },  // 17
            { -429.49f, 244.16f, -53.73f },  // 18 dry: north of Ghamoo-ra's shore
            { -442.77f, 240.49f, -53.29f },  // 19
            { -456.05f, 236.83f, -53.76f },  // 20
            { -469.33f, 233.17f, -53.63f },  // 21
            { -483.15f, 230.30f, -55.19f },  // 22 the lake west of Ghamoo-ra
            { -496.98f, 229.77f, -55.96f },  // 23
            { -510.81f, 229.24f, -55.96f },  // 24
            { -524.65f, 228.70f, -55.96f },  // 25
            { -538.48f, 228.17f, -55.96f },  // 26
            { -552.31f, 227.64f, -55.96f },  // 27
            { -562.05f, 227.67f, -55.85f },  // 28
            { -576.77f, 229.45f, -54.18f },  // 29 its west shore
            { -589.63f, 225.34f, -51.11f },  // 30
            { -602.48f, 221.22f, -50.63f },  // 31
            { -613.50f, 217.70f, -50.30f },  // 32 the junction by the naga camp
            { -616.60f, 214.13f, -50.84f },  // 33
            { -616.15f, 209.04f, -50.59f },  // 34 turning south
            { -613.79f, 195.54f, -51.62f },  // 35
            { -611.44f, 182.03f, -52.99f },  // 36
            { -609.76f, 172.39f, -54.69f },  // 37
            { -613.31f, 159.50f, -56.02f },  // 38
            { -619.77f, 146.82f, -57.34f },  // 39 a shallow crossing
            { -622.45f, 141.91f, -57.08f },  // 40
            { -626.30f, 128.00f, -56.71f },  // 41
            { -624.99f, 113.92f, -55.43f },  // 42
            { -624.63f, 100.09f, -56.18f },  // 43
            { -624.01f, 96.57f, -55.90f },   // 44
            { -620.59f, 83.53f, -55.28f },   // 45
            { -617.40f, 74.51f, -55.43f },   // 46
            { -612.37f, 60.43f, -55.43f },   // 47 a shallow crossing
            { -610.30f, 45.83f, -54.52f },   // 48
            { -604.57f, 33.58f, -51.88f },   // 49
            { -598.83f, 21.33f, -49.10f },   // 50
            { -589.33f, 13.33f, -46.89f },   // 51
            { -581.13f, 1.94f, -46.65f },    // 52 into the Fallenroot corridor
            { -568.06f, -1.50f, -45.79f },   // 53
            { -554.17f, -1.50f, -43.81f },   // 54
            { -540.28f, -1.50f, -41.11f },   // 55
            { -526.39f, -1.50f, -40.87f },   // 56
            { -512.50f, -1.50f, -44.15f },   // 57
            { -498.60f, -1.50f, -45.18f },   // 58
            { -489.31f, -0.20f, -46.03f },   // 59
            { -478.30f, 9.55f, -45.06f },    // 60
            { -470.81f, 14.43f, -44.89f },   // 61 the cave mouth
            { -464.95f, 16.53f, -46.86f },   // 62
            { -461.38f, 17.86f, -46.89f },   // 63 the cave's shallow sheet
            { -449.08f, 23.22f, -47.15f },   // 64
            { -436.95f, 29.12f, -47.43f },   // 65 by the Blindlight Oracles
            { -424.83f, 35.01f, -47.43f },   // 66 13.5yd short of Gelihast
        });

    // --- Old Serra'kis's pool -> Twilight Lord Kelris -------------------------
    // Kelris (-818.8, -155.6, -25.8) sits on his altar on a platform at z -25.6
    // (the Elementalists and Shadowmages around him stand at -25.8), and water
    // runs under much of it at z -31.4: Old Serra'kis's pool, whose sheet
    // touches no walkable ground, and a strip of the MAIN mesh's water along
    // x -797..-807 that leads to the nearest shore, at (-809, -89), and a step
    // up onto the platform. The party reaches Kelris from Old Serra'kis, under
    // water; the swim fallback then swam straight at Kelris and surfaced under
    // the platform, 6yd below him.
    //
    // Anchor 0 is that strip's end nearest the opening between the two pools
    // (the pool floor runs on through it, (-784.8, -153.6, -43.7) ->
    // (-789.5, -145.6, -40.0)). A party in Serra'kis's pool is 60yd from it, out
    // of the resnap radius of every anchor, so its cursor starts here and it
    // swims to it in a straight line (MoveTo generates no path for a swimming
    // bot). A party already on the platform, or on the shore, is seeded onto the
    // anchors next to it and never enters the water.
    DungeonClearRouteRegistry::Register(BFD_MAP, DUNGEON_DIFFICULTY_NORMAL, BFD_KELRIS,
        {
            { -794.13f, -137.60f, -31.42f },  // 0  main-mesh water by the pools' opening
            { -796.94f, -136.53f, -31.42f },  // 1
            { -802.80f, -127.47f, -31.42f },  // 2  at the surface, along x -803
            { -803.15f, -109.80f, -31.42f },  // 3
            { -802.79f, -104.02f, -31.37f },  // 4
            { -807.52f, -93.05f, -31.34f },   // 5
            { -809.09f, -89.39f, -29.29f },   // 6  the shore
            { -811.68f, -82.50f, -29.29f },   // 7
            { -813.80f, -83.20f, -26.76f },   // 8  up the step
            { -816.37f, -95.20f, -25.56f },   // 9  the platform (z -25.6)
            { -818.52f, -97.21f, -25.75f },   // 10 the narrow way in
            { -818.80f, -104.93f, -25.56f },  // 11
            { -805.70f, -110.49f, -25.56f },  // 12 round the cut in the floor at y -111
            { -803.39f, -113.04f, -25.33f },  // 13
            { -803.30f, -114.73f, -25.32f },  // 14
            { -811.06f, -126.34f, -25.49f },  // 15 past the Shadowmage at (-819.5, -132.7)
            { -814.82f, -139.04f, -25.56f },  // 16 17yd short of Kelris, on his floor
        });

    // --- Kelris -> Aku'mai -----------------------------------------------------
    // Kelris's row again (a party that ended his fight in the pool under the
    // platform is seeded onto its water anchors and leaves by the shore; a party
    // on the platform is seeded next to Kelris's altar), then across it between
    // the four Fires of Aku'mai, through the Portal of Aku'mai (21117 at
    // (-818.4, -200.6)), down the Snapjaws' corridor and into the lair's shallow
    // sheet (z -33.6; Aku'mai stands in it at -33.9). The portal opens on the
    // fires (instance_blackfathom_deeps); this row neither lights them nor waits
    // on the door.
    DungeonClearRouteRegistry::Register(BFD_MAP, DUNGEON_DIFFICULTY_NORMAL, BFD_AKUMAI,
        {
            { -794.13f, -137.60f, -31.42f },  // 0  as Kelris's row, anchors 0-16
            { -796.94f, -136.53f, -31.42f },  // 1
            { -802.80f, -127.47f, -31.42f },  // 2
            { -803.15f, -109.80f, -31.42f },  // 3
            { -802.79f, -104.02f, -31.37f },  // 4
            { -807.52f, -93.05f, -31.34f },   // 5
            { -809.09f, -89.39f, -29.29f },   // 6  the shore
            { -811.68f, -82.50f, -29.29f },   // 7
            { -813.80f, -83.20f, -26.76f },   // 8
            { -816.37f, -95.20f, -25.56f },   // 9  the platform
            { -818.52f, -97.21f, -25.75f },   // 10
            { -818.80f, -104.93f, -25.56f },  // 11
            { -805.70f, -110.49f, -25.56f },  // 12
            { -803.39f, -113.04f, -25.33f },  // 13
            { -803.30f, -114.73f, -25.32f },  // 14
            { -811.06f, -126.34f, -25.49f },  // 15
            { -814.82f, -139.04f, -25.56f },  // 16
            { -817.92f, -151.92f, -25.26f },  // 17
            { -818.80f, -155.60f, -24.55f },  // 18 Kelris's altar, where he dies
            { -821.55f, -161.56f, -23.04f },  // 19 between the Fires of Aku'mai
            { -821.46f, -174.95f, -25.18f },  // 20
            { -820.92f, -188.54f, -25.56f },  // 21
            { -820.37f, -202.13f, -25.56f },  // 22 the Portal of Aku'mai
            { -822.90f, -215.61f, -25.56f },  // 23 the corridor (Snapjaws at z -25.8)
            { -825.43f, -229.09f, -25.56f },  // 24
            { -827.97f, -242.57f, -25.76f },  // 25
            { -830.50f, -256.06f, -25.87f },  // 26
            { -833.96f, -270.05f, -26.09f },  // 27
            { -837.33f, -283.61f, -26.52f },  // 28
            { -840.70f, -297.16f, -28.36f },  // 29 down to the lair
            { -844.07f, -310.72f, -31.21f },  // 30
            { -847.45f, -324.27f, -32.07f },  // 31
            { -849.93f, -337.90f, -32.28f },  // 32
            { -850.18f, -351.70f, -33.26f },  // 33 the lair's shallow sheet
            { -850.43f, -365.50f, -33.22f },  // 34
            { -851.10f, -378.92f, -33.56f },  // 35
            { -853.40f, -393.68f, -29.85f },  // 36 over the rise at y -394
            { -856.32f, -406.65f, -33.43f },  // 37
            { -856.39f, -420.55f, -33.31f },  // 38
            { -855.87f, -423.91f, -32.91f },  // 39
            { -854.64f, -428.47f, -33.56f },  // 40
            { -851.50f, -441.54f, -32.77f },  // 41 12.7yd short of Aku'mai
        });
}
