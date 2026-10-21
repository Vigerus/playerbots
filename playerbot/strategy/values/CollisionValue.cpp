
#include "playerbot/playerbot.h"
#include "CollisionValue.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"

#include "Grids/GridNotifiers.h"
#include "Grids/GridNotifiersImpl.h"
#include "Grids/CellImpl.h"
#include "VisibleObjects.h"

using namespace ai;

bool CollisionValue::Calculate()
{
    Unit* target = AI_VALUE(Unit*, qualifier);
    if (!target)
        return false;

    std::list<Unit*> targets;
    float range = sPlayerbotAIConfig.contactDistance;
    // the units visible to the bot (kept by the core on visibility changes), not a grid search
    MaNGOS::AnyUnitInObjectRangeCheck u_check(bot, range);
    VisibleObjects::FindUnits(bot, range, u_check, targets);

    for (std::list<Unit*>::iterator i = targets.begin(); i != targets.end(); ++i)
    {
        Unit* target = *i;
        if (bot == target) continue;

        if (!target->isVisibleFor(bot, bot))
            continue;

        float dist = sServerFacade.GetDistance2d(bot, target->GetPositionX(), target->GetPositionY());
        if (sServerFacade.IsDistanceLessThan(dist, target->GetObjectBoundingRadius())) return true;
    }

    return false;
}
