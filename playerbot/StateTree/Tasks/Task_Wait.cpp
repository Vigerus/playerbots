#include "Task_Wait.h"

#include "playerbot/StateTree/STAI_TaskFactory.h"

using namespace ai;

void Task_Wait::Validate(const STAI_TaskConfig& config)
{

}

std::unique_ptr<STAI_Task> Task_Wait::Create(const STAI_TaskConfig& config)
{
    return std::make_unique<Task_Wait>(0);
}

void Task_Wait::OnEnter(STAI_TaskContext& baseContext) const
{

}

EActionRunStatus Task_Wait::OnTick(STAI_TaskContext& baseContext, uint32 elapsed, bool minimal) const
{
    return EActionRunStatus::Running;
}

std::string Task_Wait::GetDebugInfo(const STAI_TaskContext& baseContext) const
{
    std::ostringstream out;
    out << "wait " << "infinitely";
    return out.str();
}
