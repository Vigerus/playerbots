#include "STAI_SendEventTask.h"

#include "playerbot/StateTree/STAI_TreeInstance.h"

#include <stdexcept>

using namespace ai;

void STAI_SendEventTask::Validate(const STAI_TaskConfig& config)
{
    if (config.get<std::string>("<xmlattr>.event", "").empty())
        throw std::runtime_error("missing event=\"...\" on the <Task>");
}

std::unique_ptr<STAI_TaskBase> STAI_SendEventTask::Create(const STAI_TaskConfig& config)
{
    return std::make_unique<STAI_SendEventTask>(config.get<std::string>("<xmlattr>.event", ""));
}

EActionRunStatus STAI_SendEventTask::OnEnterState(STAI_TaskContext& context) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(STAI_SendEventTask::OnEnterState);
    if (context.instance)
        context.instance->SendEvent(event);
    return EActionRunStatus::Succeeded;
}
