#pragma once

#include "playerbot/StateTree/Tasks/STAI_GlobalTask.h"

#include "Entities/ObjectGuid.h"

#include <string>
#include <vector>

namespace ai
{
    // runtime context of LootProvider_GlobalTask
    struct LootProvider_GlobalTaskContext : public STAI_TaskContext
    {
        // fired when lootTarget got a new corpse:
        //   <Transition trigger="OnDelegate" source="Root|LootProvider_GlobalTask::onLootable" target="Root|Loot"/>
        FSTAI_Delegate onLootable;
        ObjectGuid lootTarget;      // property "lootTarget": the closest corpse the bot may loot, empty without one
        uint32 count = 0;           // property "count": the corpses the bot may loot in range
        uint32 sinceCheckMs = 0;
        uint32 targetSinceMs = 0;   // how long lootTarget has been the same corpse
        std::vector<ObjectGuid> ignored;    // corpses the bot did not get to (they despawn anyway)
    };

    // Global task that finds the corpses around the bot it may loot (the loot is the bot's or its group's and something
    // is left for it), the closest first; a corpse still not looted after 30s is ignored (the bot cannot reach it):
    //   <GlobalTasks>
    //     <Task class="LootProvider_GlobalTask">
    //       <Range value="30"/>     yards, default 30
    //     </Task>
    //   </GlobalTasks>
    // Ticked: checks every 500ms. Task_Loot loots the corpse.
    class LootProvider_GlobalTask : public STAI_DataProviderBase
    {
    public:
        explicit LootProvider_GlobalTask(float range) : range(range) {}

        static void Validate(const STAI_TaskConfig& config);
        static std::unique_ptr<STAI_TaskBase> Create(const STAI_TaskConfig& config);

        virtual const char* GetNodeClass() const override { return "LootProvider_GlobalTask"; }
        inline static std::string task_name = "LootProvider";
        virtual std::string GetName() const override { return task_name; }

        virtual const STAI_TaskContextType& GetContextType() const override { return STAI_TaskContextType::Of<LootProvider_GlobalTaskContext>(); }

        virtual EActionRunStatus OnEnterState(STAI_TaskContext& context) const override;
        virtual EActionRunStatus OnTick(STAI_TaskContext& context, uint32 elapsed, bool minimal) const override;
        virtual bool ShouldCallTick() const override { return true; }

        virtual const std::vector<STAI_PropertyDef>& GetProperties() const override;
        virtual const std::vector<STAI_DelegateDef>& GetDelegates() const override;

    private:
        static float ParseRange(const STAI_TaskConfig& config);
        void Update(LootProvider_GlobalTaskContext& context) const;

        float range;
    };
}
