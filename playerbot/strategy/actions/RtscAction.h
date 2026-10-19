#pragma once

#include "SeeSpellAction.h"
#include "playerbot/strategy/values/RTSCValues.h"

namespace ai
{    
    #define RTSC_MOVE_SPELL 30758 //Aedm (Awesome Energetic do move)

    class RTSCAction : public SeeSpellAction, public Qualified
    {
    public:
        RTSCAction(PlayerbotAI* ai) : SeeSpellAction(ai, "rtsc"), Qualified() {}
        virtual bool Execute(Event& event);
        // the command itself enables the rtsc strategy, so don't require it here
        virtual bool isUseful() override { return GetMaster(); }
    };
}
