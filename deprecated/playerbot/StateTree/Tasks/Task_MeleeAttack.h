#pragma once

#include "playerbot/StateTree/STAI_TaskFactory.h"

#include "Entities/ObjectGuid.h"

namespace ai
{
    // runtime context of Task_MeleeAttack: the target (bound) and the movement in progress
    struct Task_MeleeAttackContext : public STAI_TaskContext
    {
        ObjectGuid target;          // property "target", e.g. bound to TargetProvider_GlobalTask::target
        ObjectGuid attacking;       // the unit the run started the attack on
        uint32 enterMs = 0;
        bool goingBehind = false;   // walking to the point behind the target (MovePoint), then chasing again
    };

    // Melee: walks to the target, starts the auto attack, stands where the bot belongs and faces the target:
    //   <Task class="Task_MeleeAttack">
    //     <Binding local="target" external="Root|TargetProvider_GlobalTask::target"/>
    //     <Position value="behind"/>   behind (damage dealers: no parry, block or frontal cleave; unless the target
    //                                  attacks the bot) or front (tanks: the target stays where it is), default front
    //     <Duration value="0"/>        ms; 0: until the bot is in melee reach, positioned and facing the target
    //   </Task>
    // The chase (MoveChase, at the target's back for behind) and the auto attack go on after the task: the ability nodes
    // run while the bot keeps hitting and following. A bot in front of the target with behind walks around it to its
    // back. Not into a dungeon stage that is not cleared, except its boxes that may be entered in combat while the group
    // fights the target (bCanBeEnteredInCombatIfNotCleared). Succeeded when positioned (Duration 0) or after Duration, or
    // without a living target; Failed after 15s without reaching it or when the way is blocked.
    class Task_MeleeAttack : public STAI_Task
    {
    public:
        Task_MeleeAttack(bool behind, uint32 durationMs, const std::string& targetSource)
            : behind(behind), durationMs(durationMs), targetSource(targetSource) {}

        static void Validate(const STAI_TaskConfig& config);
        static std::unique_ptr<STAI_Task> Create(const STAI_TaskConfig& config);

        virtual const char* GetTaskClass() const override { return "Task_MeleeAttack"; }
        inline static std::string task_name = "MeleeAttack";
        virtual std::string GetName() const override { return task_name; }

        virtual const STAI_TaskContextType& GetContextType() const override { return STAI_TaskContextType::Of<Task_MeleeAttackContext>(); }

        virtual EActionRunStatus OnEnter(STAI_TaskContext& context) const override;
        virtual EActionRunStatus OnTick(STAI_TaskContext& context, uint32 elapsed, bool minimal) const override;
        virtual void OnExit(STAI_TaskContext& context) const override;
        virtual bool IsTickable() const override { return true; }

        virtual const std::vector<STAI_PropertyDef>& GetProperties() const override;

        virtual std::string GetDebugInfo() const override;

        // the angle of the chase around the target: its back for behind, the side the bot is on for front
        static float ChaseAngle(bool behind);

    private:
        EActionRunStatus Update(Task_MeleeAttackContext& context) const;

        bool behind;
        uint32 durationMs;
        std::string targetSource;   // what target is bound to, for the debug info
    };

    // runtime context of Task_StartAttack: the target (bound)
    struct Task_StartAttackContext : public STAI_TaskContext
    {
        ObjectGuid target;          // property "target", e.g. bound to TargetProvider_GlobalTask::target
    };

    // Puts the auto attack on the target, as the client's attack swing, without moving: the movement is the Reaction
    // tree's MeleePositioner_GlobalTask, which follows the unit the auto attack is on:
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

    // Stops the auto attack (the target is held back by threat, or there is none); the bot stays where it is:
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
