#include "Task_MeleeAttack.h"

#include "playerbot/PlayerbotAI.h"
#include "playerbot/ServerFacade.h"

#include "Entities/Player.h"
#include "Maps/Map.h"
#include "MotionGenerators/MotionMaster.h"

#include <stdexcept>

using namespace ai;

namespace
{
    // the bot gives up reaching the target after this long
    constexpr uint32 timeoutMs = 15000;
    // how far behind the target's back the bot stands (inside melee reach)
    constexpr float behindDistance = 1.0f;

    Unit* GetTarget(const Task_MeleeAttackContext& context)
    {
        Player* bot = context.bot;
        if (context.target.IsEmpty() || !bot->IsInWorld())
            return nullptr;
        Unit* target = bot->GetMap()->GetUnit(context.target);
        return target && target->IsInWorld() && sServerFacade.IsAlive(target) && sServerFacade.IsHostileTo(bot, target) ? target : nullptr;
    }
}

void Task_MeleeAttack::Validate(const STAI_TaskConfig& config)
{
    Create(config);
}

std::unique_ptr<STAI_Task> Task_MeleeAttack::Create(const STAI_TaskConfig& config)
{
    const std::string position = STAI_TaskFactory::GetSetting(config, "Position").value_or("front");
    if (position != "front" && position != "behind")
        throw std::runtime_error("<Position value=\"" + position + "\">: expected front or behind");

    const std::string duration = STAI_TaskFactory::GetSetting(config, "Duration").value_or("0");
    char* end = nullptr;
    const unsigned long ms = std::strtoul(duration.c_str(), &end, 10);
    if (duration.empty() || *end)
        throw std::runtime_error("<Duration value=\"" + duration + "\">: needs milliseconds");

    return std::make_unique<Task_MeleeAttack>(position == "behind", uint32(ms), STAI_TaskFactory::GetBindingSource(config, "target"));
}

float Task_MeleeAttack::ChaseAngle(bool behind)
{
    // ChaseMovementGenerator: 0 keeps the side the bot is on (and always while the target attacks the bot), else the
    // angle from the target's facing
    return behind ? M_PI_F : 0.0f;
}

EActionRunStatus Task_MeleeAttack::Update(Task_MeleeAttackContext& context) const
{
    Player* bot = context.bot;
    Unit* target = GetTarget(context);
    if (!target || !sServerFacade.IsAlive(bot))
        return EActionRunStatus::Succeeded;

    MotionMaster& motion = *bot->GetMotionMaster();
    const bool inReach = bot->CanReachWithMeleeAttack(target);

    // not after a target in a dungeon stage that is not cleared, except into the boxes that may be entered in combat
    // while the group fights the target (the bot itself is not in combat yet when it waited for the tank's threat)
    const auto& attackers = context.ai->GetTeamAttackers();
    const bool fighting = bot->IsInCombat() || attackers.find(target->GetObjectGuid()) != attackers.end();
    if (!inReach && context.ai->IsStageEntryBlocked(target->GetPositionX(), target->GetPositionY(), target->GetPositionZ(), fighting))
    {
        motion.Clear();
        bot->StopMoving();
        return EActionRunStatus::Failed;
    }

    // the auto attack, as the client's attack swing
    if (bot->IsSitState())
        bot->SetStandState(UNIT_STAND_STATE_STAND);
    if (bot->GetVictim() != target)
    {
        bot->SetSelectionGuid(target->GetObjectGuid());
        bot->Attack(target, true);
    }

    const bool wantBehind = behind && target->GetVictim() != bot;
    const bool inFront = target->HasInArc(bot);

    // walking around the target: wait until the point behind it is reached
    if (context.goingBehind)
    {
        if (motion.GetCurrentMovementGeneratorType() == POINT_MOTION_TYPE && !bot->IsStopped())
            return EActionRunStatus::Running;
        context.goingBehind = false;
        context.attacking.Clear();     // chase again from there
    }

    // in reach but in front of a target that does not attack the bot: to its back
    if (wantBehind && inReach && inFront)
    {
        float x, y, z;
        target->GetNearPoint(bot, x, y, z, bot->GetObjectBoundingRadius(), behindDistance, target->GetOrientation() + M_PI_F);
        motion.MovePoint(0, x, y, z);
        context.goingBehind = true;
        return EActionRunStatus::Running;
    }

    // follow the target: the chase keeps the bot in reach (at the target's back for behind)
    if (context.attacking != target->GetObjectGuid() || motion.GetCurrentMovementGeneratorType() != CHASE_MOTION_TYPE)
    {
        motion.MoveChase(target, 0.0f, ChaseAngle(wantBehind));
        context.attacking = target->GetObjectGuid();
    }

    // positioned: in reach, behind when it has to be; standing still, face the target for the swings
    const bool positioned = inReach && (!wantBehind || !inFront);
    if (positioned && bot->IsStopped() && !bot->HasInArc(target))
        sServerFacade.SetFacingTo(bot, target);

    const uint32 since = WorldTimer::getMSTimeDiff(context.enterMs, WorldTimer::getMSTime());
    if (durationMs ? since >= durationMs : positioned)
        return EActionRunStatus::Succeeded;
    if (!positioned && since >= timeoutMs)
        return EActionRunStatus::Failed;
    return EActionRunStatus::Running;
}

EActionRunStatus Task_MeleeAttack::OnEnter(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(Task_MeleeAttack::OnEnter);
    Task_MeleeAttackContext& context = ContextAs<Task_MeleeAttackContext>(baseContext);
    StartLog(context);
    context.enterMs = WorldTimer::getMSTime();
    context.goingBehind = false;
    // a chase of the same target stays, another one is replaced
    const MotionMaster& motion = *context.bot->GetMotionMaster();
    if (motion.GetCurrentMovementGeneratorType() != CHASE_MOTION_TYPE || context.bot->GetVictim() == nullptr ||
        context.bot->GetVictim()->GetObjectGuid() != context.target)
        context.attacking.Clear();
    return LogStatus(context, Update(context));
}

EActionRunStatus Task_MeleeAttack::OnTick(STAI_TaskContext& baseContext, uint32 elapsed, bool minimal) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(Task_MeleeAttack::OnTick);
    Task_MeleeAttackContext& context = ContextAs<Task_MeleeAttackContext>(baseContext);
    return LogStatus(context, Update(context));
}

void Task_MeleeAttack::OnExit(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(Task_MeleeAttack::OnExit);
    // the chase and the auto attack go on: the ability nodes run in reach of the target
    EndLog(baseContext, false);
}

const std::vector<STAI_PropertyDef>& Task_MeleeAttack::GetProperties() const
{
    static const std::vector<STAI_PropertyDef> properties = { STAI_PropertyDef::Of("target", &Task_MeleeAttackContext::target) };
    return properties;
}

std::string Task_MeleeAttack::GetDebugInfo() const
{
    return std::string("Melee ") + (behind ? "behind " : "") + targetSource + (durationMs ? " for " + std::to_string(durationMs) + "ms" : "");
}

EActionRunStatus Task_StartAttack::OnEnter(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(Task_StartAttack::OnEnter);
    Task_StartAttackContext& context = ContextAs<Task_StartAttackContext>(baseContext);
    Player* bot = context.bot;
    Unit* target = !context.target.IsEmpty() && bot->IsInWorld() ? bot->GetMap()->GetUnit(context.target) : nullptr;
    if (!target || !target->IsInWorld() || !sServerFacade.IsAlive(target) || !sServerFacade.IsHostileTo(bot, target) || !sServerFacade.IsAlive(bot))
        return EActionRunStatus::Failed;
    if (bot->GetVictim() == target)
        return EActionRunStatus::Succeeded;

    StartLog(context);
    if (bot->IsSitState())
        bot->SetStandState(UNIT_STAND_STATE_STAND);
    bot->SetSelectionGuid(target->GetObjectGuid());
    bot->Attack(target, true);
    return LogStatus(context, bot->GetVictim() == target ? EActionRunStatus::Succeeded : EActionRunStatus::Failed);
}

const std::vector<STAI_PropertyDef>& Task_StartAttack::GetProperties() const
{
    static const std::vector<STAI_PropertyDef> properties = { STAI_PropertyDef::Of("target", &Task_StartAttackContext::target) };
    return properties;
}

EActionRunStatus Task_StopAttack::OnEnter(STAI_TaskContext& context) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(Task_StopAttack::OnEnter);
    Player* bot = context.bot;
    if (!bot->GetVictim() && bot->GetMotionMaster()->GetCurrentMovementGeneratorType() != CHASE_MOTION_TYPE)
        return EActionRunStatus::Succeeded;

    StartLog(context);
    bot->AttackStop();
    if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == CHASE_MOTION_TYPE)
    {
        bot->GetMotionMaster()->Clear();
        bot->StopMoving();
    }
    return LogStatus(context, EActionRunStatus::Succeeded);
}
