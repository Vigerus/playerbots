#pragma once

#include "playerbot/StateTree/STAI_TaskFactory.h"

namespace ai
{
    // Base of the data providers (RedStateTree's FRedSTDataProviderBase): tasks of <GlobalTasks> that publish
    // properties and delegates for the tree (Providers/). Entered when the tree starts and running until it stops,
    // their result is ignored by default (completion="..." changes it: a global task completing finishes the tree)
    // and their runs get no History entries.
    class STAI_DataProviderBase : public STAI_TaskBase
    {
    public:
        STAI_DataProviderBase() { bShowLog = false; }

        virtual ESTAI_TaskCompletion GetDefaultCompletion() const override { return ESTAI_TaskCompletion::IgnoreResult; }

    protected:
        virtual EActionRunStatus OnEnterState(STAI_TaskContext& context) const override { return EActionRunStatus::Running; }
        virtual EActionRunStatus OnTick(STAI_TaskContext& context, uint32 elapsed, bool minimal) const override { return EActionRunStatus::Running; }
    };

    // Global task that does nothing; base for global tasks written against the old name.
    //   <StateTree version="2">
    //     <GlobalTasks>
    //       <Task class="STAI_GlobalTask"/>
    //     </GlobalTasks>
    //     ...
    //   </StateTree>
    // Entered when the tree starts, updated on every tick of the tree whichever state runs, exited when it finishes.
    class STAI_GlobalTask : public STAI_DataProviderBase
    {
    public:
        static void Validate(const STAI_TaskConfig& config) {}
        static std::unique_ptr<STAI_TaskBase> Create(const STAI_TaskConfig& config) { return std::make_unique<STAI_GlobalTask>(); }

        virtual const char* GetNodeClass() const override { return "STAI_GlobalTask"; }
        inline static std::string task_name = "GlobalTask";
        virtual std::string GetName() const override { return task_name; }

    protected:
        virtual EActionRunStatus OnEnterState(STAI_TaskContext& context) const override { TRACE_CPU_PROFILER_ENTRY_SCOPE(STAI_GlobalTask::OnEnterState); return EActionRunStatus::Running; }
        virtual EActionRunStatus OnTick(STAI_TaskContext& context, uint32 elapsed, bool minimal) const override { TRACE_CPU_PROFILER_ENTRY_SCOPE(STAI_GlobalTask::OnTick); return EActionRunStatus::Running; }
        virtual void OnExitState(STAI_TaskContext& context) const override { TRACE_CPU_PROFILER_ENTRY_SCOPE(STAI_GlobalTask::OnExitState); }
    };
}
