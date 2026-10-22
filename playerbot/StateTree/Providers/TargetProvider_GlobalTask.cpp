#include "TargetProvider_GlobalTask.h"

#include "playerbot/PlayerbotAI.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/StateTree/STAI_TaskFactory.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/values/PossibleAttackTargetsValue.h"
#include "playerbot/strategy/values/RtiTargetValue.h"

#include "Combat/ThreatManager.h"
#include "Groups/Group.h"
#include "Maps/Map.h"

#include <algorithm>
#include <cstdlib>
#include <list>
#include <sstream>
#include <stdexcept>

using namespace ai;

namespace
{
    // how often a new target is chosen (the current one is checked every update)
    constexpr uint32 chooseIntervalMs = 250;

    // the living units marked with the icons of the bot's rti value, in the value's order
    std::vector<Unit*> GetMarkedUnits(const STAI_TaskContext& context, Value<std::vector<std::string>>* rtiValue)
    {
        std::vector<Unit*> units;
        Player* bot = context.bot;
        Group* group = bot->GetGroup();
        if (!group || !rtiValue)
            return units;

        for (const std::string& icon : rtiValue->Get())
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

    // <WithThreat value="true|false"/>, default false
    bool ParseWithThreat(const STAI_TaskConfig& config)
    {
        const std::string value = STAI_TaskFactory::GetSetting(config, "WithThreat").value_or("false");
        if (value == "true" || value == "1")
            return true;
        if (value == "false" || value == "0")
            return false;
        throw std::runtime_error("<WithThreat value=\"...\">: expected true or false, got '" + value + "'");
    }

    // <ThreatAmount value="90"/>, percent of the highest other threat, default 90
    float ParseThreatShare(const STAI_TaskConfig& config)
    {
        const std::string value = STAI_TaskFactory::GetSetting(config, "ThreatAmount").value_or("90");
        char* end = nullptr;
        const double percent = std::strtod(value.c_str(), &end);
        if (value.empty() || *end || percent <= 0.0)
            throw std::runtime_error("<ThreatAmount value=\"...\">: expected a percent greater than 0, got '" + value + "'");
        return float(percent / 100.0);
    }
}

void TargetProvider_GlobalTask::Validate(const STAI_TaskConfig& config)
{
    ParseWithThreat(config);
    ParseThreatShare(config);
}

std::unique_ptr<STAI_Task> TargetProvider_GlobalTask::Create(const STAI_TaskConfig& config)
{
    return std::make_unique<TargetProvider_GlobalTask>(ParseWithThreat(config), ParseThreatShare(config));
}

bool TargetProvider_GlobalTask::IsValidTarget(const STAI_TaskContext& context, Unit* unit, const std::vector<Unit*>& ccMarks)
{
    Player* bot = context.bot;
    if (!unit || !unit->IsInWorld() || !sServerFacade.IsAlive(unit) || unit->GetMap() != bot->GetMap())
        return false;
    if (!sServerFacade.IsHostileTo(bot, unit) || !bot->IsWithinDistInMap(unit, sPlayerbotAIConfig.sightDistance))
        return false;
    if (PossibleAttackTargetsValue::HasBreakableCC(unit, bot) || PossibleAttackTargetsValue::HasUnBreakableCC(unit, bot))
        return false;

    // the bot's own crowd-control mark is left for CC
    return std::find(ccMarks.begin(), ccMarks.end(), unit) == ccMarks.end();
}

Unit* TargetProvider_GlobalTask::FindMarkedTarget(const TargetProvider_GlobalTaskContext& context, const std::vector<Unit*>& ccMarks)
{
    for (Unit* unit : GetMarkedUnits(context, context.rtiValue))
        if (IsValidTarget(context, unit, ccMarks))
            return unit;
    return nullptr;
}

Unit* TargetProvider_GlobalTask::FindAssistTarget(const STAI_TaskContext& context, const std::vector<Unit*>& ccMarks)
{
    // the value caches its list (the expensive part: units around the bot, its group and master)
    const std::list<ObjectGuid> attackers = context.ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("possible attack targets")->Get();

    Unit* best = nullptr;
    for (const ObjectGuid& guid : attackers)
    {
        Unit* unit = context.ai->GetUnit(guid);
        if (!unit || unit->IsPlayer() || !IsValidTarget(context, unit, ccMarks))
            continue;
        if (!best || unit->GetHealth() < best->GetHealth())
            best = unit;
    }
    return best;
}

bool TargetProvider_GlobalTask::IsThreatHigh(const STAI_TaskContext& context, Unit* unit, float threatShare)
{
    // players have no threat list
    Player* bot = context.bot;
    Group* group = bot->GetGroup();
    if (!unit->IsCreature() || !group)
        return false;

    // somebody to hold aggro instead of the bot
    bool hasTank = false;
    for (GroupReference* ref = group->GetFirstMember(); ref && !hasTank; ref = ref->next())
    {
        Player* member = ref->getSource();
        hasTank = member && member != bot && member->IsInMap(bot) && sServerFacade.IsAlive(member) && context.ai->IsTank(member);
    }
    if (!hasTank)
        return false;

    float botThreat = 0.0f;
    float highestThreat = 0.0f;
    for (HostileReference* ref : sServerFacade.GetThreatManager(unit).getThreatList())
    {
        if (ref->getUnitGuid() == bot->GetObjectGuid())
            botThreat = ref->getThreat();
        else
            highestThreat = std::max(highestThreat, ref->getThreat());
    }
    // nobody else has threat yet (also before the pull): the bot waits for the tank
    return botThreat >= threatShare * highestThreat;
}

void TargetProvider_GlobalTask::SetChosen(TargetProvider_GlobalTaskContext& context, Unit* unit, bool marked)
{
    const ObjectGuid guid = unit ? unit->GetObjectGuid() : ObjectGuid();
    context.marked = unit && marked;
    if (guid == context.chosen)
        return;

    context.chosen = guid;
    // the legacy values (reaction engine, chat commands) see the same target
    context.ai->GetAiObjectContext()->GetValue<Unit*>("current target")->Set(unit);
}

void TargetProvider_GlobalTask::Publish(TargetProvider_GlobalTaskContext& context, bool checkThreat) const
{
    Unit* chosen = context.chosen.IsEmpty() ? nullptr : context.bot->GetMap()->GetUnit(context.chosen);
    if (!withThreat || !chosen)
        context.threatHigh = false;
    else if (checkThreat)
        context.threatHigh = IsThreatHigh(context, chosen, threatShare);

    const ObjectGuid target = context.threatHigh ? ObjectGuid() : context.chosen;
    if (target == context.target)
        return;

    context.target = target;
    context.onTargetChanged.Broadcast();
}

void TargetProvider_GlobalTask::Update(TargetProvider_GlobalTaskContext& context, bool choose) const
{
    const ObjectGuid previous = context.chosen;
    Choose(context, choose);
    // the threat is checked with each choice, and right away for a new unit
    Publish(context, choose || context.chosen != previous);
}

void TargetProvider_GlobalTask::Choose(TargetProvider_GlobalTaskContext& context, bool choose)
{
    Player* bot = context.bot;
    if (!bot->IsInWorld() || !sServerFacade.IsAlive(bot))
    {
        SetChosen(context, nullptr, false);
        return;
    }

    // the bot's crowd-control marks, once per update (no group: none)
    const std::vector<Unit*> ccMarks = GetMarkedUnits(context, context.rtiCcValue);

    Unit* current = context.chosen.IsEmpty() ? nullptr : bot->GetMap()->GetUnit(context.chosen);
    const bool currentValid = current && IsValidTarget(context, current, ccMarks);

    // the current target stays until the next choice, unless it died / got crowd-controlled
    if (!choose && (currentValid || !current))
    {
        if (!current)
            choose = true;
        else
            return;
    }

    // a living mark always wins
    if (Unit* marked = FindMarkedTarget(context, ccMarks))
    {
        SetChosen(context, marked, true);
        return;
    }

    // without marks: keep a valid fallback target, else the dps assist choice
    if (currentValid && !context.marked)
        return;
    SetChosen(context, FindAssistTarget(context, ccMarks), false);
}

void TargetProvider_GlobalTask::OnEnter(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(TargetProvider_GlobalTask::OnEnter);
    TargetProvider_GlobalTaskContext& context = ContextAs<TargetProvider_GlobalTaskContext>(baseContext);
    context.target.Clear();
    context.chosen.Clear();
    context.threatHigh = false;
    context.marked = false;
    context.sinceCheckMs = 0;
    AiObjectContext* aiObjectContext = context.ai->GetAiObjectContext();
    context.rtiValue = aiObjectContext->GetValue<std::vector<std::string>>("rti");
    context.rtiCcValue = aiObjectContext->GetValue<std::vector<std::string>>("rti cc");
    Update(context, true);
}

void TargetProvider_GlobalTask::OnExit(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(TargetProvider_GlobalTask::OnExit);
    TargetProvider_GlobalTaskContext& context = ContextAs<TargetProvider_GlobalTaskContext>(baseContext);
    context.target.Clear();
    context.chosen.Clear();
    context.threatHigh = false;
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
    static const std::vector<STAI_PropertyDef> properties = {
        STAI_PropertyDef::Of("target", &TargetProvider_GlobalTaskContext::target),
        STAI_PropertyDef::Of("threatHigh", &TargetProvider_GlobalTaskContext::threatHigh),
    };
    return properties;
}

const std::vector<STAI_DelegateDef>& TargetProvider_GlobalTask::GetDelegates() const
{
    static const std::vector<STAI_DelegateDef> delegates = { STAI_DelegateDef::Of("onTargetChanged", &TargetProvider_GlobalTaskContext::onTargetChanged) };
    return delegates;
}

