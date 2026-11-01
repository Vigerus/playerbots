#include "InventoryProvider_GlobalTask.h"

#include "playerbot/ItemEvalSubsystem.h"
#include "playerbot/PlayerbotAI.h"
#include "playerbot/StateTree/STAI_TaskFactory.h"

#include "Entities/Bag.h"
#include "World/World.h"

#include <stdexcept>

using namespace ai;

namespace
{
    constexpr uint32 checkIntervalMs = 2000;

    uint32 CountItems(PlayerbotAI* ai, const char* kind)
    {
        uint32 count = 0;
        for (Item* item : ai->InventoryParseItems(kind, IterateItemsMask::ITERATE_ITEMS_IN_BAGS))
            count += item->GetCount();
        return count;
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

void InventoryProvider_GlobalTask::Update(InventoryProvider_GlobalTaskContext& context) const
{
    const bool wasLow = context.lowFood || context.lowDrink;
    context.food = CountItems(context.ai, "food");
    context.drink = CountItems(context.ai, "drink");
    context.lowFood = context.food < low;
    context.lowDrink = context.drink < low;
    if ((context.lowFood || context.lowDrink) && !wasLow)
        context.onLowSupplies.Broadcast();

    const bool wasFull = context.bagsFull;
    context.freeSlots = CountFreeSlots(context.bot);
    context.sellable = 0;
    if (ItemEvalSubsystem* itemEval = sWorld.GetSubsystem<ItemEvalSubsystem>())
        ForEachBagItem(context.bot, [&context, itemEval](Item* item) { if (itemEval->Evaluate(context.bot, item) == EItemEval::Vendor) ++context.sellable; });
    context.bagsFull = context.freeSlots < free && context.sellable;
    if (context.bagsFull && !wasFull)
        context.onBagsFull.Broadcast();
}

EActionRunStatus InventoryProvider_GlobalTask::OnEnterState(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(InventoryProvider_GlobalTask::OnEnterState);
    InventoryProvider_GlobalTaskContext& context = ContextAs<InventoryProvider_GlobalTaskContext>(baseContext);
    context.lowFood = context.lowDrink = context.bagsFull = false;
    context.sinceCheckMs = 0;
    Update(context);
    return EActionRunStatus::Running;
}

EActionRunStatus InventoryProvider_GlobalTask::OnTick(STAI_TaskContext& baseContext, uint32 elapsed, bool minimal) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(InventoryProvider_GlobalTask::OnTick);
    InventoryProvider_GlobalTaskContext& context = ContextAs<InventoryProvider_GlobalTaskContext>(baseContext);
    context.sinceCheckMs += elapsed;
    if (context.sinceCheckMs >= checkIntervalMs)
    {
        context.sinceCheckMs = 0;
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
        STAI_PropertyDef::Of("bagsFull", &InventoryProvider_GlobalTaskContext::bagsFull),
    };
    return properties;
}

const std::vector<STAI_DelegateDef>& InventoryProvider_GlobalTask::GetDelegates() const
{
    static const std::vector<STAI_DelegateDef> delegates =
    {
        STAI_DelegateDef::Of("onLowSupplies", &InventoryProvider_GlobalTaskContext::onLowSupplies),
        STAI_DelegateDef::Of("onBagsFull", &InventoryProvider_GlobalTaskContext::onBagsFull),
    };
    return delegates;
}
