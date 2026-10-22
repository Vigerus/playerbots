#pragma once

#include "playerbot/StateTree/STAI_TaskFactory.h"

namespace ai
{
    // Finishes the tree that contains its node: the only way a tree exits on its own.
    //   <Node name="Done">
    //     <Conditions> <Condition trigger="..."/> </Conditions>
    //     <Task class="STAI_FinishTreeTask"/>
    //   </Node>
    // Succeeds in OnEnter and asks for the finish through its context (bFinishTree), then the tree gets OnFinish: a
    // subtree is popped from the tree stack (its Task_RunSubtree node succeeds), the initial tree of a state starts
    // again on the next update.
    class STAI_FinishTreeTask : public STAI_Task
    {
    public:
        static void Validate(const STAI_TaskConfig& config) {}
        static std::unique_ptr<STAI_Task> Create(const STAI_TaskConfig& config) { return std::make_unique<STAI_FinishTreeTask>(); }

        virtual const char* GetTaskClass() const override { return "STAI_FinishTreeTask"; }
        inline static std::string task_name = "FinishTreeTask";
        virtual std::string GetName() const override { return task_name; }

        virtual EActionRunStatus OnEnter(STAI_TaskContext& context) const override
        {
            TRACE_CPU_PROFILER_ENTRY_SCOPE(STAI_FinishTreeTask::OnEnter);
            context.bFinishTree = true;
            return EActionRunStatus::Succeeded;
        }
    };
}
