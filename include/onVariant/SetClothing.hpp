#pragma once

namespace on
{
    /* @param sound play the "change clothes" sound (when the player equips or takes off something)
       @param everyone also send the clothes to everyone else in the world */
    extern void SetClothing(ENetPeer *peer, bool sound = false, bool everyone = false);
}
