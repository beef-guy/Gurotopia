#pragma once

namespace on
{
    /* @param sound play the "change clothes" sound (when the player equips or takes off something) */
    extern void SetClothing(ENetPeer *peer, bool sound = false);
}
