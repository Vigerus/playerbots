#pragma once

#include "playerbot/StateTree/STAI_TaskFactory.h"

namespace ai
{
    // a spot on the bot's map, as a task property (InstanceProvider_GlobalTask::stagePosition); unset while !valid
    struct STAI_Position
    {
        float x = 0.0f, y = 0.0f, z = 0.0f;
        bool valid = false;

        bool IsEmpty() const { return !valid; }
        bool operator==(const STAI_Position& other) const { return valid == other.valid && x == other.x && y == other.y && z == other.z; }
        bool operator!=(const STAI_Position& other) const { return !(*this == other); }
    };

    // runtime context of Task_MoveToPosition
    struct Task_MoveToPositionContext : public STAI_TaskContext
    {
        STAI_Position position;         // property "position", bound to the spot to go to
        bool moving = false;            // a move ordered by this task is in progress
        STAI_Position destination;      // where the last move went
        uint32 elapsedMs = 0;
        float lastDistance = -1.0f;
    };

    // Moves the bot to a spot and stops there:
    //   <Task class="Task_MoveToPosition">
    //     <AcceptableRadius value="2"/>    yards (default 2)
    //     <Binding local="position" external="Root|InstanceProvider_GlobalTask::stagePosition"/>
    //   </Task>
    // Moves again when the bound position changes. Succeeded inside the radius (also when no position is set),
    // Failed when not reached within 30s; OnExit stops its movement.
    class Task_MoveToPosition : public STAI_Task
    {
    public:
        explicit Task_MoveToPosition(float acceptableRadius) : acceptableRadius(acceptableRadius) {}

        static void Validate(const STAI_TaskConfig& config);
        static std::unique_ptr<STAI_Task> Create(const STAI_TaskConfig& config);

        virtual const char* GetTaskClass() const override { return "Task_MoveToPosition"; }

        virtual const STAI_TaskContextType& GetContextType() const override { return STAI_TaskContextType::Of<Task_MoveToPositionContext>(); }

        virtual void OnEnter(STAI_TaskContext& context) const override;
        virtual EActionRunStatus OnTick(STAI_TaskContext& context, uint32 elapsed, bool minimal) const override;
        virtual void OnExit(STAI_TaskContext& context) const override;
        virtual bool IsTickable() const override { return true; }

        virtual const std::vector<STAI_PropertyDef>& GetProperties() const override;

        virtual std::string GetDebugInfo(const STAI_TaskContext& context) const override;

    private:
        float acceptableRadius;     // yards
    };
}
