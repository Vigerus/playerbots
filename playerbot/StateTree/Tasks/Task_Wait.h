#pragma once

#include "playerbot/StateTree/STAI_TaskFactory.h"

namespace ai
{
    struct Task_WaitContext : public STAI_TaskContext
    {
        uint32 delay = 0;
    };

    class Task_Wait : public STAI_Task
    {
    public:
        Task_Wait(uint32 delay = 0) {}

        static void Validate(const STAI_TaskConfig& config);
        static std::unique_ptr<STAI_Task> Create(const STAI_TaskConfig& config);

        virtual const char* GetTaskClass() const override { return "Task_Wait"; }

        virtual const STAI_TaskContextType& GetContextType() const override { return STAI_TaskContextType::Of<Task_WaitContext>(); }

        virtual void OnEnter(STAI_TaskContext& context) const override;
        virtual EActionRunStatus OnTick(STAI_TaskContext& context, uint32 elapsed, bool minimal) const override;
        virtual bool IsTickable() const override { return true; }

        virtual std::string GetDebugInfo(const STAI_TaskContext& context) const override;
    };
}
