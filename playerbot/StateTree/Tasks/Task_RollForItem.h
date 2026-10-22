#pragma once

#include "playerbot/StateTree/Providers/UIInteractionProvider_GlobalTask.h"
#include "playerbot/StateTree/STAI_TaskFactory.h"

#include <memory>
#include <vector>

namespace ai
{
    // runtime context of Task_RollForItem
    struct Task_RollForItemContext : public STAI_TaskContext
    {
        // property "rolls", bound to UIInteractionProvider_GlobalTask::rolls
        std::vector<std::shared_ptr<STAI_ItemRoll>> rolls;
    };

    // Votes on every loot roll the bot was asked for:
    //   <Task class="Task_RollForItem">
    //     <Binding local="rolls" external="Root|UIInteractionProvider_GlobalTask::rolls"/>
    //   </Task>
    // For now always greed (pass when greed is not allowed). Marks each roll done, the provider drops it. Not
    // tickable: Succeeded once entered.
    class Task_RollForItem : public STAI_Task
    {
    public:
        static void Validate(const STAI_TaskConfig& config) {}
        static std::unique_ptr<STAI_Task> Create(const STAI_TaskConfig& config) { return std::make_unique<Task_RollForItem>(); }

        virtual const char* GetTaskClass() const override { return "Task_RollForItem"; }

        virtual const STAI_TaskContextType& GetContextType() const override { return STAI_TaskContextType::Of<Task_RollForItemContext>(); }

        virtual void OnEnter(STAI_TaskContext& context) const override;
        virtual EActionRunStatus OnTick(STAI_TaskContext& context, uint32 elapsed, bool minimal) const override { return EActionRunStatus::Succeeded; }

        virtual const std::vector<STAI_PropertyDef>& GetProperties() const override;

        virtual std::string GetDebugInfo() const override { return "Roll greed"; }
    };
}
