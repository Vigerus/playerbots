
#include "EngineValues.h"


using namespace ai;

bool ActionPossibleValue::Calculate()
{
    Action* action = context->GetAction(getQualifier());

    if (!action)
        return false;

    return action->isPossible();
}

bool ActionUsefulValue::Calculate()
{
    Action* action = context->GetAction(getQualifier());

    if (!action)
        return false;

    return action->isUseful();
}

bool TriggerActiveValue::Calculate()
{
    Trigger* trigger = context->GetTrigger(getQualifier());

    if (!trigger)
        return false;

    Event event = trigger->Check();

    return event.IsValid();
}

