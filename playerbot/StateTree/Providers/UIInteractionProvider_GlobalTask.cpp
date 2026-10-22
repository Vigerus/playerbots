#include "UIInteractionProvider_GlobalTask.h"

#include "playerbot/PlayerbotAI.h"
#include "playerbot/PlayerbotSecurity.h"

#include "Groups/TeamSubsystem.h"
#include "Loot/LootMgr.h"
#include "World/World.h"

#include <algorithm>

using namespace ai;

bool ai::STAI_CanTradeWith(PlayerbotAI* ai, Player* bot, Player* trader)
{
    if (!trader)
        return false;
    if (trader->GetPlayerbotAI())
        return true;
    if (trader != ai->GetMaster() && !bot->IsInGroup(trader))
        return false;
    return ai->GetSecurity()->CheckLevelFor(PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false, trader);
}

void UIInteractionProvider_GlobalTask::OnEnter(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(UIInteractionProvider_GlobalTask::OnEnter);
    UIInteractionProvider_GlobalTaskContext& context = ContextAs<UIInteractionProvider_GlobalTaskContext>(baseContext);
    context.Subscribe();
    context.UpdateTrade();
}

EActionRunStatus UIInteractionProvider_GlobalTask::OnTick(STAI_TaskContext& baseContext, uint32 elapsed, bool minimal) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(UIInteractionProvider_GlobalTask::OnTick);
    UIInteractionProvider_GlobalTaskContext& context = ContextAs<UIInteractionProvider_GlobalTaskContext>(baseContext);

    // voted on, or the roll is over (the group decides after LOOT_ROLL_TIMEOUT)
    const uint32 now = WorldTimer::getMSTime();
    context.rolls.erase(std::remove_if(context.rolls.begin(), context.rolls.end(), [now](const std::shared_ptr<STAI_ItemRoll>& roll)
    {
        return roll->done || WorldTimer::getMSTimeDiff(roll->startMs, now) > LOOT_ROLL_TIMEOUT;
    }), context.rolls.end());

    context.UpdateTrade();
    return EActionRunStatus::Running;
}

void UIInteractionProvider_GlobalTask::OnExit(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(UIInteractionProvider_GlobalTask::OnExit);
    UIInteractionProvider_GlobalTaskContext& context = ContextAs<UIInteractionProvider_GlobalTaskContext>(baseContext);
    context.Unsubscribe();
    context.rolls.clear();
    context.tradeOpenRequested = false;
    context.tradeAcceptRequested = false;
}

const std::vector<STAI_PropertyDef>& UIInteractionProvider_GlobalTask::GetProperties() const
{
    static const std::vector<STAI_PropertyDef> properties =
    {
        STAI_PropertyDef::Of("rolls", &UIInteractionProvider_GlobalTaskContext::rolls),
        STAI_PropertyDef::Of("tradeOpenRequested", &UIInteractionProvider_GlobalTaskContext::tradeOpenRequested),
        STAI_PropertyDef::Of("tradeAcceptRequested", &UIInteractionProvider_GlobalTaskContext::tradeAcceptRequested),
    };
    return properties;
}

const std::vector<STAI_DelegateDef>& UIInteractionProvider_GlobalTask::GetDelegates() const
{
    static const std::vector<STAI_DelegateDef> delegates =
    {
        STAI_DelegateDef::Of("onRollRequested", &UIInteractionProvider_GlobalTaskContext::onRollRequested),
        STAI_DelegateDef::Of("onTradeRequested", &UIInteractionProvider_GlobalTaskContext::onTradeRequested),
        STAI_DelegateDef::Of("onTradeAccepted", &UIInteractionProvider_GlobalTaskContext::onTradeAccepted),
    };
    return delegates;
}

// ---------------------------------------------------------------- context

void UIInteractionProvider_GlobalTaskContext::Subscribe()
{
    if (subscribed || !bot)
        return;

    subscribed = true;
    if (TeamSubsystem* teams = sWorld.GetSubsystem<TeamSubsystem>())
        teams->AddOnLootRollStarted(bot->GetObjectGuid(), this, &UIInteractionProvider_GlobalTaskContext::OnLootRollStarted);
    // the trade is the bot's own: its SMSG_TRADE_STATUS
    ai->onTradeStatus.Add(this, &UIInteractionProvider_GlobalTaskContext::OnTradeStatus);
}

void UIInteractionProvider_GlobalTaskContext::Unsubscribe()
{
    if (!subscribed)
        return;

    subscribed = false;
    if (TeamSubsystem* teams = sWorld.GetSubsystem<TeamSubsystem>())
        teams->RemoveAll(bot->GetObjectGuid(), this);
    ai->onTradeStatus.RemoveAll(this);
}

void UIInteractionProvider_GlobalTaskContext::OnLootRollStarted(ObjectGuid self, ObjectGuid lootGuid, uint32 itemSlot, uint32 itemId, uint8 voteMask)
{
    auto roll = std::make_shared<STAI_ItemRoll>();
    roll->lootGuid = lootGuid;
    roll->itemSlot = itemSlot;
    roll->itemId = itemId;
    roll->voteMask = voteMask;
    roll->startMs = WorldTimer::getMSTime();
    rolls.push_back(std::move(roll));
    onRollRequested.Broadcast();
}

void UIInteractionProvider_GlobalTaskContext::OnTradeStatus(uint32 status)
{
    switch (status)
    {
        case TRADE_STATUS_BEGIN_TRADE:          // a player opened a trade with the bot
            tradeOpenRequested = true;
            onTradeRequested.Broadcast();
            break;
        case TRADE_STATUS_OPEN_WINDOW:          // both windows are open
            tradeOpenRequested = false;
            break;
        case TRADE_STATUS_TRADE_ACCEPT:         // the other player accepted
        case TRADE_STATUS_BACK_TO_TRADE:        // something changed: the other player may still have accepted
            UpdateTrade();
            if (tradeAcceptRequested)
                onTradeAccepted.Broadcast();
            break;
        case TRADE_STATUS_TRADE_CANCELED:
        case TRADE_STATUS_TRADE_COMPLETE:
        case TRADE_STATUS_CLOSE_WINDOW:
            tradeOpenRequested = false;
            tradeAcceptRequested = false;
            break;
        default:
            break;
    }
}

void UIInteractionProvider_GlobalTaskContext::UpdateTrade()
{
    TradeData* trade = bot->GetTradeData();
    TradeData* other = trade ? trade->GetTraderData() : nullptr;
    if (!trade)
        tradeOpenRequested = false;
    tradeAcceptRequested = !tradeOpenRequested && other && other->IsAccepted() && !trade->IsAccepted();
}
