#include "playerbot/StateTree/STAI_TaskFactory.h"

#include "playerbot/StateTree/Providers/BuffProvider_GlobalTask.h"
#include "playerbot/StateTree/Providers/CCProvider_GlobalTask.h"
#include "playerbot/StateTree/Providers/CooldownProvider_GlobalTask.h"
#include "playerbot/StateTree/Providers/CurseProvider_GlobalTask.h"
#include "playerbot/StateTree/Providers/HazardProvider_GlobalTask.h"
#include "playerbot/StateTree/Providers/InstanceProvider_GlobalTask.h"
#include "playerbot/StateTree/Providers/InterruptProvider_GlobalTask.h"
#include "playerbot/StateTree/Providers/UIInteractionProvider_GlobalTask.h"
#include "playerbot/StateTree/Providers/MasterDistance_GlobalTask.h"
#include "playerbot/StateTree/Providers/ResourceProvider_GlobalTask.h"
#include "playerbot/StateTree/Providers/SpecProvider_GlobalTask.h"
#include "playerbot/StateTree/Providers/TargetProvider_GlobalTask.h"
#include "playerbot/StateTree/Tasks/STAI_FinishTreeTask.h"
#include "playerbot/StateTree/Tasks/STAI_GlobalTask.h"
#include "playerbot/StateTree/Tasks/Task_CastBuff.h"
#include "playerbot/StateTree/Tasks/Task_CastSpell.h"
#include "playerbot/StateTree/Tasks/Task_EatAndDrink.h"
#include "playerbot/StateTree/Tasks/Task_MakePath.h"
#include "playerbot/StateTree/Tasks/Task_MoveTo.h"
#include "playerbot/StateTree/Tasks/Task_MoveToPosition.h"
#include "playerbot/StateTree/Tasks/Task_GetInRange.h"
#include "playerbot/StateTree/Tasks/Task_RollForItem.h"
#include "playerbot/StateTree/Tasks/Task_RunSubtree.h"
#include "playerbot/StateTree/Tasks/Task_Shoot.h"
#include "playerbot/StateTree/Tasks/Task_Trade.h"
#include "playerbot/StateTree/Tasks/Task_UseItem.h"
#include "playerbot/StateTree/Tasks/Task_Wait.h"

namespace ai
{
    void RegisterBuiltinStateTreeTasks(STAI_TaskFactory& factory)
    {
        factory.RegisterTask("Task_MakePath", { &Task_MakePath::Validate, &Task_MakePath::Create });
        factory.RegisterTask("Task_MoveTo", { &Task_MoveTo::Validate, &Task_MoveTo::Create });
        factory.RegisterTask("Task_MoveToPosition", { &Task_MoveToPosition::Validate, &Task_MoveToPosition::Create });
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
        factory.RegisterTask("UIInteractionProvider_GlobalTask", { &UIInteractionProvider_GlobalTask::Validate, &UIInteractionProvider_GlobalTask::Create });
        factory.RegisterTask("Task_RollForItem", { &Task_RollForItem::Validate, &Task_RollForItem::Create });
        factory.RegisterTask("Task_OpenTrade", { &Task_OpenTrade::Validate, &Task_OpenTrade::Create });
        factory.RegisterTask("Task_AcceptTrade", { &Task_AcceptTrade::Validate, &Task_AcceptTrade::Create });

        factory.RegisterTask("Task_Wait", { &Task_Wait::Validate, &Task_Wait::Create });
        factory.RegisterTask("CCProvider_GlobalTask", { &CCProvider_GlobalTask::Validate, &CCProvider_GlobalTask::Create });
        factory.RegisterTask("TargetProvider_GlobalTask", { &TargetProvider_GlobalTask::Validate, &TargetProvider_GlobalTask::Create });
        factory.RegisterTask("Task_GetInRange", { &Task_GetInRange::Validate, &Task_GetInRange::Create });
        factory.RegisterTask("InterruptProvider_GlobalTask", { &InterruptProvider_GlobalTask::Validate, &InterruptProvider_GlobalTask::Create });
        factory.RegisterTask("CooldownProvider_GlobalTask", { &CooldownProvider_GlobalTask::Validate, &CooldownProvider_GlobalTask::Create });
        factory.RegisterTask("SpecProvider_GlobalTask", { &SpecProvider_GlobalTask::Validate, &SpecProvider_GlobalTask::Create });
        factory.RegisterTask("HazardProvider_GlobalTask", { &HazardProvider_GlobalTask::Validate, &HazardProvider_GlobalTask::Create });
        factory.RegisterTask("Task_UseItem", { &Task_UseItem::Validate, &Task_UseItem::Create });
        factory.RegisterTask("Task_Shoot", { &Task_Shoot::Validate, &Task_Shoot::Create });
    }
}
