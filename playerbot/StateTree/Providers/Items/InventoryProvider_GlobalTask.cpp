#include "InventoryProvider_GlobalTask.h"

#include "playerbot/ItemEvalSubsystem.h"
#include "playerbot/PlayerbotAI.h"
#include "playerbot/StateTree/STAI_TaskFactory.h"

#include "Entities/Bag.h"
#include "Groups/TeamSubsystem.h"
#include "World/World.h"

#include <algorithm>
#include <stdexcept>

using namespace ai;

namespace
{
    constexpr uint32 checkIntervalMs = 2000;
    constexpr uint32 keepItemMs = 10000;    // an item to equip still in the bags by then cannot be equipped

    uint32 CountItems(PlayerbotAI* ai, const char* kind)
    {
        uint32 count = 0;
        for (Item* item : ai->InventoryParseItems(kind, IterateItemsMask::ITERATE_ITEMS_IN_BAGS))
            count += item->GetCount();
        return count;
    }

    bool IsInBags(const Item* item)
    {
        return item && (item->IsInBag() || (item->GetSlot() >= INVENTORY_SLOT_ITEM_START && item->GetSlot() < INVENTORY_SLOT_ITEM_END));
    }
}

uint32 InventoryProvider_GlobalTask::ParseCount(const STAI_TaskConfig& config, const std::string& name, const char* fallback)
{
    const std::string text = STAI_TaskFactory::GetSetting(config, name).value_or(fallback);
    char* end = nullptr;
    const unsigned long value = std::strtoul(text.c_str(), &end, 10);
    if (end == text.c_str() || *end)
        throw std::runtime_error("<" + name + " value=\"" + text + "\">: expected a count");
    return uint32(value);
}

void InventoryProvider_GlobalTask::Validate(const STAI_TaskConfig& config)
{
    ParseCount(config, "Low", "5");
    ParseCount(config, "Free", "2");
}

std::unique_ptr<STAI_TaskBase> InventoryProvider_GlobalTask::Create(const STAI_TaskConfig& config)
{
    return std::make_unique<InventoryProvider_GlobalTask>(ParseCount(config, "Low", "5"), ParseCount(config, "Free", "2"));
}

void InventoryProvider_GlobalTask::ForEachBagItem(Player* bot, const std::function<void(Item*)>& visit)
{
    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        if (Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            visit(item);

    for (uint8 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
        if (Bag* bag = static_cast<Bag*>(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, bagSlot)))
            for (uint32 slot = 0; slot < bag->GetBagSize(); ++slot)
                if (Item* item = bag->GetItemByPos(uint8(slot)))
                    visit(item);
}

uint32 InventoryProvider_GlobalTask::CountFreeSlots(Player* bot)
{
    uint32 free = 0;
    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        if (!bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            ++free;

    for (uint8 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
        if (Bag* bag = static_cast<Bag*>(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, bagSlot)))
            free += bag->GetFreeSlots();
    return free;
}

void InventoryProvider_GlobalTask::Evaluate(InventoryProvider_GlobalTaskContext& context)
{
    Player* bot = context.bot;
    const ObjectGuid previous = context.equipItem;
    context.equipItem = ObjectGuid();
    context.sellable = context.ahCount = context.equipCount = 0;
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

    ForEachBagItem(bot, [&](Item* item)
    {
        switch (itemEval->Evaluate(bot, item))
        {
            case EItemEval::Equip:
                ++context.equipCount;
                if (context.equipItem.IsEmpty() && std::find(context.ignored.begin(), context.ignored.end(), item->GetObjectGuid()) == context.ignored.end())
                    context.equipItem = item->GetObjectGuid();
                break;
            case EItemEval::Vendor: ++context.sellable; break;
            case EItemEval::AH:     ++context.ahCount; break;
            case EItemEval::Keep:   break;
        }
    });

    if (context.equipItem != previous)
        context.itemSinceMs = 0;
    if (!context.equipItem.IsEmpty() && context.equipItem != previous)
        context.onItemEquipRequest.Broadcast();
}

void InventoryProvider_GlobalTask::Update(InventoryProvider_GlobalTaskContext& context) const
{
    const bool wasLow = context.lowFood || context.lowDrink;
    context.food = CountItems(context.ai, "food");
    context.drink = CountItems(context.ai, "drink");
    context.lowFood = context.food < low;
    context.lowDrink = context.drink < low;
    if ((context.lowFood || context.lowDrink) && !wasLow)
        context.onLowSupplies.Broadcast();

    Evaluate(context);

    const bool wasFull = context.bagsFull;
    context.freeSlots = CountFreeSlots(context.bot);
    context.bagsFull = context.freeSlots < free && context.sellable;
    if (context.bagsFull && !wasFull)
        context.onBagsFull.Broadcast();
}

EActionRunStatus InventoryProvider_GlobalTask::OnEnterState(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(InventoryProvider_GlobalTask::OnEnterState);
    InventoryProvider_GlobalTaskContext& context = ContextAs<InventoryProvider_GlobalTaskContext>(baseContext);
    context.lowFood = context.lowDrink = context.bagsFull = false;
    context.equipItem = ObjectGuid();
    context.sinceCheckMs = 0;
    context.itemSinceMs = 0;
    context.itemReceived = false;
    context.ignored.clear();
    context.Subscribe();
    Update(context);
    return EActionRunStatus::Running;
}

void InventoryProvider_GlobalTask::OnExitState(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(InventoryProvider_GlobalTask::OnExitState);
    ContextAs<InventoryProvider_GlobalTaskContext>(baseContext).Unsubscribe();
}

void InventoryProvider_GlobalTaskContext::Subscribe()
{
    if (!subscribed.IsEmpty() || !bot)
        return;

    subscribed = bot->GetObjectGuid();
    if (TeamSubsystem* teams = sWorld.GetSubsystem<TeamSubsystem>())
        teams->AddOnItemReceived(subscribed, this, &InventoryProvider_GlobalTaskContext::OnItemReceived);
}

void InventoryProvider_GlobalTaskContext::Unsubscribe()
{
    if (subscribed.IsEmpty())
        return;

    if (TeamSubsystem* teams = sWorld.GetSubsystem<TeamSubsystem>())
        teams->RemoveAll(subscribed, this);
    subscribed.Clear();
}

EActionRunStatus InventoryProvider_GlobalTask::OnTick(STAI_TaskContext& baseContext, uint32 elapsed, bool minimal) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(InventoryProvider_GlobalTask::OnTick);
    InventoryProvider_GlobalTaskContext& context = ContextAs<InventoryProvider_GlobalTaskContext>(baseContext);
    context.sinceCheckMs += elapsed;
    // equipped (or gone): the next one at once, not after the interval
    bool equipDone = false;
    if (!context.equipItem.IsEmpty())
    {
        context.itemSinceMs += elapsed;
        equipDone = !IsInBags(context.bot->GetItemByGuid(context.equipItem));
    }
    if (context.sinceCheckMs >= checkIntervalMs || context.itemReceived || equipDone)
    {
        context.sinceCheckMs = 0;
        context.itemReceived = false;
        Update(context);
    }
    return EActionRunStatus::Running;
}

const std::vector<STAI_PropertyDef>& InventoryProvider_GlobalTask::GetProperties() const
{
    static const std::vector<STAI_PropertyDef> properties =
    {
        STAI_PropertyDef::Of("food", &InventoryProvider_GlobalTaskContext::food),
        STAI_PropertyDef::Of("drink", &InventoryProvider_GlobalTaskContext::drink),
        STAI_PropertyDef::Of("lowFood", &InventoryProvider_GlobalTaskContext::lowFood),
        STAI_PropertyDef::Of("lowDrink", &InventoryProvider_GlobalTaskContext::lowDrink),
        STAI_PropertyDef::Of("freeSlots", &InventoryProvider_GlobalTaskContext::freeSlots),
        STAI_PropertyDef::Of("sellable", &InventoryProvider_GlobalTaskContext::sellable),
        STAI_PropertyDef::Of("ahCount", &InventoryProvider_GlobalTaskContext::ahCount),
        STAI_PropertyDef::Of("bagsFull", &InventoryProvider_GlobalTaskContext::bagsFull),
        STAI_PropertyDef::Of("equipItem", &InventoryProvider_GlobalTaskContext::equipItem),
        STAI_PropertyDef::Of("equipCount", &InventoryProvider_GlobalTaskContext::equipCount),
    };
    return properties;
}

const std::vector<STAI_DelegateDef>& InventoryProvider_GlobalTask::GetDelegates() const
{
    static const std::vector<STAI_DelegateDef> delegates =
    {
        STAI_DelegateDef::Of("onLowSupplies", &InventoryProvider_GlobalTaskContext::onLowSupplies),
        STAI_DelegateDef::Of("onBagsFull", &InventoryProvider_GlobalTaskContext::onBagsFull),
        STAI_DelegateDef::Of("onItemEquipRequest", &InventoryProvider_GlobalTaskContext::onItemEquipRequest),
    };
    return delegates;
}
