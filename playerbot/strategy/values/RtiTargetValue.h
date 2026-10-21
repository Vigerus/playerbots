#pragma once
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/Value.h"
#include "Groups/Group.h"
#include "TargetValue.h"
#include "PossibleTargetsValue.h"

namespace ai
{
    class RtiTargetValue : public TargetValue
    {
    public:
        RtiTargetValue(PlayerbotAI* ai, std::string type = "rti", std::string name = "rti target") : type(type), TargetValue(ai,name)
        {}

    public:
        static int GetRtiIndex(const std::string& rti)
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

        // the first icon of the list (priority order) that marks a living possible target in sight; icons that are
        // not set or whose target does not qualify are skipped
        Unit *Calculate()
        {
            Group *group = bot->GetGroup();
            if(!group)
                return NULL;

            std::vector<std::string> rti_list = AI_VALUE(std::vector<std::string>, type);
            if (rti_list.empty())
                return NULL;

            for (const std::string& rti : rti_list)
            {
                int index = GetRtiIndex(rti);
                if (index == -1)
                    continue;

                ObjectGuid guid = group->GetTargetIcon(index);
                if (!guid)
                    continue;

                // the marked unit itself is checked (what "possible targets" accepted): no list to search
                Unit* unit = ai->GetUnit(ObjectGuid(guid));
                if (!unit || sServerFacade.UnitIsDead(unit) ||
                    !bot->IsWithinDistInMap(unit, sPlayerbotAIConfig.sightDistance, false) ||
                    !PossibleTargetsValue::IsValid(unit, bot) || !sServerFacade.IsWithinLOSInMap(bot, unit))
                    continue;

                return unit;
            }

            return nullptr;
        }

    private:
    std::string type;
    };

    class RtiCcTargetValue : public RtiTargetValue
    {
    public:
        RtiCcTargetValue(PlayerbotAI* ai, std::string name = "rti cc target") : RtiTargetValue(ai, "rti cc", name) {}
    };
}
