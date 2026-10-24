#pragma once

#include "playerbot/StateTree/STAI_TaskFactory.h"

#include "Entities/ObjectGuid.h"

namespace ai
{
    // runtime context of Task_StartAttack: the target (bound)
    struct Task_StartAttackContext : public STAI_TaskContext
    {
        ObjectGuid target;          // property "target", e.g. bound to TargetProvider_GlobalTask::target
    };

    // Puts the auto attack on the target, as the client's attack swing, without moving (the tree's movement goal, a
    // MovementGoal_GlobalTask of kind range on the same target, brings the bot into reach):
    //   <Task class="Task_StartAttack">
    //     <Binding local="target" external="Root|TargetProvider_GlobalTask::target"/>
    //   </Task>
    // Not tickable: Succeeded in OnEnter once the attack is on the target, Failed without a living hostile target.
    class Task_StartAttack : public STAI_Task
    {
    public:
        explicit Task_StartAttack(const std::string& targetSource) : targetSource(targetSource) {}

        static void Validate(const STAI_TaskConfig& config) {}
        static std::unique_ptr<STAI_Task> Create(const STAI_TaskConfig& config)
        {
            return std::make_unique<Task_StartAttack>(STAI_TaskFactory::GetBindingSource(config, "target"));
        }

        virtual const char* GetTaskClass() const override { return "Task_StartAttack"; }
        inline static std::string task_name = "StartAttack";
        virtual std::string GetName() const override { return task_name; }

        virtual const STAI_TaskContextType& GetContextType() const override { return STAI_TaskContextType::Of<Task_StartAttackContext>(); }

        virtual EActionRunStatus OnEnter(STAI_TaskContext& context) const override;

        virtual const std::vector<STAI_PropertyDef>& GetProperties() const override;

        virtual std::string GetDebugInfo() const override { return "Attack " + targetSource; }

    private:
        std::string targetSource;   // what target is bound to, for the debug info
    };

    // Stops the auto attack (the target is held back by threat, or there is none); the movement goal on the target is
    // dropped with the target, so the bot stays where it is:
    //   <Task class="Task_StopAttack"/>
    // Not tickable: Succeeded in OnEnter.
    class Task_StopAttack : public STAI_Task
    {
    public:
        static void Validate(const STAI_TaskConfig& config) {}
        static std::unique_ptr<STAI_Task> Create(const STAI_TaskConfig& config) { return std::make_unique<Task_StopAttack>(); }

        virtual const char* GetTaskClass() const override { return "Task_StopAttack"; }
        inline static std::string task_name = "StopAttack";
        virtual std::string GetName() const override { return task_name; }

        virtual EActionRunStatus OnEnter(STAI_TaskContext& context) const override;

        virtual std::string GetDebugInfo() const override { return "Stop attacking"; }
    };
}
