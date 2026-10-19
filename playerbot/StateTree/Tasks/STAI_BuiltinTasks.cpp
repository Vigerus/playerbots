#include "playerbot/StateTree/STAI_TaskFactory.h"

#include "playerbot/StateTree/Providers/BuffProvider_GlobalTask.h"
#include "playerbot/StateTree/Providers/CurseProvider_GlobalTask.h"
#include "playerbot/StateTree/Providers/InstanceProvider_GlobalTask.h"
#include "playerbot/StateTree/Providers/MasterDistance_GlobalTask.h"
#include "playerbot/StateTree/Providers/ResourceProvider_GlobalTask.h"
#include "playerbot/StateTree/Tasks/STAI_FinishTreeTask.h"
#include "playerbot/StateTree/Tasks/STAI_GlobalTask.h"
#include "playerbot/StateTree/Tasks/StateNode_Legacy.h"
#include "playerbot/StateTree/Tasks/Task_CastBuff.h"
#include "playerbot/StateTree/Tasks/Task_CastSpell.h"
#include "playerbot/StateTree/Tasks/Task_EatAndDrink.h"
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
        factory.RegisterTask("BuffProvider_GlobalTask", { &BuffProvider_GlobalTask::Validate, &BuffProvider_GlobalTask::Create });
        factory.RegisterTask("Task_CastBuff", { &Task_CastBuff::Validate, &Task_CastBuff::Create });
        factory.RegisterTask("MasterDistance_GlobalTask", { &MasterDistance_GlobalTask::Validate, &MasterDistance_GlobalTask::Create });
        factory.RegisterTask("CurseProvider_GlobalTask", { &CurseProvider_GlobalTask::Validate, &CurseProvider_GlobalTask::Create });
        factory.RegisterTask("Task_CastSpell", { &Task_CastSpell::Validate, &Task_CastSpell::Create });
        factory.RegisterTask("HealthProvider_GlobalTask", { &HealthProvider_GlobalTask::Validate, &HealthProvider_GlobalTask::Create });
        factory.RegisterTask("ManaProvider_GlobalTask", { &ManaProvider_GlobalTask::Validate, &ManaProvider_GlobalTask::Create });
        factory.RegisterTask("Task_EatAndDrink", { &Task_EatAndDrink::Validate, &Task_EatAndDrink::Create });
        factory.RegisterTask("InstanceProvider_GlobalTask", { &InstanceProvider_GlobalTask::Validate, &InstanceProvider_GlobalTask::Create });
    }
}
