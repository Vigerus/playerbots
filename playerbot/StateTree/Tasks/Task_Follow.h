#pragma once

#include "playerbot/StateTree/STAI_TaskFactory.h"

namespace ai
{
    // Moves the bot to its target until it is within AcceptableRadius.
    //   <Task class="Task_Follow">
    //     <Target value="player"/>            player = the bot's master
    //     <AcceptableRadius value="10"/>      yards
    //   </Task>
    // OnEnter starts following, Update finishes with Succeeded inside the radius (Failed when the target is
    // gone, on another map or not reached in time), OnExit stops the movement it started.
    class Task_Follow : public STAI_Task
    {
    public:
        Task_Follow(const STAI_TaskContext& context, float acceptableRadius);

        static void Validate(const STAI_TaskConfig& config);
        static std::unique_ptr<STAI_Task> Create(const STAI_TaskContext& context, const STAI_TaskConfig& config);

        virtual const char* GetTaskClass() const override { return "Task_Follow"; }

        virtual void OnEnter() override;
        virtual EActionRunStatus Update(uint32 elapsed, bool minimal) override;
        virtual void OnExit() override;

        virtual std::string GetDebugInfo() const override;

    private:
        Unit* GetTarget() const;
        void StartMoving(Unit* target);

        PlayerbotAI* ai;
        Player* bot;
        float acceptableRadius;

        bool moving = false;
        uint32 elapsedMs = 0;
        float lastDistance = -1.0f;
    };
}
