#include "Task_Wait.h"

#include "playerbot/StateTree/STAI_TaskFactory.h"

#include <cstdlib>
#include <sstream>
#include <stdexcept>

using namespace ai;

namespace
{
    // <Delay value="..."/>, ms, default 0 (forever)
    uint32 ParseDelay(const STAI_TaskConfig& config)
    {
        const std::string value = STAI_TaskFactory::GetSetting(config, "Delay").value_or("0");
        char* end = nullptr;
        const long delay = std::strtol(value.c_str(), &end, 10);
        if (value.empty() || *end || delay < 0)
            throw std::runtime_error("<Delay value=\"...\">: expected milliseconds (0 = forever), got '" + value + "'");
        return uint32(delay);
    }
}

void Task_Wait::Validate(const STAI_TaskConfig& config)
{
    ParseDelay(config);
}

std::unique_ptr<STAI_Task> Task_Wait::Create(const STAI_TaskConfig& config)
{
    return std::make_unique<Task_Wait>(ParseDelay(config));
}

void Task_Wait::OnEnter(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(Task_Wait::OnEnter);
    ContextAs<Task_WaitContext>(baseContext).waited = 0;
}

EActionRunStatus Task_Wait::OnTick(STAI_TaskContext& baseContext, uint32 elapsed, bool minimal) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(Task_Wait::OnTick);
    if (!delay)
        return EActionRunStatus::Running;

    Task_WaitContext& context = ContextAs<Task_WaitContext>(baseContext);
    context.waited += elapsed;
    return context.waited >= delay ? EActionRunStatus::Succeeded : EActionRunStatus::Running;
}

std::string Task_Wait::GetDebugInfo() const
{
    return delay ? "Wait " + std::to_string(delay) + " ms" : "Wait";
}
