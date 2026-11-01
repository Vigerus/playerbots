#include "StatsProvider_GlobalTask.h"

#include "playerbot/PlayerbotAI.h"

#include <cmath>

using namespace ai;

namespace
{
    // the change delegates fire at most this often
    constexpr uint32 minFireIntervalMs = 1000;
}

float StatsProvider_GlobalTask::HealthPercent(const Player* bot)
{
    return bot->GetMaxHealth() ? bot->GetHealthPercent() : 100.0f;
}

float StatsProvider_GlobalTask::ManaPercent(const Player* bot)
{
    // the bot's current power: a druid in a form drinks no mana either
    if (bot->GetPowerType() != POWER_MANA)
        return 100.0f;
    return bot->GetPowerPercent(POWER_MANA);
}

void StatsProvider_GlobalTask::UpdateStat(float value, float& percent, int& lastWhole, uint32& firedMs, FSTAI_Delegate& changed, uint32 now)
{
    percent = value;
    const int wholePercent = int(std::floor(percent));
    if (wholePercent == lastWhole)
        return;

    // a pending change stays different from lastWhole, so it fires once the interval is over
    if (lastWhole >= 0 && WorldTimer::getMSTimeDiff(firedMs, now) < minFireIntervalMs)
        return;

    lastWhole = wholePercent;
    firedMs = now;
    changed.Broadcast();
}

EActionRunStatus StatsProvider_GlobalTask::OnEnterState(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(StatsProvider_GlobalTask::OnEnterState);
    StatsProvider_GlobalTaskContext& context = ContextAs<StatsProvider_GlobalTaskContext>(baseContext);
    context.healthPct = HealthPercent(context.bot);
    context.manaPct = ManaPercent(context.bot);
    // the first update fires, so transitions see the values the tree starts with
    context.lastHealth = context.lastMana = -1;
    return EActionRunStatus::Running;
}

EActionRunStatus StatsProvider_GlobalTask::OnTick(STAI_TaskContext& baseContext, uint32 elapsed, bool minimal) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(StatsProvider_GlobalTask::OnTick);
    StatsProvider_GlobalTaskContext& context = ContextAs<StatsProvider_GlobalTaskContext>(baseContext);
    const uint32 now = WorldTimer::getMSTime();
    UpdateStat(HealthPercent(context.bot), context.healthPct, context.lastHealth, context.healthFiredMs, context.onHealthChanged, now);
    UpdateStat(ManaPercent(context.bot), context.manaPct, context.lastMana, context.manaFiredMs, context.onManaChanged, now);
    return EActionRunStatus::Running;
}

const std::vector<STAI_PropertyDef>& StatsProvider_GlobalTask::GetProperties() const
{
    static const std::vector<STAI_PropertyDef> properties =
    {
        STAI_PropertyDef::Of("healthPct", &StatsProvider_GlobalTaskContext::healthPct),
        STAI_PropertyDef::Of("manaPct", &StatsProvider_GlobalTaskContext::manaPct),
    };
    return properties;
}

const std::vector<STAI_DelegateDef>& StatsProvider_GlobalTask::GetDelegates() const
{
    static const std::vector<STAI_DelegateDef> delegates =
    {
        STAI_DelegateDef::Of("onHealthChanged", &StatsProvider_GlobalTaskContext::onHealthChanged),
        STAI_DelegateDef::Of("onManaChanged", &StatsProvider_GlobalTaskContext::onManaChanged),
    };
    return delegates;
}
