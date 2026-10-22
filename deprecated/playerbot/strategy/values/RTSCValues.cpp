
#include "playerbot/playerbot.h"
#include "RTSCValues.h"

using namespace ai;

bool RtscIsPlayerCloseValue::Calculate()
{
   std::string locationName = getQualifier();

   if (!locationName.empty())
   {
      WorldPosition spellPosition = AI_VALUE2(WorldPosition, "RTSC saved location", locationName);

      if (Unit* unit = ai->GetBot())
      {
         const float distance = unit->GetDistance(spellPosition.coord_x, spellPosition.coord_y, spellPosition.coord_z);
         return distance <= INTERACTION_DISTANCE;
      }
   }

   return false;
}


