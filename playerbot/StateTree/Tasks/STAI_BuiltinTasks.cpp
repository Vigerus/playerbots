#include "playerbot/StateTree/STAI_TaskFactory.h"

#include "playerbot/StateTree/Tasks/StateNode_Legacy.h"
#include "playerbot/StateTree/Tasks/Task_Follow.h"

namespace ai
{
    void RegisterBuiltinStateTreeTasks(STAI_TaskFactory& factory)
    {
        factory.RegisterTask("StateNode_Legacy", { &StateNode_Legacy::Validate, &StateNode_Legacy::Create });
        factory.RegisterTask("StateNode_Reaction", { &StateNode_Legacy::Validate, &StateNode_Reaction::Create });
        factory.RegisterTask("Task_Follow", { &Task_Follow::Validate, &Task_Follow::Create });
    }
}
