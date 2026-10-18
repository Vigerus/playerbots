#pragma once

#include "playerbot/StateTree/STAI_TaskFactory.h"

namespace ai
{
    // Moves the bot next to its target. The only owner of follow movement: the legacy "follow" / "stay"
    // strategies are passive and only serve as tree conditions.
    //   <Task class="Task_Follow">
    //     <Target value="player"/>            player = the bot's master
    //     <AcceptableRadius value="follow"/>  yards, or a range name ("follow": the bot's follow distance)
    //   </Task>
    // OnEnter picks the side to stand on; Update recomputes the destination next to the target and moves there
    // (again when the target moved), finishing with Succeeded once inside the radius while the target stands
    // still (Failed when the target is gone, flying, on another map or transport, or not reached in time);
    // OnExit stops the movement the task started.
    class Task_Follow : public STAI_Task
    {
    public:
        Task_Follow(const STAI_TaskContext& context, const std::string& acceptableRadius);

        static void Validate(const STAI_TaskConfig& config);
        static std::unique_ptr<STAI_Task> Create(const STAI_TaskContext& context, const STAI_TaskConfig& config);

        virtual const char* GetTaskClass() const override { return "Task_Follow"; }

        virtual void OnEnter() override;
        virtual EActionRunStatus Update(uint32 elapsed, bool minimal) override;
        virtual void OnExit() override;

        virtual std::string GetDebugInfo() const override;

    private:
        Unit* GetTarget() const;
        float GetRadius() const;
        void MoveTo(float x, float y, float z);

        PlayerbotAI* ai;
        Player* bot;
        std::string acceptableRadius;   // yards or a range name

        float relativeAngle = 0.0f;     // side of the target to stand on, relative to its orientation
        bool moving = false;            // a move ordered by this task is in progress
        bool hasDestination = false;
        float destinationX = 0.0f, destinationY = 0.0f, destinationZ = 0.0f;
        uint32 elapsedMs = 0;
        float lastDistance = -1.0f;
    };
}
