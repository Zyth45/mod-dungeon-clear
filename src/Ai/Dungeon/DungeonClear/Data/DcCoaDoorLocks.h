#ifndef _PLAYERBOT_DC_COA_DOOR_LOCKS_H
#define _PLAYERBOT_DC_COA_DOOR_LOCKS_H

#include "Define.h"

// The CoA world database ships these door templates with lock 0 where stock
// AzerothCore has a lock (85/86 are the empty "open me" locks, the others need
// a key). The door policy keys off the lock, so on CoA it read every one of
// them as a script-only door and parked the run. This hands back the stock
// lock id, generated from data/sql/base/db_world/gameobject_template.sql.
namespace DcCoaDoorLocks
{
    inline uint32 StockLockId(uint32 goEntry)
    {
        switch (goEntry)
        {
            case 13965: return 85;  // Factory Door
            case 16397: return 202;  // Iron Clad Door
            case 16399: return 85;  // Foundry Door
            case 16400: return 85;  // Mast Room Door
            case 17153: return 86;  // Heavy Door
            case 17154: return 86;  // Heavy Door
            case 18895: return 85;  // Courtyard Door
            case 18934: return 85;  // Cell Door
            case 18935: return 85;  // Cell Door
            case 18936: return 85;  // Cell Door
            case 18971: return 85;  // Arugal's Lair
            case 18972: return 85;  // Sorcerer's Gate
            case 90858: return 92;  // Workshop Door
            case 97700: return 85;  // Secret Door
            case 101854: return 299;  // Herod's Door
            case 104591: return 299;  // Chapel Door
            case 104600: return 85;  // High Inquisitor's Door
            case 124367: return 85;  // Temple Door
            case 124368: return 86;  // Temple Door
            case 124369: return 85;  // The Ancient Vault
            case 124370: return 86;  // Echomok Door
            case 124371: return 359;  // Keystone
            case 141869: return 86;  // Temple Door
            case 142207: return 86;  // The Final Chamber
            case 157816: return 86;  // Holding Pen
            case 157817: return 86;  // Holding Pen
            case 157818: return 86;  // Holding Pen
            case 157819: return 86;  // Holding Pen
            case 157820: return 86;  // Holding Pen
            case 161460: return 680;  // The Shadowforge Lock
            case 170558: return 680;  // The Lyceum
            case 170559: return 680;  // Shadowforge Gate
            case 170560: return 680;  // Shadowforge Gate
            case 170562: return 699;  // Cell Door
            case 170563: return 699;  // Cell Door
            case 170564: return 699;  // Cell Door
            case 170565: return 699;  // Cell Door
            case 170566: return 699;  // Cell Door
            case 170567: return 699;  // Cell Door
            case 170568: return 699;  // Cell Door
            case 170569: return 699;  // Cell Door
            case 170570: return 680;  // East Garrison Door
            case 170571: return 739;  // Bar Door
            case 170573: return 85;  // Golem Room North
            case 170574: return 85;  // Golem Room South
            case 170575: return 85;  // Thone Room Doors
            case 170576: return 85;  // Doodad_BlackRockDoors02
            case 170577: return 85;  // Doodad_BlackRockDoors03
            case 174554: return 639;  // Relic Coffer Door
            case 174555: return 639;  // Relic Coffer Door
            case 174556: return 639;  // Relic Coffer Door
            case 174557: return 639;  // Relic Coffer Door
            case 174558: return 639;  // Relic Coffer Door
            case 174559: return 639;  // Relic Coffer Door
            case 174560: return 639;  // Relic Coffer Door
            case 174561: return 639;  // Relic Coffer Door
            case 174562: return 639;  // Relic Coffer Door
            case 174563: return 639;  // Relic Coffer Door
            case 174564: return 639;  // Relic Coffer Door
            case 174565: return 659;  // Dark Coffer
            case 174566: return 639;  // Relic Coffer Door
            case 175167: return 1199;  // Viewing Room Door
            case 175352: return 879;  // King's Square Gate
            case 175353: return 879;  // King's Square Gate
            case 175356: return 879;  // Gauntlet Gate
            case 175357: return 879;  // Gauntlet Gate
            case 175368: return 879;  // Service Entrance Gate
            case 175967: return 1634;  // The Bastion Door
            case 175968: return 1634;  // Hoard Door
            case 176194: return 1634;  // Hall of the High Command
            case 177217: return 1564;  // Gordok Inner Door
            case 177219: return 1563;  // Gordok Courtyard Door
            case 177221: return 1562;  // Door
            case 179549: return 1562;  // Door
            case 179550: return 1562;  // Door
            default: return 0;
        }
    }
}

#endif
