#pragma once

#include "Platform/Define.h"

#include <vector>

namespace ai
{
    struct STAI_TaskContext;

    // Optional debug interface of a task, next to STAI_Task (which knows nothing about spells): the spells the task
    // works with (configured or being cast), shown as icons by the debug addon. The tree finds it by dynamic_cast.
    class ISTAI_DebugSpells
    {
    public:
        virtual ~ISTAI_DebugSpells() = default;

        virtual std::vector<uint32> GetDebugSpells(const STAI_TaskContext& context) const = 0;
    };
}
