#pragma once

#include "playerbot/StateTree/STAI_TaskFactory.h"

namespace ai
{
    // Scopeless task of a tree's <Global> section that does nothing; base for global tasks.
    //   <Root>
    //     <Global>
    //       <Task class="STAI_GlobalTask"/>
    //     </Global>
    //     ...
    //   </Root>
    // Entered when the tree starts, updated on every tick of the tree whichever node runs, exited when the tree
    // finishes.
    class STAI_GlobalTask : public STAI_Task
    {
    public:
        // global tasks run the whole time the tree runs: no History entries
        STAI_GlobalTask() { bShowLog = false; }

        static void Validate(const STAI_TaskConfig& config) {}
        static std::unique_ptr<STAI_Task> Create(const STAI_TaskConfig& config) { return std::make_unique<STAI_GlobalTask>(); }

        virtual const char* GetTaskClass() const override { return "STAI_GlobalTask"; }
        inline static std::string task_name = "GlobalTask";
        virtual std::string GetName() const override { return task_name; }

        virtual EActionRunStatus OnEnter(STAI_TaskContext& context) const override { TRACE_CPU_PROFILER_ENTRY_SCOPE(STAI_GlobalTask::OnEnter); return EActionRunStatus::Running; }
        virtual EActionRunStatus OnTick(STAI_TaskContext& context, uint32 elapsed, bool minimal) const override { TRACE_CPU_PROFILER_ENTRY_SCOPE(STAI_GlobalTask::OnTick); return EActionRunStatus::Running; }
        virtual void OnExit(STAI_TaskContext& context) const override { TRACE_CPU_PROFILER_ENTRY_SCOPE(STAI_GlobalTask::OnExit); }
    };
}
