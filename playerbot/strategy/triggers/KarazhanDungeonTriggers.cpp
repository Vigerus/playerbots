
#include "playerbot/playerbot.h"
#include "KarazhanDungeonTriggers.h"
#include "GenericTriggers.h"
#include "Grids/GridNotifiers.h"
#include "Grids/GridNotifiersImpl.h"
#include "Grids/CellImpl.h"
#include "playerbot/strategy/values/VisibleObjects.h"

using namespace ai;

bool NetherspiteBeamsCheatNeedRefreshTrigger::IsActive()
{
    //Checking that is portal phase
    // the creatures visible to the bot (kept by the core on visibility changes), not a grid search
    std::list<Unit*> creatures;
    MaNGOS::AllCreaturesOfEntryInRangeCheck u_check(bot, 17369, 100);
    VisibleObjects::FindUnits(bot, 100, u_check, creatures);

    if (creatures.empty())
        return false;

    //Checking that is Netherspite target
    return AI_VALUE2(bool, "has aggro", "current target");
}