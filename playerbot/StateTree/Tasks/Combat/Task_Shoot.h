#pragma once

#include "playerbot/StateTree/STAI_TaskFactory.h"

#include "Entities/ObjectGuid.h"

namespace ai
{
    // runtime context of Task_Shoot: the target (bound) and the one being shot
    struct Task_ShootContext : public STAI_TaskContext
    {
        ObjectGuid target;      // property "target", e.g. bound to TargetProvider_GlobalTask::target
        ObjectGuid shooting;    // the unit the wand shoots at
        uint32 sinceShotMs = 0; // since the auto-repeat last ran
    };

    // Shoots the target with the equipped wand (Shoot, auto-repeat) for as long as the node runs:
    //   <Task class="Task_Shoot">
    //     <Binding local="target" external="Root|TargetProvider_GlobalTask::target"/>
    //   </Task>
    // Running while it shoots; Succeeded without a living target or when the bound target changed (the node selects
    // again), Failed without a wand, when the bot cannot shoot the target (range, sight) or the shooting stopped for
    // 2s and cannot be started again. Not finished by itself while the target lives: the node is left through its
    // transitions. OnExitState stops the shooting.
    class Task_Shoot : public STAI_TaskBase
    {
    public:
        static void Validate(const STAI_TaskConfig& config) {}
        static std::unique_ptr<STAI_TaskBase> Create(const STAI_TaskConfig& config) { return std::make_unique<Task_Shoot>(); }

        virtual const char* GetNodeClass() const override { return "Task_Shoot"; }
        inline static std::string task_name = "Shoot";
        virtual std::string GetName() const override { return task_name; }

        virtual const STAI_TaskContextType& GetContextType() const override { return STAI_TaskContextType::Of<Task_ShootContext>(); }

        virtual EActionRunStatus OnEnterState(STAI_TaskContext& context) const override;
        virtual EActionRunStatus OnTick(STAI_TaskContext& context, uint32 elapsed, bool minimal) const override;
        virtual void OnExitState(STAI_TaskContext& context) const override;
        virtual bool ShouldCallTick() const override { return true; }

        virtual const std::vector<STAI_PropertyDef>& GetProperties() const override;

        virtual std::string GetDebugInfo() const override;

    private:
        // starts the wand on the target: false without a wand or when the bot cannot shoot it
        static bool Start(Task_ShootContext& context, Unit* target);
    };
}
