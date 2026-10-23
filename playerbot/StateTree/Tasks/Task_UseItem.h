#pragma once

#include "playerbot/StateTree/STAI_TaskFactory.h"

#include <vector>

namespace ai
{
    // runtime context of Task_UseItem: the use in progress
    struct Task_UseItemContext : public STAI_TaskContext
    {
        uint32 itemId = 0;      // 0: nothing was used
        uint32 spellId = 0;     // the item's spell
        uint32 startMs = 0;
    };

    // Uses the first of its items the bot can use right now on itself (a potion, a mana gem):
    //   <Task class="Task_UseItem">
    //     <Item id="8008"/>    Mana Ruby
    //     <Item id="8007"/>    Mana Citrine: listed in the order they are tried
    //   </Task>
    // An item is used when it is in the bags and its spell is off cooldown; with the item cheat an item the bot does
    // not have (the first listed one its level allows) is used by casting its spell. Succeeded once the item's spell
    // went off (its cooldown started), Failed in OnEnter when no item can be used and after 3s without the spell.
    class Task_UseItem : public STAI_Task
    {
    public:
        explicit Task_UseItem(std::vector<uint32> items) : items(std::move(items)) {}

        static void Validate(const STAI_TaskConfig& config);
        static std::unique_ptr<STAI_Task> Create(const STAI_TaskConfig& config);

        virtual const char* GetTaskClass() const override { return "Task_UseItem"; }
        inline static std::string task_name = "UseItem";
        virtual std::string GetName() const override { return task_name; }

        virtual const STAI_TaskContextType& GetContextType() const override { return STAI_TaskContextType::Of<Task_UseItemContext>(); }

        virtual EActionRunStatus OnEnter(STAI_TaskContext& context) const override;
        virtual EActionRunStatus OnTick(STAI_TaskContext& context, uint32 elapsed, bool minimal) const override;
        virtual void OnExit(STAI_TaskContext& context) const override;
        virtual bool IsTickable() const override { return true; }

        virtual std::string GetDebugInfo() const override;

    private:
        static std::vector<uint32> ParseItems(const STAI_TaskConfig& config);

        std::vector<uint32> items;  // item ids, in the order they are tried
    };
}
