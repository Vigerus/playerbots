#include "ResourceProvider_GlobalTask.h"

#include "playerbot/PlayerbotAI.h"

#include <cmath>
#include <sstream>

using namespace ai;

namespace
{
    // the change delegate fires at most this often
    constexpr uint32 minFireIntervalMs = 1000;
}

void ResourceProvider_GlobalTask::OnEnter(STAI_TaskContext& baseContext) const
{
    ResourceProvider_GlobalTaskContext& context = ContextAs<ResourceProvider_GlobalTaskContext>(baseContext);
    context.percent = ReadPercent(context.bot);
    // the first update fires, so transitions see the value the tree starts with
    context.lastWholePercent = -1;
}

EActionRunStatus ResourceProvider_GlobalTask::OnTick(STAI_TaskContext& baseContext, uint32 elapsed, bool minimal) const
{
    ResourceProvider_GlobalTaskContext& context = ContextAs<ResourceProvider_GlobalTaskContext>(baseContext);
    context.percent = ReadPercent(context.bot);

    const int wholePercent = int(std::floor(context.percent));
    if (wholePercent == context.lastWholePercent)
        return EActionRunStatus::Running;

    // a pending change stays different from lastWholePercent, so it fires once the interval is over
    const uint32 now = WorldTimer::getMSTime();
    if (context.lastWholePercent >= 0 && WorldTimer::getMSTimeDiff(context.lastFiredMs, now) < minFireIntervalMs)
        return EActionRunStatus::Running;

    context.lastWholePercent = wholePercent;
    context.lastFiredMs = now;
    context.onChanged.Broadcast();
    return EActionRunStatus::Running;
}

float HealthProvider_GlobalTask::ReadPercent(const Player* bot) const
{
    return bot->GetMaxHealth() ? bot->GetHealthPercent() : 100.0f;
}

const std::vector<STAI_PropertyDef>& HealthProvider_GlobalTask::GetProperties() const
{
    static const std::vector<STAI_PropertyDef> properties = { STAI_PropertyDef::Of("healthPct", &ResourceProvider_GlobalTaskContext::percent) };
    return properties;
}

const std::vector<STAI_DelegateDef>& HealthProvider_GlobalTask::GetDelegates() const
{
    static const std::vector<STAI_DelegateDef> delegates = { STAI_DelegateDef::Of("onHealthChanged", &ResourceProvider_GlobalTaskContext::onChanged) };
    return delegates;
}

float ManaProvider_GlobalTask::ReadPercent(const Player* bot) const
{
    // the bot's current power: a druid in a form drinks no mana either
    if (bot->GetPowerType() != POWER_MANA)
        return 100.0f;
    return bot->GetPowerPercent(POWER_MANA);
}

const std::vector<STAI_PropertyDef>& ManaProvider_GlobalTask::GetProperties() const
{
    static const std::vector<STAI_PropertyDef> properties = { STAI_PropertyDef::Of("manaPct", &ResourceProvider_GlobalTaskContext::percent) };
    return properties;
}

const std::vector<STAI_DelegateDef>& ManaProvider_GlobalTask::GetDelegates() const
{
    static const std::vector<STAI_DelegateDef> delegates = { STAI_DelegateDef::Of("onManaChanged", &ResourceProvider_GlobalTaskContext::onChanged) };
    return delegates;
}
