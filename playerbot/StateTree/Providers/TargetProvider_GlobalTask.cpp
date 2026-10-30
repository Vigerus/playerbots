#include "TargetProvider_GlobalTask.h"

#include "playerbot/StateTree/STAI_SameMap.h"

#include "playerbot/PlayerbotAI.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/StateTree/STAI_TaskFactory.h"

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
    std::vector<Unit*> GetMarkedUnits(const STAI_TaskContext& context, const std::vector<std::string>& icons)
    {
        std::vector<Unit*> units;
        Player* bot = context.bot;
        Group* group = bot->GetGroup();
        if (!group)
            return units;

        for (const std::string& icon : icons)
        {
            const int index = STAI_GetRtiIndex(icon);
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

    // <AssistTank value="true|false"/>, default false
    bool ParseAssistTank(const STAI_TaskConfig& config)
    {
        const std::string value = STAI_TaskFactory::GetSetting(config, "AssistTank").value_or("false");
        if (value == "true" || value == "1")
            return true;
        if (value == "false" || value == "0")
            return false;
        throw std::runtime_error("<AssistTank value=\"...\">: expected true or false, got '" + value + "'");
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

    // <TankRange value="10"/>, yards; 0 (the default) switches the tank check off
    float ParseTankRange(const STAI_TaskConfig& config)
    {
        const std::string value = STAI_TaskFactory::GetSetting(config, "TankRange").value_or("0");
        char* end = nullptr;
        const double range = std::strtod(value.c_str(), &end);
        if (value.empty() || *end || range < 0.0)
            throw std::runtime_error("<TankRange value=\"...\">: expected yards, got '" + value + "'");
        return float(range);
    }
}

void TargetProvider_GlobalTask::Validate(const STAI_TaskConfig& config)
{
    ParseWithThreat(config);
    ParseThreatShare(config);
    ParseAssistTank(config);
    ParseTankRange(config);
    STAI_TargetAuraWatch::Parse(config);
}

std::unique_ptr<STAI_TaskBase> TargetProvider_GlobalTask::Create(const STAI_TaskConfig& config)
{
    return std::make_unique<TargetProvider_GlobalTask>(ParseWithThreat(config), ParseThreatShare(config), ParseAssistTank(config),
        ParseTankRange(config), STAI_TargetAuraWatch::Parse(config));
}

bool TargetProvider_GlobalTask::IsValidTarget(const STAI_TaskContext& context, Unit* unit, const std::vector<Unit*>& ccMarks)
{
    Player* bot = context.bot;
    if (!unit || !sServerFacade.IsAlive(unit) || !IsOnSameMap(unit, bot))
        return false;
    if (!sServerFacade.IsHostileTo(bot, unit) || !bot->IsWithinDistInMap(unit, sPlayerbotAIConfig.sightDistance))
        return false;
    if (STAI_HasBreakableCC(unit, bot) || STAI_HasUnBreakableCC(unit, bot))
        return false;

    // the bot's own crowd-control mark is left for CC
    return std::find(ccMarks.begin(), ccMarks.end(), unit) == ccMarks.end();
}

Unit* TargetProvider_GlobalTask::FindMarkedTarget(const TargetProvider_GlobalTaskContext& context, const std::vector<Unit*>& ccMarks)
{
    for (Unit* unit : GetMarkedUnits(context, context.ai->GetTargetIcons().GetRti(context.bot)))
        if (IsValidTarget(context, unit, ccMarks))
            return unit;
    return nullptr;
}

Unit* TargetProvider_GlobalTask::FindAssistTarget(const STAI_TaskContext& context, const std::vector<Unit*>& ccMarks, bool assistTank)
{
    // the unit a living tank of the group attacks, when it fights the group
    Player* bot = context.bot;
    Group* group = bot->GetGroup();
    if (assistTank && group)
    {
        const auto& attackers = context.ai->GetTeamAttackers();
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->getSource();
            if (!member || member == bot || !member->IsInMap(bot) || !sServerFacade.IsAlive(member) || !context.ai->IsTank(member))
                continue;
            Unit* victim = member->GetVictim();
            if (victim && attackers.find(victim->GetObjectGuid()) != attackers.end() && IsValidTarget(context, victim, ccMarks))
                return victim;
        }
    }

    // the units on the threat lists of the bot and its group (TODO(strategy): was the legacy "possible attack
    // targets" value: units around the bot, its group and master)
    Unit* best = nullptr;
    for (const auto& [guid, victims] : context.ai->GetTeamAttackers())
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

bool TargetProvider_GlobalTask::IsTankNear(const STAI_TaskContext& context, Unit* unit, float range)
{
    Player* bot = context.bot;
    Group* group = bot->GetGroup();
    if (!group)
        return true;

    bool hasTank = false;
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->getSource();
        if (!member || member == bot || !member->IsInMap(bot) || !sServerFacade.IsAlive(member) || !context.ai->IsTank(member))
            continue;
        if (member->IsWithinDistInMap(unit, range))
            return true;
        hasTank = true;
    }
    // nobody to wait for
    return !hasTank;
}

void TargetProvider_GlobalTask::UpdateTankNear(TargetProvider_GlobalTaskContext& context, bool check) const
{
    const bool wasNear = context.tankNear;
    if (tankRange <= 0.0f)
        context.tankNear = true;
    else if (context.chosen.IsEmpty())
    {
        context.tankNear = false;
        context.tankNearUnit.Clear();
    }
    // once the tank has come near, the bot stays on the unit (also when the tank moves on with it)
    else if (context.tankNearUnit == context.chosen)
        context.tankNear = true;
    else if (check)
    {
        Unit* chosen = context.bot->GetMap()->GetUnit(context.chosen);
        context.tankNear = chosen && IsTankNear(context, chosen, tankRange);
        if (context.tankNear)
            context.tankNearUnit = context.chosen;
    }
    else
        context.tankNear = false;

    if (context.tankNear && !wasNear)
        context.onTankNear.Broadcast();
}

void TargetProvider_GlobalTask::UpdateAuras(TargetProvider_GlobalTaskContext& context) const
{
    Unit* target = context.target.IsEmpty() ? nullptr : context.bot->GetMap()->GetUnit(context.target);
    if (context.auraWatch.Update(target, auras, context.auraStacks))
        context.onAuraChanged.Broadcast();
}

void TargetProvider_GlobalTask::SetChosen(TargetProvider_GlobalTaskContext& context, Unit* unit, bool marked)
{
    const ObjectGuid guid = unit ? unit->GetObjectGuid() : ObjectGuid();
    context.marked = unit && marked;
    if (guid == context.chosen)
        return;

    context.chosen = guid;
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
    // the threat (and the tank's distance) is checked with each choice, and right away for a new unit
    const bool check = choose || context.chosen != previous;
    UpdateTankNear(context, check);
    Publish(context, check);
    UpdateAuras(context);
}

void TargetProvider_GlobalTask::Choose(TargetProvider_GlobalTaskContext& context, bool choose) const
{
    Player* bot = context.bot;
    if (!bot->IsInWorld() || !sServerFacade.IsAlive(bot))
    {
        SetChosen(context, nullptr, false);
        return;
    }

    // the bot's crowd-control marks, once per update (no group: none)
    const std::vector<Unit*> ccMarks = GetMarkedUnits(context, context.ai->GetTargetIcons().GetRtiCcList(context.bot));

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
    SetChosen(context, FindAssistTarget(context, ccMarks, assistTank), false);
}

EActionRunStatus TargetProvider_GlobalTask::OnEnterState(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(TargetProvider_GlobalTask::OnEnterState);
    TargetProvider_GlobalTaskContext& context = ContextAs<TargetProvider_GlobalTaskContext>(baseContext);
    context.target.Clear();
    context.chosen.Clear();
    context.threatHigh = false;
    context.marked = false;
    context.tankNear = tankRange <= 0.0f;
    context.tankNearUnit.Clear();
    context.auraStacks.clear();
    context.sinceCheckMs = 0;
    context.iconsVersion = context.ai->GetTargetIcons().GetVersion();
    Update(context, true);
    return EActionRunStatus::Running;
}

void TargetProvider_GlobalTask::OnExitState(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(TargetProvider_GlobalTask::OnExitState);
    TargetProvider_GlobalTaskContext& context = ContextAs<TargetProvider_GlobalTaskContext>(baseContext);
    context.target.Clear();
    context.chosen.Clear();
    context.threatHigh = false;
    context.marked = false;
    context.tankNearUnit.Clear();
    context.auraWatch.Unsubscribe();
    context.auraStacks.clear();
}

EActionRunStatus TargetProvider_GlobalTask::OnTick(STAI_TaskContext& baseContext, uint32 elapsed, bool minimal) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(TargetProvider_GlobalTask::OnTick);
    TargetProvider_GlobalTaskContext& context = ContextAs<TargetProvider_GlobalTaskContext>(baseContext);
    context.sinceCheckMs += elapsed;
    // the bot's rti icons changed (the rti command, the inspect addon): choose again now
    const uint32 iconsVersion = context.ai->GetTargetIcons().GetVersion();
    const bool choose = context.sinceCheckMs >= chooseIntervalMs || iconsVersion != context.iconsVersion;
    context.iconsVersion = iconsVersion;
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
        STAI_PropertyDef::Of("auraStacks", &TargetProvider_GlobalTaskContext::auraStacks),
        STAI_PropertyDef::Of("tankNear", &TargetProvider_GlobalTaskContext::tankNear),
    };
    return properties;
}

const std::vector<STAI_DelegateDef>& TargetProvider_GlobalTask::GetDelegates() const
{
    static const std::vector<STAI_DelegateDef> delegates = {
        STAI_DelegateDef::Of("onTargetChanged", &TargetProvider_GlobalTaskContext::onTargetChanged),
        STAI_DelegateDef::Of("onAuraChanged", &TargetProvider_GlobalTaskContext::onAuraChanged),
        STAI_DelegateDef::Of("onTankNear", &TargetProvider_GlobalTaskContext::onTankNear),
    };
    return delegates;
}

