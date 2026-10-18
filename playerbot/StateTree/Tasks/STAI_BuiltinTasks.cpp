#include "playerbot/StateTree/STAI_TaskFactory.h"

#include "playerbot/StateTree/Tasks/STAI_FinishTreeTask.h"
#include "playerbot/StateTree/Tasks/STAI_GlobalTask.h"
#include "playerbot/StateTree/Tasks/StateNode_Legacy.h"
#include "playerbot/StateTree/Tasks/Task_Follow.h"
#include "playerbot/StateTree/Tasks/Task_RunSubtree.h"

namespace ai
{
    void RegisterBuiltinStateTreeTasks(STAI_TaskFactory& factory)
    {
        factory.RegisterTask("StateNode_Legacy", { &StateNode_Legacy::Validate, &StateNode_Legacy::Create });
        factory.RegisterTask("StateNode_Reaction", { &StateNode_Legacy::Validate, &StateNode_Reaction::Create });
        factory.RegisterTask("Task_Follow", { &Task_Follow::Validate, &Task_Follow::Create });
        factory.RegisterTask("STAI_FinishTreeTask", { &STAI_FinishTreeTask::Validate, &STAI_FinishTreeTask::Create });
        factory.RegisterTask("STAI_GlobalTask", { &STAI_GlobalTask::Validate, &STAI_GlobalTask::Create });
        factory.RegisterTask("Task_RunSubtree", { &Task_RunSubtree::Validate, &Task_RunSubtree::Create });
    }
}
