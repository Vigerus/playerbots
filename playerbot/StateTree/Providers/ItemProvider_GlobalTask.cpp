#include "ItemProvider_GlobalTask.h"

#include "playerbot/ItemEvalSubsystem.h"
#include "playerbot/PlayerbotAI.h"
#include "playerbot/StateTree/Providers/InventoryProvider_GlobalTask.h"

#include "Entities/Item.h"
#include "Entities/Player.h"
#include "Groups/TeamSubsystem.h"
#include "World/World.h"

#include <algorithm>

using namespace ai;

namespace
{
    constexpr uint32 checkIntervalMs = 2000;
    constexpr uint32 keepItemMs = 10000;
}

void ItemProvider_GlobalTask::Update(ItemProvider_GlobalTaskContext& context)
{
    Player* bot = context.bot;
    const ObjectGuid previous = context.equipItem;
    context.equipItem = ObjectGuid();
    context.equipCount = context.vendorCount = context.ahCount = 0;
    if (!previous.IsEmpty() && context.itemSinceMs >= keepItemMs)
    {
        context.ignored.push_back(previous);
        context.itemSinceMs = 0;
    }

    ItemEvalSubsystem* itemEval = sWorld.GetSubsystem<ItemEvalSubsystem>();
    if (!itemEval || !bot->IsInWorld())
        return;

    // an ignored item that left the bags is not met again
    context.ignored.erase(std::remove_if(context.ignored.begin(), context.ignored.end(),
        [bot](ObjectGuid guid) { return !bot->GetItemByGuid(guid); }), context.ignored.end());

    InventoryProvider_GlobalTask::ForEachBagItem(bot, [&](Item* item)
    {
        switch (itemEval->Evaluate(bot, item))
        {
            case EItemEval::Equip:
                ++context.equipCount;
                if (context.equipItem.IsEmpty() && std::find(context.ignored.begin(), context.ignored.end(), item->GetObjectGuid()) == context.ignored.end())
                    context.equipItem = item->GetObjectGuid();
                break;
            case EItemEval::Vendor: ++context.vendorCount; break;
            case EItemEval::AH:     ++context.ahCount; break;
            case EItemEval::Keep:   break;
        }
    });

    if (context.equipItem != previous)
        context.itemSinceMs = 0;
    if (!context.equipItem.IsEmpty() && context.equipItem != previous)
        context.onItemEquipRequest.Broadcast();
}

EActionRunStatus ItemProvider_GlobalTask::OnEnterState(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(ItemProvider_GlobalTask::OnEnterState);
    ItemProvider_GlobalTaskContext& context = ContextAs<ItemProvider_GlobalTaskContext>(baseContext);
    context.equipItem = ObjectGuid();
    context.sinceCheckMs = 0;
    context.itemSinceMs = 0;
    context.itemReceived = false;
    context.ignored.clear();
    context.Subscribe();
    Update(context);
    return EActionRunStatus::Running;
}

void ItemProvider_GlobalTask::OnExitState(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(ItemProvider_GlobalTask::OnExitState);
    ContextAs<ItemProvider_GlobalTaskContext>(baseContext).Unsubscribe();
}

void ItemProvider_GlobalTaskContext::Subscribe()
{
    if (!subscribed.IsEmpty() || !bot)
        return;

    subscribed = bot->GetObjectGuid();
    if (TeamSubsystem* teams = sWorld.GetSubsystem<TeamSubsystem>())
        teams->AddOnItemReceived(subscribed, this, &ItemProvider_GlobalTaskContext::OnItemReceived);
}

void ItemProvider_GlobalTaskContext::Unsubscribe()
{
    if (subscribed.IsEmpty())
        return;

    if (TeamSubsystem* teams = sWorld.GetSubsystem<TeamSubsystem>())
        teams->RemoveAll(subscribed, this);
    subscribed.Clear();
}

EActionRunStatus ItemProvider_GlobalTask::OnTick(STAI_TaskContext& baseContext, uint32 elapsed, bool minimal) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(ItemProvider_GlobalTask::OnTick);
    ItemProvider_GlobalTaskContext& context = ContextAs<ItemProvider_GlobalTaskContext>(baseContext);
    context.sinceCheckMs += elapsed;
    // equipped (or gone): the next one at once, not after the interval
    bool equipDone = false;
    if (!context.equipItem.IsEmpty())
    {
        context.itemSinceMs += elapsed;
        Item* item = context.bot->GetItemByGuid(context.equipItem);
        const bool inBags = item && (item->IsInBag() || (item->GetSlot() >= INVENTORY_SLOT_ITEM_START && item->GetSlot() < INVENTORY_SLOT_ITEM_END));
        equipDone = !inBags;
    }
    if (context.sinceCheckMs >= checkIntervalMs || context.itemReceived || equipDone)
    {
        context.sinceCheckMs = 0;
        context.itemReceived = false;
        Update(context);
    }
    return EActionRunStatus::Running;
}

const std::vector<STAI_PropertyDef>& ItemProvider_GlobalTask::GetProperties() const
{
    static const std::vector<STAI_PropertyDef> properties =
    {
        STAI_PropertyDef::Of("equipItem", &ItemProvider_GlobalTaskContext::equipItem),
        STAI_PropertyDef::Of("equipCount", &ItemProvider_GlobalTaskContext::equipCount),
        STAI_PropertyDef::Of("vendorCount", &ItemProvider_GlobalTaskContext::vendorCount),
        STAI_PropertyDef::Of("ahCount", &ItemProvider_GlobalTaskContext::ahCount),
    };
    return properties;
}

const std::vector<STAI_DelegateDef>& ItemProvider_GlobalTask::GetDelegates() const
{
    static const std::vector<STAI_DelegateDef> delegates =
    {
        STAI_DelegateDef::Of("onItemEquipRequest", &ItemProvider_GlobalTaskContext::onItemEquipRequest),
    };
    return delegates;
}
