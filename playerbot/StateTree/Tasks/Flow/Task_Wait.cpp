#include "Task_Wait.h"

#include "playerbot/StateTree/STAI_TaskFactory.h"

#include "Util/Util.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>
#include <stdexcept>

using namespace ai;

namespace
{
    // <Delay value="..." deviation="..."/>, ms, default 0 (forever) and 0
    uint32 ParseDelay(const STAI_TaskConfig& config, const std::string& attribute)
    {
        // the deviation only as an attribute: <Delay>500</Delay> is the delay
        const std::string value = attribute == "value" ? STAI_TaskFactory::GetSetting(config, "Delay").value_or("0")
            : config.get<std::string>("Delay.<xmlattr>." + attribute, "0");
        char* end = nullptr;
        const long delay = std::strtol(value.c_str(), &end, 10);
        if (value.empty() || *end || delay < 0)
            throw std::runtime_error("<Delay " + attribute + "=\"...\">: expected milliseconds, got '" + value + "'");
        return uint32(delay);
    }
}

void Task_Wait::Validate(const STAI_TaskConfig& config)
{
    ParseDelay(config, "value");
    ParseDelay(config, "deviation");
}

std::unique_ptr<STAI_TaskBase> Task_Wait::Create(const STAI_TaskConfig& config)
{
    return std::make_unique<Task_Wait>(ParseDelay(config, "value"), ParseDelay(config, "deviation"));
}

EActionRunStatus Task_Wait::OnEnterState(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(Task_Wait::OnEnterState);
    Task_WaitContext& context = ContextAs<Task_WaitContext>(baseContext);
    context.waited = 0;
    context.duration = delay && deviation ? uint32(std::max<int64>(1, int64(delay) + int64(urand(0, 2 * deviation)) - int64(deviation))) : delay;
    StartLog(baseContext);
    return EActionRunStatus::Running;
}

EActionRunStatus Task_Wait::OnTick(STAI_TaskContext& baseContext, uint32 elapsed, bool minimal) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(Task_Wait::OnTick);
    if (!delay)
        return LogStatus(baseContext, EActionRunStatus::Running);

    Task_WaitContext& context = ContextAs<Task_WaitContext>(baseContext);
    context.waited += elapsed;
    return LogStatus(baseContext, context.waited >= context.duration ? EActionRunStatus::Succeeded : EActionRunStatus::Running);
}

void Task_Wait::OnExitState(STAI_TaskContext& context) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(Task_Wait::OnExitState);
    // left by a transition: waiting went as planned
    EndLog(context, true);
}

std::string Task_Wait::GetDebugInfo() const
{
    if (!delay)
        return "Wait";
    return "Wait " + std::to_string(delay) + (deviation ? " +-" + std::to_string(deviation) : "") + " ms";
}
