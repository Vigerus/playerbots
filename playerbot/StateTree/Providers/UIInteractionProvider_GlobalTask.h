#pragma once

#include "playerbot/StateTree/Tasks/STAI_GlobalTask.h"

#include "Entities/ObjectGuid.h"

#include <memory>
#include <vector>

class Player;
class PlayerbotAI;

namespace ai
{
    // a loot roll the bot was asked to vote on (TeamSubsystem::OnLootRollStarted)
    struct STAI_ItemRoll
    {
        ObjectGuid lootGuid;        // the looted creature / object
        uint32 itemSlot = 0;        // the item's slot in its loot
        uint32 itemId = 0;
        uint8 voteMask = 0;         // RollVoteMask: the votes allowed to the bot
        uint32 startMs = 0;
        bool done = false;          // voted (Task_RollForItem), dropped by the provider on its next update
    };

    // whether the bot trades with trader at all (the deprecated TradeStatusAction's rule): other bots, its master and
    // the members of its group (with the security level for everything); the trade tasks cancel any other trade
    bool STAI_CanTradeWith(PlayerbotAI* ai, Player* bot, Player* trader);

    // runtime context of UIInteractionProvider_GlobalTask
    struct UIInteractionProvider_GlobalTaskContext : public STAI_TaskContext
    {
        ~UIInteractionProvider_GlobalTaskContext() override { Unsubscribe(); }

        // fired when the group starts a roll the bot may vote on:
        //   <Transition type="OnDelegate" source="Root|UIInteractionProvider_GlobalTask::onRollRequested" node="Root|Roll For Item"/>
        FSTAI_Delegate onRollRequested;
        // the rolls still to vote on, oldest first (shared: the voting task marks them done):
        //   <Binding local="rolls" external="Root|UIInteractionProvider_GlobalTask::rolls"/>
        std::vector<std::shared_ptr<STAI_ItemRoll>> rolls;

        // fired when a player opens a trade with the bot (its trade window is not open yet)
        FSTAI_Delegate onTradeRequested;
        // fired when the other player accepted the trade (the bot did not yet)
        FSTAI_Delegate onTradeAccepted;
        // property "tradeOpenRequested": a trade was opened with the bot and its window is not open yet
        bool tradeOpenRequested = false;
        // property "tradeAcceptRequested": the trade window is open, the other player accepted, the bot did not
        bool tradeAcceptRequested = false;

        bool subscribed = false;

        void Subscribe();
        void Unsubscribe();
        void OnLootRollStarted(ObjectGuid self, ObjectGuid lootGuid, uint32 itemSlot, uint32 itemId, uint8 voteMask);
        void OnTradeStatus(uint32 status);
        // tradeAcceptRequested from the trade data
        void UpdateTrade();
    };

    // Global task for what a player answers in its UI: loot rolls (property rolls, delegate onRollRequested, a new
    // roll from TeamSubsystem::OnLootRollStarted) and trades (properties tradeOpenRequested / tradeAcceptRequested,
    // delegates onTradeRequested / onTradeAccepted, from the SMSG_TRADE_STATUS the bot gets: PlayerbotAI::onTradeStatus).
    // Ticked: drops the rolls that were voted on or timed out, follows the trade.
    //   <Global>
    //     <Task class="UIInteractionProvider_GlobalTask"/>
    //   </Global>
    // Task_RollForItem votes on the rolls, Task_OpenTrade opens the trade window, Task_AcceptTrade accepts the trade.
    class UIInteractionProvider_GlobalTask : public STAI_GlobalTask
    {
    public:
        static void Validate(const STAI_TaskConfig& config) {}
        static std::unique_ptr<STAI_Task> Create(const STAI_TaskConfig& config) { return std::make_unique<UIInteractionProvider_GlobalTask>(); }

        virtual const char* GetTaskClass() const override { return "UIInteractionProvider_GlobalTask"; }
        inline static std::string task_name = "UIInteractionProvider";
        virtual std::string GetName() const override { return task_name; }

        virtual const STAI_TaskContextType& GetContextType() const override { return STAI_TaskContextType::Of<UIInteractionProvider_GlobalTaskContext>(); }

        virtual EActionRunStatus OnEnter(STAI_TaskContext& context) const override;
        virtual EActionRunStatus OnTick(STAI_TaskContext& context, uint32 elapsed, bool minimal) const override;
        virtual void OnExit(STAI_TaskContext& context) const override;
        virtual bool IsTickable() const override { return true; }

        virtual const std::vector<STAI_PropertyDef>& GetProperties() const override;
        virtual const std::vector<STAI_DelegateDef>& GetDelegates() const override;
    };
}
