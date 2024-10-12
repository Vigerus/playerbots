
#include "playerbot/playerbot.h"
#include "playerbot/strategy/Action.h"
#include "playerbot/strategy/values/PositionValue.h"
#include "RtscTriggers.h"

using namespace ai;

bool RtscCloseTrigger::IsActive()
{
   std::string locationName = getQualifier();

   if (!locationName.empty())
   {
      WorldPosition spellPosition = AI_VALUE2(WorldPosition, "RTSC saved location", locationName);

      if (Unit* unit = ai->GetBot())
      {
         return unit->GetDistance(spellPosition.coord_x, spellPosition.coord_y, spellPosition.coord_z) < INTERACTION_DISTANCE;
      }
   }

    return false;
}

