#pragma once

#include "playerbot/StateTree/Tasks/STAI_GlobalTask.h"

#include "Entities/ObjectGuid.h"

#include <string>
#include <vector>

namespace ai
{
    // runtime context of ItemProvider_GlobalTask
    struct ItemProvider_GlobalTaskContext : public STAI_TaskContext
    {
        // fired when equipItem got a new item:
        //   <Transition trigger="OnDelegate" source="Root|ItemProvider_GlobalTask::onItemEquipRequest" target="Root"/>
        FSTAI_Delegate onItemEquipRequest;
        ObjectGuid equipItem;       // property "equipItem": a bag item ItemEvalSubsystem says to Equip, empty without one
        uint32 equipCount = 0;      // property "equipCount": the bag items to Equip
        uint32 vendorCount = 0;     // property "vendorCount": the bag items to sell to a vendor
        uint32 ahCount = 0;         // property "ahCount": the bag items to sell on the auction house

        uint32 sinceCheckMs = 0;
        uint32 itemSinceMs = 0;     // how long equipItem has been the same item
        std::vector<ObjectGuid> ignored;    // items the bot did not manage to equip (while they stay in the bags)

        // the core's OnItemReceived (looted, quest reward, crafted, bought): checked on the next update
        ~ItemProvider_GlobalTaskContext() override { Unsubscribe(); }
        void Subscribe();
        void Unsubscribe();
        void OnItemReceived(ObjectGuid self, ObjectGuid item, uint32 itemId) { itemReceived = true; }
        bool itemReceived = false;
        ObjectGuid subscribed;
    };

    // Global task that evaluates the bot's bag items with ItemEvalSubsystem (Vendor / AH / Keep / Equip); an item still
    // not equipped after 10s is ignored (it cannot be, e.g. a bag replacing a full one):
    //   <GlobalTasks>
    //     <Task class="ItemProvider_GlobalTask"/>
    //   </GlobalTasks>
    // Ticked: checks every 2s (an equipped item puts the old one in the bags), and on the next update after the core's
    // TeamSubsystem OnItemReceived. Task_EquipItem equips equipItem.
    class ItemProvider_GlobalTask : public STAI_DataProviderBase
    {
    public:
        static void Validate(const STAI_TaskConfig& config) {}
        static std::unique_ptr<STAI_TaskBase> Create(const STAI_TaskConfig& config) { return std::make_unique<ItemProvider_GlobalTask>(); }

        virtual const char* GetNodeClass() const override { return "ItemProvider_GlobalTask"; }
        inline static std::string task_name = "ItemProvider";
        virtual std::string GetName() const override { return task_name; }

        virtual const STAI_TaskContextType& GetContextType() const override { return STAI_TaskContextType::Of<ItemProvider_GlobalTaskContext>(); }

        virtual EActionRunStatus OnEnterState(STAI_TaskContext& context) const override;
        virtual EActionRunStatus OnTick(STAI_TaskContext& context, uint32 elapsed, bool minimal) const override;
        virtual void OnExitState(STAI_TaskContext& context) const override;
        virtual bool ShouldCallTick() const override { return true; }

        virtual const std::vector<STAI_PropertyDef>& GetProperties() const override;
        virtual const std::vector<STAI_DelegateDef>& GetDelegates() const override;

    private:
        static void Update(ItemProvider_GlobalTaskContext& context);
    };
}
