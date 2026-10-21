
#include "playerbot/playerbot.h"
#include "NearestNpcsValue.h"

#include "playerbot/ServerFacade.h"
#include "Grids/GridNotifiers.h"
#include "Grids/GridNotifiersImpl.h"
#include "Grids/CellImpl.h"
#ifdef MANGOSBOT_TWO
#include "Entities/Vehicle.h"
#endif

using namespace ai;
using namespace MaNGOS;

void NearestNpcsValue::FindUnits(std::list<Unit*> &targets)
{
    // the units visible to the bot (kept by the core on visibility changes), not a grid search
    AnyUnitInObjectRangeCheck u_check(bot, range);
    VisibleObjects::FindUnits(bot, range, u_check, targets);
}

bool NearestNpcsValue::AcceptUnit(Unit* unit)
{
    return !sServerFacade.IsHostileTo(unit, bot) && !dynamic_cast<Player*>(unit);
}

void NearestVehiclesValue::FindUnits(std::list<Unit*>& targets)
{
    // the units visible to the bot (kept by the core on visibility changes), not a grid search
    AnyUnitInObjectRangeCheck u_check(bot, range);
    VisibleObjects::FindUnits(bot, range, u_check, targets);
}

bool NearestVehiclesValue::AcceptUnit(Unit* unit)
{
#ifdef MANGOSBOT_TWO
    if (!unit || !unit->IsVehicle() || !unit->IsAlive())
        return false;

    VehicleInfo* veh = unit->GetVehicleInfo();
    if (!veh->CanBoard(bot))
        return false;

    return true;
#endif

    return false;
}
