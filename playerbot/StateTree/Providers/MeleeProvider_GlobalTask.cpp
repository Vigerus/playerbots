#include "MeleeProvider_GlobalTask.h"

#include "playerbot/StateTree/STAI_SameMap.h"

#include "playerbot/PlayerbotAI.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/StateTree/STAI_TaskFactory.h"

#include "Entities/Creature.h"
#include "Grids/CellImpl.h"
#include "Grids/GridNotifiers.h"
#include "Grids/GridNotifiersImpl.h"
#include "Maps/Map.h"
#include "Spells/Spell.h"

#include <cstdlib>
#include <stdexcept>

using namespace ai;

namespace
{
    // how often the target is checked
    constexpr uint32 checkIntervalMs = 100;
    // the range of the melee area abilities (Cleave, Whirlwind, Thunder Clap, Demoralizing Shout)
    constexpr float aoeRange = 8.0f;
    // in the target's front arc this long before the bot is not behind any more (targets turn with their victim)
    constexpr uint32 frontGraceMs = 1000;

    float ParseFloat(const std::string& text, const std::string& what)
    {
        char* end = nullptr;
        const float value = std::strtof(text.c_str(), &end);
        if (text.empty() || *end || value < 0.0f)
            throw std::runtime_error(what + ": needs a number (0 or more), got '" + text + "'");
        return value;
    }

    // a creature that joins a fight near it: alive, hostile to the bot, not fighting yet, no critter, totem or pet
    bool IsIdleEnemy(Player* bot, Unit* unit)
    {
        if (!unit->IsCreature() || !unit->IsAlive() || unit->IsInCombat())
            return false;
        Creature* creature = static_cast<Creature*>(unit);
        if (creature->IsCritter() || creature->IsTotem() || creature->IsPet())
            return false;
        return sServerFacade.IsHostileTo(bot, unit);
    }
}

MeleeProvider_GlobalTask::Settings MeleeProvider_GlobalTask::ParseSettings(const STAI_TaskConfig& config)
{
    Settings settings;
    for (const auto& [key, child] : config)
    {
        if (key == "ChargeRange")
        {
            settings.chargeMin = ParseFloat(child.get<std::string>("<xmlattr>.min", "8"), "<ChargeRange min>");
            settings.chargeMax = ParseFloat(child.get<std::string>("<xmlattr>.max", "25"), "<ChargeRange max>");
            if (settings.chargeMax < settings.chargeMin)
                throw std::runtime_error("<ChargeRange>: max is below min");
        }
        else if (key == "PullRadius")
            settings.pullRadius = ParseFloat(child.get<std::string>("<xmlattr>.value", ""), "<PullRadius value>");
        else if (key == "FightTime")
            settings.fightTimeMs = uint32(ParseFloat(child.get<std::string>("<xmlattr>.value", ""), "<FightTime value>"));
    }
    return settings;
}

void MeleeProvider_GlobalTask::Validate(const STAI_TaskConfig& config)
{
    ParseSettings(config);
}

std::unique_ptr<STAI_TaskBase> MeleeProvider_GlobalTask::Create(const STAI_TaskConfig& config)
{
    return std::make_unique<MeleeProvider_GlobalTask>(ParseSettings(config));
}

uint32 MeleeProvider_GlobalTask::CountEnemiesNear(PlayerbotAI* ai, Player* bot, float range)
{
    uint32 count = 0;
    for (const auto& [guid, victims] : ai->GetTeamAttackers())
    {
        Unit* unit = ai->GetUnit(guid);
        if (unit && unit->IsAlive() && IsOnSameMap(unit, bot) && bot->IsWithinDistInMap(unit, range))
            ++count;
    }
    return count;
}

bool MeleeProvider_GlobalTask::IsIdleEnemyNear(Player* bot, float x, float y, float z, float radius, Unit* except)
{
    bool found = false;
    std::list<Unit*> units;
    MaNGOS::AnyUnitInObjectRangeCheck check(bot, bot->GetDistance(x, y, z) + radius);
    MaNGOS::UnitListSearcher<MaNGOS::AnyUnitInObjectRangeCheck> searcher(units, check);
    Cell::VisitAllObjects(bot, searcher, bot->GetDistance(x, y, z) + radius);
    for (Unit* unit : units)
    {
        if (unit == except || !IsIdleEnemy(bot, unit))
            continue;
        if (unit->GetDistance(x, y, z) <= radius)
        {
            found = true;
            break;
        }
    }
    return found;
}

void MeleeProvider_GlobalTask::Update(MeleeProvider_GlobalTaskContext& context) const
{
    Player* bot = context.bot;
    const bool wasInMelee = context.inMelee;
    const bool couldCharge = context.canCharge;
    context.inMelee = false;
    context.behind = false;
    context.targetAttacksBot = false;
    context.targetHealthPct = 100.0f;
    context.targetDistance = 0.0f;
    context.canCharge = false;
    context.attacking = false;
    context.swingQueued = bot->GetCurrentSpell(CURRENT_MELEE_SPELL) != nullptr;
    context.inCombat = bot->IsInCombat();
    context.enemiesInMelee = bot->IsInWorld() ? CountEnemiesNear(context.ai, bot, aoeRange) : 0;

    Unit* target = bot->IsInWorld() && !context.target.IsEmpty() ? bot->GetMap()->GetUnit(context.target) : nullptr;
    if (target && (!target->IsInWorld() || !target->IsAlive()))
        target = nullptr;

    // how long the target has been fighting the group
    if (!target || context.fightTarget != target->GetObjectGuid())
    {
        context.fightTarget = target ? target->GetObjectGuid() : ObjectGuid();
        context.fightStartMs = 0;
        context.frontSinceMs = 0;
    }
    const auto& attackers = context.ai->GetTeamAttackers();
    const bool fightsGroup = target && attackers.find(target->GetObjectGuid()) != attackers.end();
    if (fightsGroup && !context.fightStartMs)
        context.fightStartMs = std::max<uint32>(1, WorldTimer::getMSTime());

    if (target && sServerFacade.IsAlive(bot))
    {
        context.inMelee = bot->CanReachWithMeleeAttack(target);
        context.attacking = bot->GetVictim() == target;
        if (!target->HasInArc(bot))
            context.frontSinceMs = 0;
        else if (!context.frontSinceMs)
            context.frontSinceMs = std::max<uint32>(1, WorldTimer::getMSTime());
        context.behind = !context.frontSinceMs || WorldTimer::getMSTimeDiff(context.frontSinceMs, WorldTimer::getMSTime()) < frontGraceMs;
        context.targetAttacksBot = target->GetVictim() == bot;
        context.targetHealthPct = target->GetHealthPercent();
        context.targetDistance = bot->GetDistance(target);

        // a charge that pulls nothing else: the target fights the group for a while, in range and sight, nobody idle
        // near it or the bot, not into a room that is not cleared
        if (fightsGroup && !context.targetAttacksBot && context.fightStartMs &&
            WorldTimer::getMSTimeDiff(context.fightStartMs, WorldTimer::getMSTime()) >= settings.fightTimeMs &&
            context.targetDistance >= settings.chargeMin && context.targetDistance <= settings.chargeMax &&
            bot->IsWithinLOSInMap(target) &&
            !context.ai->IsStageEntryBlocked(target->GetPositionX(), target->GetPositionY(), target->GetPositionZ(), false) &&
            !IsIdleEnemyNear(bot, target->GetPositionX(), target->GetPositionY(), target->GetPositionZ(), settings.pullRadius, target) &&
            !IsIdleEnemyNear(bot, bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), settings.pullRadius, target))
            context.canCharge = true;
    }

    if (context.inMelee != wasInMelee)
        context.onMeleeChanged.Broadcast();
    if (context.canCharge && !couldCharge)
        context.onCanCharge.Broadcast();
}

EActionRunStatus MeleeProvider_GlobalTask::OnEnterState(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(MeleeProvider_GlobalTask::OnEnterState);
    MeleeProvider_GlobalTaskContext& context = ContextAs<MeleeProvider_GlobalTaskContext>(baseContext);
    context.inMelee = false;
    context.canCharge = false;
    context.fightTarget.Clear();
    context.fightStartMs = 0;
    context.sinceCheckMs = 0;
    Update(context);
    return EActionRunStatus::Running;
}

EActionRunStatus MeleeProvider_GlobalTask::OnTick(STAI_TaskContext& baseContext, uint32 elapsed, bool minimal) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(MeleeProvider_GlobalTask::OnTick);
    MeleeProvider_GlobalTaskContext& context = ContextAs<MeleeProvider_GlobalTaskContext>(baseContext);
    context.sinceCheckMs += elapsed;
    if (context.sinceCheckMs >= checkIntervalMs || context.target != context.fightTarget)
    {
        context.sinceCheckMs = 0;
        Update(context);
    }
    return EActionRunStatus::Running;
}

const std::vector<STAI_PropertyDef>& MeleeProvider_GlobalTask::GetProperties() const
{
    static const std::vector<STAI_PropertyDef> properties =
    {
        STAI_PropertyDef::Of("target", &MeleeProvider_GlobalTaskContext::target),
        STAI_PropertyDef::Of("inMelee", &MeleeProvider_GlobalTaskContext::inMelee),
        STAI_PropertyDef::Of("behind", &MeleeProvider_GlobalTaskContext::behind),
        STAI_PropertyDef::Of("targetAttacksBot", &MeleeProvider_GlobalTaskContext::targetAttacksBot),
        STAI_PropertyDef::Of("targetHealthPct", &MeleeProvider_GlobalTaskContext::targetHealthPct),
        STAI_PropertyDef::Of("targetDistance", &MeleeProvider_GlobalTaskContext::targetDistance),
        STAI_PropertyDef::Of("enemiesInMelee", &MeleeProvider_GlobalTaskContext::enemiesInMelee),
        STAI_PropertyDef::Of("inCombat", &MeleeProvider_GlobalTaskContext::inCombat),
        STAI_PropertyDef::Of("canCharge", &MeleeProvider_GlobalTaskContext::canCharge),
        STAI_PropertyDef::Of("attacking", &MeleeProvider_GlobalTaskContext::attacking),
        STAI_PropertyDef::Of("swingQueued", &MeleeProvider_GlobalTaskContext::swingQueued),
    };
    return properties;
}

const std::vector<STAI_DelegateDef>& MeleeProvider_GlobalTask::GetDelegates() const
{
    static const std::vector<STAI_DelegateDef> delegates =
    {
        STAI_DelegateDef::Of("onMeleeChanged", &MeleeProvider_GlobalTaskContext::onMeleeChanged),
        STAI_DelegateDef::Of("onCanCharge", &MeleeProvider_GlobalTaskContext::onCanCharge),
    };
    return delegates;
}
