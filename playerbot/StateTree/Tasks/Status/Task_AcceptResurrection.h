#pragma once

#include "playerbot/StateTree/STAI_TaskFactory.h"

namespace ai
{
    // Waits, dead, for a resurrection offered to the bot (a priest's Resurrection, a paladin's Redemption, ...) and
    // accepts it, like a player answering the request (CMSG_RESURRECT_RESPONSE):
    //   <Task class="Task_AcceptResurrection"/>
    // Running while nothing is offered; Succeeded once the bot accepted (it comes back, the state machine goes to
    // NonCombat), also when the bot is alive. Not logged while it waits.
    class Task_AcceptResurrection : public STAI_TaskBase
    {
    public:
        static void Validate(const STAI_TaskConfig& config) {}
        static std::unique_ptr<STAI_TaskBase> Create(const STAI_TaskConfig& config) { return std::make_unique<Task_AcceptResurrection>(); }

        virtual const char* GetNodeClass() const override { return "Task_AcceptResurrection"; }
        inline static std::string task_name = "AcceptResurrection";
        virtual std::string GetName() const override { return task_name; }

        virtual EActionRunStatus OnEnterState(STAI_TaskContext& context) const override;
        virtual EActionRunStatus OnTick(STAI_TaskContext& context, uint32 elapsed, bool minimal) const override;
        virtual bool ShouldCallTick() const override { return true; }

        virtual std::string GetDebugInfo() const override { return "Accept resurrection"; }

    private:
        EActionRunStatus Check(STAI_TaskContext& context) const;
    };
}
