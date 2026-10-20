#include "TargetProvider_GlobalTask.h"

#include "playerbot/PlayerbotAI.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/values/PossibleAttackTargetsValue.h"
#include "playerbot/strategy/values/RtiTargetValue.h"

#include "Groups/Group.h"
#include "Maps/Map.h"

#include <list>
#include <sstream>

using namespace ai;

namespace
{
    // how often a new target is chosen (the current one is checked every update)
    constexpr uint32 chooseIntervalMs = 250;

    // the living units marked with the icons of the bot's rti value, in the value's order
    std::vector<Unit*> GetMarkedUnits(const STAI_TaskContext& context, const std::string& rtiValue)
    {
        std::vector<Unit*> units;
        Player* bot = context.bot;
        Group* group = bot->GetGroup();
        if (!group)
            return units;

        for (const std::string& icon : context.ai->GetAiObjectContext()->GetValue<std::vector<std::string>>(rtiValue)->Get())
        {
            const int index = RtiTargetValue::GetRtiIndex(icon);
            if (index < 0)
                continue;

            const ObjectGuid guid = group->GetTargetIcon(index);
            if (guid.IsEmpty())
                continue;

            Unit* unit = bot->GetMap()->GetUnit(guid);
            if (unit && unit->IsInWorld() && sServerFacade.IsAlive(unit))
                units.push_back(unit);
        }
        return units;
    }
}

bool TargetProvider_GlobalTask::IsValidTarget(const STAI_TaskContext& context, Unit* unit)
{
    Player* bot = context.bot;
    if (!unit || !unit->IsInWorld() || !sServerFacade.IsAlive(unit) || unit->GetMap() != bot->GetMap())
        return false;
    if (!sServerFacade.IsHostileTo(bot, unit) || !bot->IsWithinDistInMap(unit, sPlayerbotAIConfig.sightDistance))
        return false;
    if (PossibleAttackTargetsValue::HasBreakableCC(unit, bot) || PossibleAttackTargetsValue::HasUnBreakableCC(unit, bot))
        return false;

    // the bot's own crowd-control mark is left for CC
    for (Unit* ccMark : GetMarkedUnits(context, "rti cc"))
        if (ccMark == unit)
            return false;
    return true;
}

Unit* TargetProvider_GlobalTask::FindMarkedTarget(const STAI_TaskContext& context)
{
    for (Unit* unit : GetMarkedUnits(context, "rti"))
        if (IsValidTarget(context, unit))
            return unit;
    return nullptr;
}

Unit* TargetProvider_GlobalTask::FindAssistTarget(const STAI_TaskContext& context)
{
    // the value caches its list (the expensive part: units around the bot, its group and master)
    const std::list<ObjectGuid> attackers = context.ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("possible attack targets")->Get();

    Unit* best = nullptr;
    for (const ObjectGuid& guid : attackers)
    {
        Unit* unit = context.ai->GetUnit(guid);
        if (!unit || unit->IsPlayer() || !IsValidTarget(context, unit))
            continue;
        if (!best || unit->GetHealth() < best->GetHealth())
            best = unit;
    }
    return best;
}

void TargetProvider_GlobalTask::SetTarget(TargetProvider_GlobalTaskContext& context, Unit* unit, bool marked)
{
    const ObjectGuid guid = unit ? unit->GetObjectGuid() : ObjectGuid();
    context.marked = unit && marked;
    if (guid == context.target)
        return;

    context.target = guid;
    // the legacy values (reaction engine, chat commands) see the same target
    context.ai->GetAiObjectContext()->GetValue<Unit*>("current target")->Set(unit);
    context.onTargetChanged.Broadcast();
}

void TargetProvider_GlobalTask::Update(TargetProvider_GlobalTaskContext& context, bool choose) const
{
    Player* bot = context.bot;
    if (!bot->IsInWorld() || !sServerFacade.IsAlive(bot))
    {
        SetTarget(context, nullptr, false);
        return;
    }

    Unit* current = context.target.IsEmpty() ? nullptr : bot->GetMap()->GetUnit(context.target);
    const bool currentValid = current && IsValidTarget(context, current);

    // the current target stays until the next choice, unless it died / got crowd-controlled
    if (!choose && (currentValid || !current))
    {
        if (!current)
            choose = true;
        else
            return;
    }

    // a living mark always wins
    if (Unit* marked = FindMarkedTarget(context))
    {
        SetTarget(context, marked, true);
        return;
    }

    // without marks: keep a valid fallback target, else the dps assist choice
    if (currentValid && !context.marked)
        return;
    SetTarget(context, FindAssistTarget(context), false);
}

void TargetProvider_GlobalTask::OnEnter(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(TargetProvider_GlobalTask::OnEnter);
    TargetProvider_GlobalTaskContext& context = ContextAs<TargetProvider_GlobalTaskContext>(baseContext);
    context.target.Clear();
    context.marked = false;
    context.sinceCheckMs = 0;
    Update(context, true);
}

void TargetProvider_GlobalTask::OnExit(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(TargetProvider_GlobalTask::OnExit);
    TargetProvider_GlobalTaskContext& context = ContextAs<TargetProvider_GlobalTaskContext>(baseContext);
    context.target.Clear();
    context.marked = false;
}

EActionRunStatus TargetProvider_GlobalTask::OnTick(STAI_TaskContext& baseContext, uint32 elapsed, bool minimal) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(TargetProvider_GlobalTask::OnTick);
    TargetProvider_GlobalTaskContext& context = ContextAs<TargetProvider_GlobalTaskContext>(baseContext);
    context.sinceCheckMs += elapsed;
    const bool choose = context.sinceCheckMs >= chooseIntervalMs;
    if (choose)
        context.sinceCheckMs = 0;
    Update(context, choose);
    return EActionRunStatus::Running;
}

const std::vector<STAI_PropertyDef>& TargetProvider_GlobalTask::GetProperties() const
{
    static const std::vector<STAI_PropertyDef> properties = { STAI_PropertyDef::Of("target", &TargetProvider_GlobalTaskContext::target) };
    return properties;
}

const std::vector<STAI_DelegateDef>& TargetProvider_GlobalTask::GetDelegates() const
{
    static const std::vector<STAI_DelegateDef> delegates = { STAI_DelegateDef::Of("onTargetChanged", &TargetProvider_GlobalTaskContext::onTargetChanged) };
    return delegates;
}

