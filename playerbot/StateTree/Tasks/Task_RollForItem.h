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
    // tickable: Succeeded in OnEnterState.
    class Task_RollForItem : public STAI_TaskBase
    {
    public:
        static void Validate(const STAI_TaskConfig& config) {}
        static std::unique_ptr<STAI_TaskBase> Create(const STAI_TaskConfig& config) { return std::make_unique<Task_RollForItem>(); }

        virtual const char* GetNodeClass() const override { return "Task_RollForItem"; }
        inline static std::string task_name = "RollForItem";
        virtual std::string GetName() const override { return task_name; }

        virtual const STAI_TaskContextType& GetContextType() const override { return STAI_TaskContextType::Of<Task_RollForItemContext>(); }

        virtual EActionRunStatus OnEnterState(STAI_TaskContext& context) const override;

        virtual const std::vector<STAI_PropertyDef>& GetProperties() const override;

        virtual std::string GetDebugInfo() const override { return "Roll greed"; }
    };
}
