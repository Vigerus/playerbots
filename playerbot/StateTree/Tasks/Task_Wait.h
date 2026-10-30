#pragma once

#include "playerbot/StateTree/STAI_TaskFactory.h"

namespace ai
{
    // runtime context of Task_Wait: the time waited so far
    struct Task_WaitContext : public STAI_TaskContext
    {
        uint32 waited = 0;          // ms, sum of the elapsed time of every OnTick since OnEnterState
        uint32 duration = 0;        // ms, this run's delay with its random deviation
    };

    // Waits:
    //   <Task class="Task_Wait">
    //     <Delay value="0" deviation="0"/>   ms; 0 (default): forever, the state only leaves through a transition;
    //                                        deviation: each run waits up to that much shorter or longer (RedSTTask_Wait)
    //   </Task>
    // Running until the elapsed time of its updates adds up to the delay, then Succeeded; never finishes with 0.
    class Task_Wait : public STAI_TaskBase
    {
    public:
        explicit Task_Wait(uint32 delay = 0, uint32 deviation = 0) : delay(delay), deviation(deviation) {}

        static void Validate(const STAI_TaskConfig& config);
        static std::unique_ptr<STAI_TaskBase> Create(const STAI_TaskConfig& config);

        virtual const char* GetNodeClass() const override { return "Task_Wait"; }
        inline static std::string task_name = "Wait";
        virtual std::string GetName() const override { return task_name; }

        virtual const STAI_TaskContextType& GetContextType() const override { return STAI_TaskContextType::Of<Task_WaitContext>(); }

        virtual EActionRunStatus OnEnterState(STAI_TaskContext& context) const override;
        virtual EActionRunStatus OnTick(STAI_TaskContext& context, uint32 elapsed, bool minimal) const override;
        virtual void OnExitState(STAI_TaskContext& context) const override;
        virtual bool ShouldCallTick() const override { return true; }

        virtual std::string GetDebugInfo() const override;

    private:
        uint32 delay;               // ms, 0 = forever
        uint32 deviation;           // ms
    };
}
