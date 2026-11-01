#include "Task_SellJunk.h"

#include "playerbot/PlayerbotAI.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/StateTree/Providers/InventoryProvider_GlobalTask.h"

#include "Entities/Creature.h"
#include "Server/WorldSession.h"

#include <sstream>

using namespace ai;

EActionRunStatus Task_SellJunk::OnEnterState(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(Task_SellJunk::OnEnterState);
    Task_SellJunkContext& context = ContextAs<Task_SellJunkContext>(baseContext);
    Player* bot = context.bot;
    context.result.clear();
    Creature* vendor = !context.vendor.IsEmpty() && bot->IsInWorld() ? bot->GetMap()->GetCreature(context.vendor) : nullptr;
    if (!vendor || !sServerFacade.IsAlive(bot))
        return EActionRunStatus::Failed;

    StartLog(context);
    std::vector<ObjectGuid> items;
    InventoryProvider_GlobalTask::ForEachBagItem(bot, [&items](Item* item)
    {
        if (InventoryProvider_GlobalTask::IsSellable(item))
            items.push_back(item->GetObjectGuid());
    });

    // the guids first: a sold item leaves the bags while the list is walked
    const uint32 moneyBefore = bot->GetMoney();
    uint32 sold = 0;
    for (const ObjectGuid& guid : items)
    {
        Item* item = bot->GetItemByGuid(guid);
        if (!item)
            continue;

        WorldPacket sell(CMSG_SELL_ITEM, 8 + 8 + 1);
        sell << vendor->GetObjectGuid() << guid << uint8(item->GetCount());
        bot->GetSession()->HandleSellItemOpcode(sell);
        if (!bot->GetItemByGuid(guid))
            ++sold;
    }

    std::ostringstream out;
    out << "sold " << sold << " of " << items.size() << ", money " << moneyBefore << " -> " << bot->GetMoney();
    context.result = out.str();
    // nothing left to sell is done too: the bags are clean, the Sell subtree may finish
    return LogStatus(context, sold || items.empty() ? EActionRunStatus::Succeeded : EActionRunStatus::Failed);
}

std::string Task_SellJunk::GetActivityInfo(const STAI_TaskContext& baseContext) const
{
    const Task_SellJunkContext& context = static_cast<const Task_SellJunkContext&>(baseContext);
    return context.result.empty() ? GetDebugInfo() : GetDebugInfo() + ": " + context.result;
}

const std::vector<STAI_PropertyDef>& Task_SellJunk::GetProperties() const
{
    static const std::vector<STAI_PropertyDef> properties = { STAI_PropertyDef::Of("vendor", &Task_SellJunkContext::vendor) };
    return properties;
}
