#pragma once

#include "playerbot/StateTree/STAI_TaskFactory.h"

#include "Entities/ObjectGuid.h"

#include <vector>

struct SpellEntry;

namespace ai
{
    // why a unit needs a cast: the harmful aura on it a dispel removes, the spell it casts an interrupt stops
    // (DispelProvider_GlobalTask / InterruptProvider_GlobalTask::causes, bound into Task_CastSpell::causes, which shows
    // the cause of its cast in the debug view)
    struct STAI_SpellCause
    {
        ObjectGuid target;
        uint32 spellId = 0;
        uint32 dispel = 0;      // the aura's DISPEL_* type, 0 for casts
    };
    using STAI_SpellCauses = std::vector<STAI_SpellCause>;

    // which spells a provider reacts to (any rank), one of:
    //   <Observe spellid="8399,12345"/>   only these; empty (spellid="") reacts to none
    //   <Ignore spellid="8399,12345"/>    all but these; empty reacts to all
    //   neither                           all
    struct STAI_SpellFilter
    {
        // throws on both <Observe> and <Ignore>, on a second one of them, or on an unknown id
        static STAI_SpellFilter Parse(const STAI_TaskConfig& config);

        bool Allows(uint32 spellId) const;

        bool observe = false;       // spells is the <Observe> list, else the <Ignore> list
        std::vector<uint32> spells; // first ranks
    };
    // the dispel types the spell removes (1 << DISPEL_*): its dispel effects, and those of a spell its aura triggers
    // (Abolish Disease, Abolish Poison); 0 when it dispels nothing
    uint32 STAI_GetDispelMask(const SpellEntry* spellInfo);
}
