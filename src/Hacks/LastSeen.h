#pragma once

#include <string>

/* Enemy hero last-seen positions, pushed to the Panorama minimap overlay */
namespace LastSeen
{
    void Tick();
    // "{index:[x,y,z],...}" for the heroes of the last units push, re-read on every call: their rows follow them each frame
    std::string Positions();
    // An enemy hero (entity index) channels a teleport to (x, y, z): a marker shows the destination while it channels;
    // once it arrives the destination becomes the hero's last-seen position
    void OnTeleport( int entityIndex, float x, float y, float z, long startMs, float channelSec );
    void OnTeleportEnd( int entityIndex, bool completed );
}
