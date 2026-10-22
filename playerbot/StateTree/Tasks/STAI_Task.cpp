#include "STAI_Task.h"

#include "playerbot/StateTree/STAI_ActivityLog.h"

using namespace ai;

std::string STAI_Task::task_name = "Task";

namespace
{
    // the run's History entry, from the time the run started
    void BeginActivity(const STAI_Task& task, STAI_TaskContext& context)
    {
        const std::string info = task.GetDebugInfo();
        context.activityId = context.activity->BeginAt("task", info.empty() ? task.GetName() : info, task.GetName(),
            context.activityStartMs, task.AreConsecutiveLogsStacked());
    }
}

void STAI_Task::StartLog(STAI_TaskContext& context) const
{
    context.activityId = 0;
    context.activityStartMs = WorldTimer::getMSTime();
    context.activityOpen = context.activity && LogsActivity();
}

EActionRunStatus STAI_Task::LogStatus(STAI_TaskContext& context, EActionRunStatus status) const
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

void STAI_Task::EndLog(STAI_TaskContext& context, bool successful) const
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
