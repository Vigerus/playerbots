#include "STAI_Task.h"

#include "playerbot/StateTree/STAI_ActivityLog.h"

using namespace ai;

namespace
{
    // ends the run's History entry; an instant success leaves none, everything else gets one
    void EndActivity(const STAI_Task& task, STAI_TaskContext& context, bool successful)
    {
        if (!context.activityOpen)
            return;

        context.activityOpen = false;
        if (!context.activityId)
        {
            if (successful)
                return;
            const std::string info = task.GetDebugInfo();
            context.activityId = context.activity->BeginAt("task", info.empty() ? task.GetName() : info,
                context.nodeName ? *context.nodeName : task.GetName(), context.activityStartMs, task.AreConsecutiveLogsStacked());
        }
        context.activity->End(context.activityId, successful ? 1 : 0);
        context.activityId = 0;
    }
}

void STAI_Task::Enter(STAI_TaskContext& context) const
{
    context.activityId = 0;
    context.activityStartMs = WorldTimer::getMSTime();
    context.activityOpen = context.activity && LogsActivity() && bShowLog;
    OnEnter(context);
}

EActionRunStatus STAI_Task::Tick(STAI_TaskContext& context, uint32 elapsed, bool minimal) const
{
    // a task that is not tickable is done once entered
    const EActionRunStatus status = IsTickable() ? OnTick(context, elapsed, minimal) : EActionRunStatus::Succeeded;
    if (status != EActionRunStatus::Running)
    {
        EndActivity(*this, context, IsSuccessful(context, status));
        return status;
    }

    // still running after an update: the run gets its entry, from the time the task was entered
    if (context.activityOpen && !context.activityId)
    {
        const std::string info = GetDebugInfo();
        context.activityId = context.activity->BeginAt("task", info.empty() ? GetName() : info,
            context.nodeName ? *context.nodeName : GetName(), context.activityStartMs, AreConsecutiveLogsStacked());
    }
    return status;
}

void STAI_Task::Exit(STAI_TaskContext& context) const
{
    // left while running: the task tells whether the run went well (a spell that went off) before it cleans up
    EndActivity(*this, context, IsSuccessful(context, EActionRunStatus::Running));
    OnExit(context);
}
