#include "Task_AcceptResurrection.h"

#include "playerbot/ServerFacade.h"

#include "Entities/Player.h"

using namespace ai;

EActionRunStatus Task_AcceptResurrection::Check(STAI_TaskContext& context) const
{
    Player* bot = context.bot;
    if (sServerFacade.IsAlive(bot))
        return EActionRunStatus::Succeeded;
    if (!bot->isRessurectRequested())
        return EActionRunStatus::Running;

    // as WorldSession::HandleResurrectResponseOpcode does for an accepting player
    StartLog(context);
    bot->ResurrectUsingRequestDataInit();
    return LogStatus(context, EActionRunStatus::Succeeded);
}

EActionRunStatus Task_AcceptResurrection::OnEnterState(STAI_TaskContext& context) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(Task_AcceptResurrection::OnEnterState);
    return Check(context);
}

EActionRunStatus Task_AcceptResurrection::OnTick(STAI_TaskContext& context, uint32 elapsed, bool minimal) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(Task_AcceptResurrection::OnTick);
    return Check(context);
}
