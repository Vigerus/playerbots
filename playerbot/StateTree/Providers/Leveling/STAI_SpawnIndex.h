#pragma once

#include "Common.h"

#include <vector>

namespace ai
{
    // a creature spawn of the creature table
    struct STAI_Spawn
    {
        uint32 entry;
        float x, y, z;
    };

    // every creature spawn of a map (spawn group members without an entry left out), built for all maps on first use
    // and read only afterwards, so any map thread may read it; empty for a map without spawns
    const std::vector<STAI_Spawn>& STAI_GetSpawns(uint32 mapId);
}
