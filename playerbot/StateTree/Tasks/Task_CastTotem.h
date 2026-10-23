#pragma once

#include "playerbot/StateTree/STAI_TaskFactory.h"
#include "playerbot/StateTree/Tasks/STAI_DebugSpells.h"

#include <string>
#include <vector>

namespace ai
{
    // runtime context of Task_CastTotem: the totems to cast (bound) and the cast in progress
    struct Task_CastTotemContext : public STAI_TaskContext
    {
        std::vector<uint32> needs;      // property "needs", bound to TotemProvider_GlobalTask::needs
        uint32 spellId = 0;             // 0: nothing was cast
        uint32 castStartMs = 0;
    };

    // Casts one of the totems in its needs property (bound to the totems TotemProvider_GlobalTask found not up), then
    // waits for the cast and the global cooldown, so the tree does not spam casts:
    //   <Node name="Totems">
    //     <Conditions> <Condition binding="Root|TotemProvider_GlobalTask::needs"/> </Conditions>
    //     <Task class="Task_CastTotem">
    //       <Binding local="needs" external="Root|TotemProvider_GlobalTask::needs"/>
    //     </Task>
    //   </Node>
    // OnEnter casts the first need the bot can cast now. Succeeded once the cast and the GCD are over, or in OnEnter
    // when nothing is needed; Failed in OnEnter when no need can be cast (mana, the totem's tool, cooldown...).
    class Task_CastTotem : public STAI_Task, public ISTAI_DebugSpells
    {
    public:
        static void Validate(const STAI_TaskConfig& config) {}
        static std::unique_ptr<STAI_Task> Create(const STAI_TaskConfig& config)
        {
            return std::make_unique<Task_CastTotem>(STAI_TaskFactory::GetBindingSource(config, "needs"));
        }

        explicit Task_CastTotem(const std::string& needsSource) : needsSource(needsSource) {}

        virtual const char* GetTaskClass() const override { return "Task_CastTotem"; }
        inline static std::string task_name = "CastTotem";
        virtual std::string GetName() const override { return task_name; }

        virtual const STAI_TaskContextType& GetContextType() const override { return STAI_TaskContextType::Of<Task_CastTotemContext>(); }

        virtual EActionRunStatus OnEnter(STAI_TaskContext& context) const override;
        virtual EActionRunStatus OnTick(STAI_TaskContext& context, uint32 elapsed, bool minimal) const override;
        virtual void OnExit(STAI_TaskContext& context) const override;
        virtual bool IsTickable() const override { return true; }

        virtual const std::vector<STAI_PropertyDef>& GetProperties() const override;

        virtual std::string GetDebugInfo() const override { return "Cast " + needsSource; }
        virtual std::vector<uint32> GetDebugSpells(const STAI_TaskContext& context) const override;
        // every cast gets its own History entry
        virtual bool AreConsecutiveLogsStacked() const override { return false; }

    private:
        std::string needsSource;    // what needs is bound to, for the debug info
    };
}
