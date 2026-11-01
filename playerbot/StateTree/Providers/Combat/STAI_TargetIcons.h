#pragma once

#include "playerbot/ChatHelper.h"
#include "playerbot/ServerFacade.h"

#include "Entities/Player.h"

#include <string>
#include <vector>

namespace ai
{
    // the bot's kill order and crowd control icons are PlayerbotAI::GetTargetIcons() (PlayerbotTargetIcons, the "rti"
    // chat command)

    // index of a raid target icon (Group::GetTargetIcon), -1 for an unknown name
    inline int STAI_GetRtiIndex(const std::string& rti)
    {
        int index = -1;
        if(rti == "star") index = 0;
        else if(rti == "circle") index = 1;
        else if(rti == "diamond") index = 2;
        else if(rti == "triangle") index = 3;
        else if(rti == "moon") index = 4;
        else if(rti == "square") index = 5;
        else if(rti == "cross") index = 6;
        else if(rti == "skull") index = 7;
        return index;
    }

    // crowd control that damage breaks (polymorph, frost nova, sap, gouge, shackle undead)
    inline bool STAI_HasBreakableCC(Unit* target, Player* player)
    {
        if (target->IsPolymorphed())
            return true;

        if (sServerFacade.IsFrozen(target))
            return true;

        // sap, gouge, shackle undead: their spell ids (every rank) looked up once, checked against the unit's auras
        static const std::vector<uint32> breakableCCSpells = []()
        {
            std::vector<uint32> spells;
            for (const char* name : { "sap", "gouge", "shackle undead" })
            {
                const std::vector<uint32> ids = ChatHelper::SpellIds(name);
                spells.insert(spells.end(), ids.begin(), ids.end());
            }
            return spells;
        }();

        for (uint32 spellId : breakableCCSpells)
            if (target->HasAura(spellId))
                return true;

        return false;
    }

    // crowd control that damage does not break (stun, fear, roots)
    inline bool STAI_HasUnBreakableCC(Unit* target, Player* player)
    {
        return target->IsStunned() || sServerFacade.IsFeared(target) || sServerFacade.IsInRoots(target);
    }
}
