#include "STAI_Task.h"

#include "playerbot/StateTree/STAI_ActivityLog.h"

using namespace ai;

std::string STAI_TaskBase::task_name = "Task";

namespace
{
    // the run's History entry, from the time the run started
    void BeginActivity(const STAI_TaskBase& task, STAI_TaskContext& context)
    {
        const std::string info = task.GetActivityInfo(context);
        context.activityId = context.activity->BeginAt("task", info.empty() ? task.GetName() : info, task.GetName(),
            context.activityStartMs, task.AreConsecutiveLogsStacked());
    }

    // the result as the completion of the task's state: Running while the completion ignores it
    EActionRunStatus ApplyCompletion(ESTAI_TaskCompletion completion, EActionRunStatus status)
    {
        switch (completion)
        {
            case ESTAI_TaskCompletion::AlwaysFinish:
                return status;
            case ESTAI_TaskCompletion::FinishWhenSucceeded:
                return status == EActionRunStatus::Succeeded ? status : EActionRunStatus::Running;
            case ESTAI_TaskCompletion::FinishWhenFailed:
                return status == EActionRunStatus::Failed ? status : EActionRunStatus::Running;
            default:
                return EActionRunStatus::Running;
        }
    }
}

EActionRunStatus STAI_TaskBase::EnterState(STAI_TaskContext& context) const
{
    context.bFinishTree = false;
    context.bTaskActive = true;
    const EActionRunStatus status = OnEnterState(context);
    context.bTaskActive = status == EActionRunStatus::Running;
    return ApplyCompletion(GetCompletion(), status);
}

EActionRunStatus STAI_TaskBase::TickState(STAI_TaskContext& context, uint32 elapsed, bool minimal) const
{
    // a finished task whose result its state ignored stays finished until the state is left
    if (!context.bTaskActive)
        return EActionRunStatus::Running;

    const EActionRunStatus status = OnTick(context, elapsed, minimal);
    context.bTaskActive = status == EActionRunStatus::Running;
    return ApplyCompletion(GetCompletion(), status);
}

void STAI_TaskBase::ExitState(STAI_TaskContext& context) const
{
    OnExitState(context);
    context.bTaskActive = false;
}

void STAI_TaskBase::StateCompleted(STAI_TaskContext& context, EActionRunStatus status) const
{
    OnStateCompleted(context, status);
}

void STAI_TaskBase::StartLog(STAI_TaskContext& context) const
{
    context.activityId = 0;
    context.activityStartMs = WorldTimer::getMSTime();
    context.activityOpen = context.activity && LogsActivity();
}

EActionRunStatus STAI_TaskBase::LogStatus(STAI_TaskContext& context, EActionRunStatus status) const
{
    if (status == EActionRunStatus::Running)
    {
        // still running: the run gets its entry
        if (context.activityOpen && !context.activityId)
            BeginActivity(*this, context);
        return status;
    }

    EndLog(context, status == EActionRunStatus::Succeeded);
    return status;
}

void STAI_TaskBase::EndLog(STAI_TaskContext& context, bool successful) const
{
    if (!context.activityOpen)
        return;

    context.activityOpen = false;
    // an instant success leaves no entry, everything else gets one
    if (!context.activityId)
    {
        if (successful)
            return;
        BeginActivity(*this, context);
    }
    context.activity->End(context.activityId, successful ? 1 : 0);
    context.activityId = 0;
}
