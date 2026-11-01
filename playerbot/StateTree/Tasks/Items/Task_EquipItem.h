#pragma once

#include "playerbot/StateTree/STAI_TaskFactory.h"

#include "Entities/ObjectGuid.h"

#include <string>

namespace ai
{
    // runtime context of Task_EquipItem: the item (bound) and the run
    struct Task_EquipItemContext : public STAI_TaskContext
    {
        ObjectGuid item;            // property "item", e.g. bound to InventoryProvider_GlobalTask::equipItem
        std::string result;         // how the run went, in its History entry
    };

    // Equips a bag item at the slot ItemEvalSubsystem::FindEquipSlot picks (a free one, else the weakest worn item's), as
    // a player dragging it onto that slot (CMSG_SWAP_ITEM: the worn item goes to the bag slot, a two-hander takes the
    // off hand off too):
    //   <Task class="Task_EquipItem">
    //     <Binding local="item" external="Root|InventoryProvider_GlobalTask::equipItem"/>
    //   </Task>
    // Not tickable: Succeeded in OnEnterState when the item is worn, Failed otherwise (gone, not better any more, or the
    // core refused: e.g. a bag replacing one that is not empty); its History entry tells which.
    class Task_EquipItem : public STAI_TaskBase
    {
    public:
        static void Validate(const STAI_TaskConfig& config) {}
        static std::unique_ptr<STAI_TaskBase> Create(const STAI_TaskConfig& config) { return std::make_unique<Task_EquipItem>(); }

        virtual const char* GetNodeClass() const override { return "Task_EquipItem"; }
        inline static std::string task_name = "EquipItem";
        virtual std::string GetName() const override { return task_name; }

        virtual const STAI_TaskContextType& GetContextType() const override { return STAI_TaskContextType::Of<Task_EquipItemContext>(); }

        virtual EActionRunStatus OnEnterState(STAI_TaskContext& context) const override;

        virtual const std::vector<STAI_PropertyDef>& GetProperties() const override;

        virtual std::string GetDebugInfo() const override { return "Equip an item"; }
        virtual std::string GetActivityInfo(const STAI_TaskContext& context) const override;
    };
}
