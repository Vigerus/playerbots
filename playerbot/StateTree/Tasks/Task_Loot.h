#pragma once

#include "playerbot/StateTree/STAI_TaskFactory.h"

#include "Entities/ObjectGuid.h"

namespace ai
{
    // runtime context of Task_Loot: the corpse (bound)
    struct Task_LootContext : public STAI_TaskContext
    {
        ObjectGuid target;          // property "target", e.g. bound to LootProvider_GlobalTask::lootTarget
    };

    // Loots a corpse as the client does: opens it (CMSG_LOOT), takes the money (CMSG_LOOT_MONEY) and every item the bot
    // may take while its bags have room (CMSG_AUTOSTORE_LOOT_ITEM), closes it (CMSG_LOOT_RELEASE). The tree's movement
    // goal (range, interact) brings the bot to the corpse:
    //   <Task class="Task_Loot">
    //     <Binding local="target" external="Root|LootProvider_GlobalTask::lootTarget"/>
    //   </Task>
    // Not tickable: Succeeded in OnEnterState once the corpse is opened and taken from, Failed when it cannot be opened
    // (gone, too far, no right to it).
    class Task_Loot : public STAI_TaskBase
    {
    public:
        static void Validate(const STAI_TaskConfig& config) {}
        static std::unique_ptr<STAI_TaskBase> Create(const STAI_TaskConfig& config) { return std::make_unique<Task_Loot>(); }

        virtual const char* GetNodeClass() const override { return "Task_Loot"; }
        inline static std::string task_name = "Loot";
        virtual std::string GetName() const override { return task_name; }

        virtual const STAI_TaskContextType& GetContextType() const override { return STAI_TaskContextType::Of<Task_LootContext>(); }

        virtual EActionRunStatus OnEnterState(STAI_TaskContext& context) const override;

        virtual const std::vector<STAI_PropertyDef>& GetProperties() const override;

        virtual std::string GetDebugInfo() const override { return "Loot the corpse"; }
    };
}
