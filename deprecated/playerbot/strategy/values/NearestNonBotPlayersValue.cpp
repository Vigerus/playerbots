
#include "playerbot/playerbot.h"
#include "NearestNonBotPlayersValue.h"

#include "Grids/GridNotifiers.h"
#include "Grids/GridNotifiersImpl.h"
#include "Grids/CellImpl.h"

using namespace ai;
using namespace MaNGOS;

void NearestNonBotPlayersValue::FindUnits(std::list<Unit*> &targets)
{
    // the units visible to the bot (kept by the core on visibility changes), not a grid search
    AnyUnitInObjectRangeCheck u_check(bot, range);
    VisibleObjects::FindUnits(bot, range, u_check, targets, true);
}

bool NearestNonBotPlayersValue::AcceptUnit(Unit* unit)
{
    ObjectGuid guid = unit->GetObjectGuid();
#ifdef MANGOS
    return guid.IsPlayer() && !((Player*)unit)->GetPlayerbotAI() && (!((Player*)unit)->isGameMaster() || ((Player*)unit)->isGMVisible());
#endif
#ifdef CMANGOS
    return guid.IsPlayer() && !((Player*)unit)->GetPlayerbotAI() && (!((Player*)unit)->IsGameMaster() || ((Player*)unit)->isGMVisible());
#endif
}
