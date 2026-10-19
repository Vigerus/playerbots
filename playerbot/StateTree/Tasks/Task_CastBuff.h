#pragma once

#include "playerbot/StateTree/STAI_TaskFactory.h"
#include "playerbot/StateTree/Providers/STAI_BuffTypes.h"

#include "Entities/ObjectGuid.h"

#include <string>
#include <vector>

namespace ai
{
    // runtime context of Task_CastBuff: the buffs to cast (bound) and the cast in progress
    struct Task_CastBuffContext : public STAI_TaskContext
    {
        std::vector<BuffNeed> needs;    // property "needs", bound to BuffProvider_GlobalTask::needs
        uint32 spellId = 0;             // 0: nothing was cast
        ObjectGuid target;
        uint32 castStartMs = 0;
        bool failed = false;
    };

    // Casts one of the buffs in its needs property (bound to what BuffProvider_GlobalTask found missing), then waits
    // for the cast and the global cooldown, so the tree does not spam casts:
    //   <Node name="Check Buffs">
    //     <Conditions> <Condition binding="Root|BuffProvider_GlobalTask::needs"/> </Conditions>
    //     <Task class="Task_CastBuff">
    //       <Binding local="needs" external="Root|BuffProvider_GlobalTask::needs"/>
    //     </Task>
    //   </Node>
    // OnEnter casts the first need the bot can cast now (self, a single member, or a group spell on a member of the
    // subgroup missing it). Succeeded once the cast and the GCD are over, or right away when nothing is needed;
    // Failed when no need can be cast (range, mana, reagents...).
    class Task_CastBuff : public STAI_Task
    {
    public:
        static void Validate(const STAI_TaskConfig& config) {}
        static std::unique_ptr<STAI_Task> Create(const STAI_TaskConfig& config) { return std::make_unique<Task_CastBuff>(); }

        virtual const char* GetTaskClass() const override { return "Task_CastBuff"; }

        virtual const STAI_TaskContextType& GetContextType() const override { return STAI_TaskContextType::Of<Task_CastBuffContext>(); }

        virtual void OnEnter(STAI_TaskContext& context) const override;
        virtual EActionRunStatus OnTick(STAI_TaskContext& context, uint32 elapsed, bool minimal) const override;
        virtual void OnExit(STAI_TaskContext& context) const override;
        virtual bool IsTickable() const override { return true; }

        virtual const std::vector<STAI_PropertyDef>& GetProperties() const override;

        virtual std::string GetDebugInfo(const STAI_TaskContext& context) const override;
    };
}
