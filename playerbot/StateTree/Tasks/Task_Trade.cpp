#include "Task_Trade.h"

#include "playerbot/PlayerbotAI.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/StateTree/Providers/UIInteractionProvider_GlobalTask.h"

using namespace ai;

namespace
{
    void CancelTrade(Player* bot)
    {
        WorldPacket packet;
        packet << uint32(0);
        bot->GetSession()->HandleCancelTradeOpcode(packet);
    }
}

EActionRunStatus Task_OpenTrade::OnEnter(STAI_TaskContext& context) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(Task_OpenTrade::OnEnter);
    StartLog(context);
    Player* bot = context.bot;
    Player* trader = bot->GetTrader();
    if (!trader || !bot->GetTradeData())
        return EActionRunStatus::Succeeded;

    if (!STAI_CanTradeWith(context.ai, bot, trader))
    {
        CancelTrade(bot);
        return LogStatus(context, EActionRunStatus::Failed);
    }

    if (!sServerFacade.IsInFront(bot, trader, sPlayerbotAIConfig.sightDistance, CAST_ANGLE_IN_FRONT))
        sServerFacade.SetFacingTo(bot, trader);

    WorldPacket packet;
    bot->GetSession()->HandleBeginTradeOpcode(packet);
    // TODO(strategy): the legacy action also told the trader the bot's inventory (ListItemsVisitor)
    return EActionRunStatus::Succeeded;
}

EActionRunStatus Task_AcceptTrade::OnEnter(STAI_TaskContext& context) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(Task_AcceptTrade::OnEnter);
    StartLog(context);
    Player* bot = context.bot;
    Player* trader = bot->GetTrader();
    TradeData* trade = bot->GetTradeData();
    if (!trader || !trade || trade->IsAccepted())
        return EActionRunStatus::Succeeded;

    if (!STAI_CanTradeWith(context.ai, bot, trader))
    {
        CancelTrade(bot);
        return LogStatus(context, EActionRunStatus::Failed);
    }

    // TODO(strategy): random bots priced the trade (TradeStatusAction::CheckTrade: sell / buy prices, discount) and
    // thanked the trader for items; the bots of a player accept whatever it put in or asked for
    WorldPacket packet;
    packet << uint32(0);
    bot->GetSession()->HandleAcceptTradeOpcode(packet);
    return EActionRunStatus::Succeeded;
}
