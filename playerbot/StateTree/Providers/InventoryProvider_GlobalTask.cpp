#include "InventoryProvider_GlobalTask.h"

#include "playerbot/PlayerbotAI.h"
#include "playerbot/StateTree/STAI_TaskFactory.h"

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

uint32 InventoryProvider_GlobalTask::ParseLow(const STAI_TaskConfig& config)
{
    const std::string text = STAI_TaskFactory::GetSetting(config, "Low").value_or("5");
    char* end = nullptr;
    const unsigned long value = std::strtoul(text.c_str(), &end, 10);
    if (end == text.c_str() || *end)
        throw std::runtime_error("<Low value=\"" + text + "\">: expected a count");
    return uint32(value);
}

void InventoryProvider_GlobalTask::Validate(const STAI_TaskConfig& config)
{
    ParseLow(config);
}

std::unique_ptr<STAI_TaskBase> InventoryProvider_GlobalTask::Create(const STAI_TaskConfig& config)
{
    return std::make_unique<InventoryProvider_GlobalTask>(ParseLow(config));
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
}

EActionRunStatus InventoryProvider_GlobalTask::OnEnterState(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(InventoryProvider_GlobalTask::OnEnterState);
    InventoryProvider_GlobalTaskContext& context = ContextAs<InventoryProvider_GlobalTaskContext>(baseContext);
    context.lowFood = context.lowDrink = false;
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
    };
    return properties;
}

const std::vector<STAI_DelegateDef>& InventoryProvider_GlobalTask::GetDelegates() const
{
    static const std::vector<STAI_DelegateDef> delegates = { STAI_DelegateDef::Of("onLowSupplies", &InventoryProvider_GlobalTaskContext::onLowSupplies) };
    return delegates;
}
