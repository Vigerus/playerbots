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
    // OnEnter picks the side to stand on; OnTick recomputes the destination next to the target and moves there
    // (again when the target moved), finishing with Succeeded once inside the radius while the target stands
    // still (Failed when the target is gone, flying, on another transport, or not reached in time); a living master
    // on another map or instance (it entered or left a dungeon) gets the bot teleported next to it.
    // OnExit stops the movement the task started.
    struct Task_FollowContext : public STAI_TaskContext
    {
        float relativeAngle = 0.0f;     // side of the target to stand on, relative to its orientation
        bool moving = false;            // a move ordered by this task is in progress
        bool hasDestination = false;
        float destinationX = 0.0f, destinationY = 0.0f, destinationZ = 0.0f;
        uint32 elapsedMs = 0;
        float lastDistance = -1.0f;
    };

    class Task_Follow : public STAI_Task
    {
    public:
        explicit Task_Follow(const std::string& acceptableRadius);

        static void Validate(const STAI_TaskConfig& config);
        static std::unique_ptr<STAI_Task> Create(const STAI_TaskConfig& config);

        virtual const char* GetTaskClass() const override { return "Task_Follow"; }

        virtual const STAI_TaskContextType& GetContextType() const override { return STAI_TaskContextType::Of<Task_FollowContext>(); }

        virtual void OnEnter(STAI_TaskContext& context) const override;
        virtual EActionRunStatus OnTick(STAI_TaskContext& context, uint32 elapsed, bool minimal) const override;
        virtual void OnExit(STAI_TaskContext& context) const override;
        virtual bool IsTickable() const override { return true; }

        virtual std::string GetDebugInfo() const override { return "Follow player, radius " + acceptableRadius; }

    private:
        static Unit* GetTarget(const STAI_TaskContext& context);
        float GetRadius(const STAI_TaskContext& context) const;
        static void MoveTo(Task_FollowContext& context, float x, float y, float z);

        std::string acceptableRadius;   // yards or a range name
    };
}
