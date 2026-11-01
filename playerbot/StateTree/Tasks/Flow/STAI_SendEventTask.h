#pragma once

#include "playerbot/StateTree/STAI_TaskFactory.h"

namespace ai
{
    // Sends an event to the tree running the task (RedStateTree / UE StateTree events): <Transition trigger="OnEvent"
    // event="..."> of the active states see it in the next update of the tree.
    //   <Task class="STAI_SendEventTask" event="pull"/>
    // Succeeds in OnEnterState.
    class STAI_SendEventTask : public STAI_TaskBase
    {
    public:
        explicit STAI_SendEventTask(const std::string& event) : event(event) {}

        static void Validate(const STAI_TaskConfig& config);
        static std::unique_ptr<STAI_TaskBase> Create(const STAI_TaskConfig& config);

        virtual const char* GetNodeClass() const override { return "STAI_SendEventTask"; }
        inline static std::string task_name = "SendEvent";
        virtual std::string GetName() const override { return task_name; }

        virtual std::string GetDebugInfo() const override { return "Send " + event; }

    protected:
        virtual EActionRunStatus OnEnterState(STAI_TaskContext& context) const override;

    private:
        std::string event;
    };
}
