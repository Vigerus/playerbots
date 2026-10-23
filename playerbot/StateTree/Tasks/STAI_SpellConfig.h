#pragma once

#include "playerbot/StateTree/STAI_TaskFactory.h"

#include <string>
#include <vector>

class Player;

namespace ai
{
    // A spell named in a task's config, spellid="..." on the <Task>: a spell id, or a spell name (any case) that
    // stands for the bot's highest known rank of it:
    //   <Task class="Task_CastSpell" spellid="Frostbolt"/>
    //   <Task class="InterruptProvider_GlobalTask" spellid="2139"/>
    struct STAI_ConfiguredSpell
    {
        uint32 spellId = 0;                 // the configured spell id, or the first spell of the configured name
        std::string spellName;              // lower case, empty when configured by id: by name the bot's highest rank is used
        std::vector<uint32> namedSpells;    // by name: every spell of that name, highest rank first (found once at load)

        // throws std::runtime_error without spellid, for an unknown id or a name no spell has
        static STAI_ConfiguredSpell Parse(const STAI_TaskConfig& config);

        // the spell the bot uses: the configured id when it knows it, else its highest rank of the configured name; 0
        // when it knows none
        uint32 Resolve(const Player* bot) const;
    };
}
