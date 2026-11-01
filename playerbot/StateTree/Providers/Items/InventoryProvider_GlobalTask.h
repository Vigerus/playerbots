#pragma once

#include "playerbot/StateTree/Tasks/STAI_GlobalTask.h"

#include "Entities/ObjectGuid.h"

#include <functional>
#include <string>
#include <vector>

class Item;
class Player;

namespace ai
{
    // runtime context of InventoryProvider_GlobalTask
    struct InventoryProvider_GlobalTaskContext : public STAI_TaskContext
    {
        FSTAI_Delegate onLowSupplies;   // fired when food or drink dropped below the Low count
        uint32 food = 0;                // property "food": food items in the bags the bot can eat (stacks counted)
        uint32 drink = 0;               // property "drink": drink items it can drink
        bool lowFood = false;           // property "lowFood": food below the Low count
        bool lowDrink = false;          // property "lowDrink"
        uint32 freeSlots = 0;           // property "freeSlots": empty slots in the backpack and the bags
        uint32 sellable = 0;            // property "sellable": bag items ItemEvalSubsystem says to sell to a vendor
        uint32 ahCount = 0;             // property "ahCount": bag items to sell on the auction house
        bool bagsFull = false;          // property "bagsFull": fewer free slots than Free while something is sellable
        FSTAI_Delegate onBagsFull;      // fired when bagsFull turned true

        // fired when equipItem got a new item:
        //   <Transition trigger="OnDelegate" source="Root|InventoryProvider_GlobalTask::onItemEquipRequest" target="Root"/>
        FSTAI_Delegate onItemEquipRequest;
        ObjectGuid equipItem;           // property "equipItem": a bag item ItemEvalSubsystem says to Equip, empty without one
        uint32 equipCount = 0;          // property "equipCount": the bag items to Equip

        uint32 sinceCheckMs = 0;
        uint32 itemSinceMs = 0;         // how long equipItem has been the same item
        std::vector<ObjectGuid> ignored;    // items the bot did not manage to equip (while they stay in the bags)

        // the core's OnItemReceived (looted, quest reward, crafted, bought): checked on the next update
        ~InventoryProvider_GlobalTaskContext() override { Unsubscribe(); }
        void Subscribe();
        void Unsubscribe();
        void OnItemReceived(ObjectGuid self, ObjectGuid item, uint32 itemId) { itemReceived = true; }
        bool itemReceived = false;
        ObjectGuid subscribed;
    };

    // Global task that watches the bot's bags: what it carries for its upkeep (the items Task_EatAndDrink would use),
    // how full they are, and what ItemEvalSubsystem says of each item (Vendor / AH / Keep / Equip); an item still not
    // equipped after 10s is ignored (it cannot be, e.g. a bag replacing a full one):
    //   <GlobalTasks>
    //     <Task class="InventoryProvider_GlobalTask">
    //       <Low value="5"/>     food / drink below this count is low, default 5
    //       <Free value="2"/>    fewer free slots than this (and something to sell) is bagsFull, default 2
    //     </Task>
    //   </GlobalTasks>
    // Ticked: counts every 2s, on the next update after the core's TeamSubsystem OnItemReceived, and at once when
    // equipItem left the bags (equipped: the old item came back into them). Task_EquipItem equips equipItem,
    // Task_SellJunk sells.
    class InventoryProvider_GlobalTask : public STAI_DataProviderBase
    {
    public:
        InventoryProvider_GlobalTask(uint32 low, uint32 free) : low(low), free(free) {}

        // every item in the backpack and the bags (not equipped, not in the bank)
        static void ForEachBagItem(Player* bot, const std::function<void(Item*)>& visit);
        static uint32 CountFreeSlots(Player* bot);

        static void Validate(const STAI_TaskConfig& config);
        static std::unique_ptr<STAI_TaskBase> Create(const STAI_TaskConfig& config);

        virtual const char* GetNodeClass() const override { return "InventoryProvider_GlobalTask"; }
        inline static std::string task_name = "InventoryProvider";
        virtual std::string GetName() const override { return task_name; }

        virtual const STAI_TaskContextType& GetContextType() const override { return STAI_TaskContextType::Of<InventoryProvider_GlobalTaskContext>(); }

        virtual EActionRunStatus OnEnterState(STAI_TaskContext& context) const override;
        virtual EActionRunStatus OnTick(STAI_TaskContext& context, uint32 elapsed, bool minimal) const override;
        virtual void OnExitState(STAI_TaskContext& context) const override;
        virtual bool ShouldCallTick() const override { return true; }

        virtual const std::vector<STAI_PropertyDef>& GetProperties() const override;
        virtual const std::vector<STAI_DelegateDef>& GetDelegates() const override;

    private:
        static uint32 ParseCount(const STAI_TaskConfig& config, const std::string& name, const char* fallback);
        void Update(InventoryProvider_GlobalTaskContext& context) const;
        // the bag items by ItemEvalSubsystem: sellable, ahCount, equipCount, equipItem
        static void Evaluate(InventoryProvider_GlobalTaskContext& context);

        uint32 low;
        uint32 free;
    };
}
