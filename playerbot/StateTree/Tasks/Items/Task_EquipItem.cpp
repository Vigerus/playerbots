#include "Task_EquipItem.h"

#include "playerbot/ItemEvalSubsystem.h"
#include "playerbot/PlayerbotAI.h"
#include "playerbot/ServerFacade.h"

#include "Entities/Item.h"
#include "Server/WorldSession.h"
#include "World/World.h"

#include <sstream>

using namespace ai;

EActionRunStatus Task_EquipItem::OnEnterState(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(Task_EquipItem::OnEnterState);
    Task_EquipItemContext& context = ContextAs<Task_EquipItemContext>(baseContext);
    Player* bot = context.bot;
    context.result.clear();
    Item* item = !context.item.IsEmpty() && bot->IsInWorld() ? bot->GetItemByGuid(context.item) : nullptr;
    ItemEvalSubsystem* itemEval = sWorld.GetSubsystem<ItemEvalSubsystem>();
    if (!item || !itemEval || !sServerFacade.IsAlive(bot))
        return EActionRunStatus::Failed;

    StartLog(context);
    std::ostringstream out;
    out << item->GetProto()->Name1;
    const uint8 slot = itemEval->FindEquipSlot(bot, item);
    if (slot == NULL_SLOT)
    {
        out << ": not better any more";
        context.result = out.str();
        return LogStatus(context, EActionRunStatus::Failed);
    }

    WorldPacket swap(CMSG_SWAP_ITEM, 4);
    swap << uint8(INVENTORY_SLOT_BAG_0) << slot << item->GetBagSlot() << item->GetSlot();
    bot->GetSession()->HandleSwapItem(swap);

    // the item object stays the same, only its position changes
    const bool worn = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot) == item;
    out << (worn ? " to slot " : ": refused for slot ") << uint32(slot);
    context.result = out.str();
    return LogStatus(context, worn ? EActionRunStatus::Succeeded : EActionRunStatus::Failed);
}

std::string Task_EquipItem::GetActivityInfo(const STAI_TaskContext& baseContext) const
{
    const Task_EquipItemContext& context = static_cast<const Task_EquipItemContext&>(baseContext);
    return context.result.empty() ? GetDebugInfo() : GetDebugInfo() + ": " + context.result;
}

const std::vector<STAI_PropertyDef>& Task_EquipItem::GetProperties() const
{
    static const std::vector<STAI_PropertyDef> properties = { STAI_PropertyDef::Of("item", &Task_EquipItemContext::item) };
    return properties;
}
