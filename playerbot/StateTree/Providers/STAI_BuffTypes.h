#pragma once

#include "Entities/ObjectGuid.h"

namespace ai
{
    // who a buff spell lands on, from its implicit targets (or scope="..." in the config)
    enum class BuffScope : uint8 { Self, Single, Group };

    // a buff cast the bot should do: the highest rank it knows of the spell, on target
    // (BuffProvider_GlobalTask::needs, bound into Task_CastBuff::needs)
    struct BuffNeed
    {
        ObjectGuid target;
        uint32 spellId = 0;
        BuffScope scope = BuffScope::Single;
        uint32 missingCount = 0;    // members the cast covers that miss the buff
    };
}
