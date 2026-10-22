#pragma once

#include "playerbot/StateTree/STAI_TaskFactory.h"

namespace ai
{
    // runtime context of Task_Wait: the time waited so far
    struct Task_WaitContext : public STAI_TaskContext
    {
        uint32 waited = 0;          // ms, sum of the elapsed time of every OnTick since OnEnter
    };

    // Waits:
    //   <Task class="Task_Wait">
    //     <Delay value="0"/>        ms; 0 (default): forever, the node only leaves through a transition (OnDelegate)
    //   </Task>
    // Running until the elapsed time of its updates adds up to the delay, then Succeeded; never finishes with 0.
    class Task_Wait : public STAI_Task
    {
    public:
        explicit Task_Wait(uint32 delay = 0) : delay(delay) {}

        static void Validate(const STAI_TaskConfig& config);
        static std::unique_ptr<STAI_Task> Create(const STAI_TaskConfig& config);

        virtual const char* GetTaskClass() const override { return "Task_Wait"; }
        inline static std::string task_name = "Wait";
        virtual std::string GetName() const override { return task_name; }

        virtual const STAI_TaskContextType& GetContextType() const override { return STAI_TaskContextType::Of<Task_WaitContext>(); }

        virtual EActionRunStatus OnEnter(STAI_TaskContext& context) const override;
        virtual EActionRunStatus OnTick(STAI_TaskContext& context, uint32 elapsed, bool minimal) const override;
        virtual void OnExit(STAI_TaskContext& context) const override;
        virtual bool IsTickable() const override { return true; }

        virtual std::string GetDebugInfo() const override;

    private:
        uint32 delay;               // ms, 0 = forever
    };
}
