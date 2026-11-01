#include "Task_Attack.h"

#include "playerbot/PlayerbotAI.h"
#include "playerbot/ServerFacade.h"

#include "Entities/Player.h"
#include "Maps/Map.h"

using namespace ai;

EActionRunStatus Task_StartAttack::OnEnterState(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(Task_StartAttack::OnEnterState);
    Task_StartAttackContext& context = ContextAs<Task_StartAttackContext>(baseContext);
    Player* bot = context.bot;
    Unit* target = !context.target.IsEmpty() && bot->IsInWorld() ? bot->GetMap()->GetUnit(context.target) : nullptr;
    if (!target || !target->IsInWorld() || !sServerFacade.IsAlive(target) || !sServerFacade.IsHostileTo(bot, target) || !sServerFacade.IsAlive(bot))
        return EActionRunStatus::Failed;
    if (bot->GetVictim() == target)
        return EActionRunStatus::Succeeded;

    StartLog(context);
    if (bot->IsSitState())
        bot->SetStandState(UNIT_STAND_STATE_STAND);
    bot->SetSelectionGuid(target->GetObjectGuid());
    bot->Attack(target, true);
    return LogStatus(context, bot->GetVictim() == target ? EActionRunStatus::Succeeded : EActionRunStatus::Failed);
}

const std::vector<STAI_PropertyDef>& Task_StartAttack::GetProperties() const
{
    static const std::vector<STAI_PropertyDef> properties = { STAI_PropertyDef::Of("target", &Task_StartAttackContext::target) };
    return properties;
}

EActionRunStatus Task_StopAttack::OnEnterState(STAI_TaskContext& context) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(Task_StopAttack::OnEnterState);
    Player* bot = context.bot;
    if (!bot->GetVictim())
        return EActionRunStatus::Succeeded;

    StartLog(context);
    bot->AttackStop();
    return LogStatus(context, EActionRunStatus::Succeeded);
}
