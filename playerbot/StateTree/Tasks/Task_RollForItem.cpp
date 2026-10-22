#include "Task_RollForItem.h"

#include "playerbot/PlayerbotAI.h"

#include "Loot/LootMgr.h"

using namespace ai;

void Task_RollForItem::OnEnter(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(Task_RollForItem::OnEnter);
    Task_RollForItemContext& context = ContextAs<Task_RollForItemContext>(baseContext);
    Player* bot = context.bot;

    for (const std::shared_ptr<STAI_ItemRoll>& roll : context.rolls)
    {
        if (roll->done)
            continue;

        // TODO: need for upgrades / class items (the legacy "loot roll" action rated the item for the bot)
        const RollVote vote = (roll->voteMask & ROLL_VOTE_MASK_GREED) ? ROLL_GREED : ROLL_PASS;
        if (bot->GetGroup())
            sLootMgr.PlayerVote(bot, roll->lootGuid, roll->itemSlot, vote);
        roll->done = true;
    }
}

const std::vector<STAI_PropertyDef>& Task_RollForItem::GetProperties() const
{
    static const std::vector<STAI_PropertyDef> properties = { STAI_PropertyDef::Of("rolls", &Task_RollForItemContext::rolls) };
    return properties;
}
