#pragma once

#include "playerbot/StateTree/STAI_TaskFactory.h"

namespace ai
{
    // Finishes the tree that contains its node: the only way a tree exits on its own.
    //   <Node name="Done">
    //     <Conditions> <Condition trigger="..."/> </Conditions>
    //     <Task class="STAI_FinishTreeTask"/>
    //   </Node>
    // Succeeds on its first tick, then the tree gets OnFinish: a subtree is popped from the tree stack (its
    // Task_RunSubtree node succeeds), the initial tree of a state starts again on the next update.
    class STAI_FinishTreeTask : public STAI_Task
    {
    public:
        static void Validate(const STAI_TaskConfig& config) {}
        static std::unique_ptr<STAI_Task> Create(const STAI_TaskConfig& config) { return std::make_unique<STAI_FinishTreeTask>(); }

        virtual const char* GetTaskClass() const override { return "STAI_FinishTreeTask"; }

        virtual EActionRunStatus OnTick(STAI_TaskContext& context, uint32 elapsed, bool minimal) const override { return EActionRunStatus::Succeeded; }
        virtual bool FinishesTree() const override { return true; }
    };
}
