
#include "playerbot/playerbot.h"
#include "NearestFriendlyPlayersValue.h"

#include "Grids/GridNotifiers.h"
#include "Grids/GridNotifiersImpl.h"
#include "Grids/CellImpl.h"

using namespace ai;
using namespace MaNGOS;

void NearestFriendlyPlayersValue::FindUnits(std::list<Unit*> &targets)
{
    // the players visible to the bot (kept by the core on visibility changes), not a grid search
    AnyFriendlyUnitInObjectRangeCheck u_check(bot, range);
    VisibleObjects::FindUnits(bot, range, u_check, targets, true);
}

bool NearestFriendlyPlayersValue::AcceptUnit(Unit* unit)
{
    ObjectGuid guid = unit->GetObjectGuid();
    return guid.IsPlayer() && guid != ai->GetBot()->GetObjectGuid();
}
