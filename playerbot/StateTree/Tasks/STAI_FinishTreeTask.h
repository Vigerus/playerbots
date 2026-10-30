#pragma once

#include "playerbot/StateTree/STAI_TaskFactory.h"

namespace ai
{
    // Finishes the tree that contains its state (a transition with target="Succeeded" does the same).
    //   <State name="Done">
    //     <EnterConditions> ... </EnterConditions>
    //     <Tasks> <Task class="STAI_FinishTreeTask"/> </Tasks>
    //   </State>
    // Succeeds in OnEnterState and asks for the finish through its context (bFinishTree), then the tree gets OnFinish:
    // a subtree is popped from the tree stack (its Task_RunSubtree state succeeds), the initial tree of a state starts
    // again on the next update.
    class STAI_FinishTreeTask : public STAI_TaskBase
    {
    public:
        static void Validate(const STAI_TaskConfig& config) {}
        static std::unique_ptr<STAI_TaskBase> Create(const STAI_TaskConfig& config) { return std::make_unique<STAI_FinishTreeTask>(); }

        virtual const char* GetNodeClass() const override { return "STAI_FinishTreeTask"; }
        inline static std::string task_name = "FinishTreeTask";
        virtual std::string GetName() const override { return task_name; }

    protected:
        virtual EActionRunStatus OnEnterState(STAI_TaskContext& context) const override
        {
            TRACE_CPU_PROFILER_ENTRY_SCOPE(STAI_FinishTreeTask::OnEnterState);
            context.bFinishTree = true;
            return EActionRunStatus::Succeeded;
        }
    };
}
