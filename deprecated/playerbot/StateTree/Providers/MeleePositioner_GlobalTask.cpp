#include "MeleePositioner_GlobalTask.h"

#include "playerbot/PlayerbotAI.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/StateTree/STAI_TaskFactory.h"

#include "Entities/Player.h"
#include "Maps/Map.h"
#include "MotionGenerators/MotionMaster.h"

#include <stdexcept>

using namespace ai;

namespace
{
    // how often the position is checked
    constexpr uint32 checkIntervalMs = 100;
    // in the victim's front arc this long before the bot walks to its back (units turn with their victim)
    constexpr uint32 frontGraceMs = 1000;
    // how far behind the victim's back the bot stands (inside melee reach)
    constexpr float behindDistance = 1.0f;
}

MeleePositioner_GlobalTask::Position MeleePositioner_GlobalTask::ParsePosition(const STAI_TaskConfig& config)
{
    const std::string value = STAI_TaskFactory::GetSetting(config, "Position").value_or("auto");
    if (value == "auto")
        return Position::Auto;
    if (value == "behind")
        return Position::Behind;
    if (value == "front")
        return Position::Front;
    throw std::runtime_error("<Position value=\"" + value + "\">: expected auto, behind or front");
}

void MeleePositioner_GlobalTask::Validate(const STAI_TaskConfig& config)
{
    ParsePosition(config);
}

std::unique_ptr<STAI_Task> MeleePositioner_GlobalTask::Create(const STAI_TaskConfig& config)
{
    return std::make_unique<MeleePositioner_GlobalTask>(ParsePosition(config));
}

void MeleePositioner_GlobalTask::Update(MeleePositioner_GlobalTaskContext& context) const
{
    Player* bot = context.bot;
    MotionMaster& motion = *bot->GetMotionMaster();
    const MovementGeneratorType motionType = motion.GetCurrentMovementGeneratorType();

    Unit* victim = bot->IsInWorld() && sServerFacade.IsAlive(bot) ? bot->GetVictim() : nullptr;
    if (victim && (!victim->IsInWorld() || !sServerFacade.IsAlive(victim) || !sServerFacade.IsHostileTo(bot, victim)))
        victim = nullptr;

    context.victim = victim ? victim->GetObjectGuid() : ObjectGuid();
    context.inReach = victim && bot->CanReachWithMeleeAttack(victim);
    context.blocked = false;
    if (!victim)
    {
        // the attack stopped: the positioner's own chase goes with it
        if (!context.chased.IsEmpty() && motionType == CHASE_MOTION_TYPE)
        {
            motion.Clear();
            bot->StopMoving();
        }
        context.chased.Clear();
        context.goingBehind = false;
        context.frontSinceMs = 0;
        return;
    }

    // the positioner's walk around the victim: wait until it ends
    if (context.goingBehind)
    {
        if (motionType == POINT_MOTION_TYPE)
            return;
        context.goingBehind = false;
        context.chased.Clear();
    }

    // movement somebody else gave (a hazard escape, a charge, a point move, fear): left alone
    if (motionType != IDLE_MOTION_TYPE && motionType != CHASE_MOTION_TYPE && motionType != FOLLOW_MOTION_TYPE)
        return;
    // no new movement while casting (it would interrupt the cast)
    if (bot->IsNonMeleeSpellCasted(false))
        return;

    // not after a unit in a dungeon stage that is not cleared, except into the boxes that may be entered in combat
    // while the group fights it
    const auto& attackers = context.ai->GetTeamAttackers();
    const bool fighting = bot->IsInCombat() || attackers.find(victim->GetObjectGuid()) != attackers.end();
    if (!context.inReach && context.ai->IsStageEntryBlocked(victim->GetPositionX(), victim->GetPositionY(), victim->GetPositionZ(), fighting))
    {
        context.blocked = true;
        if (motionType == CHASE_MOTION_TYPE)
        {
            motion.Clear();
            bot->StopMoving();
        }
        context.chased.Clear();
        return;
    }

    if (bot->IsSitState())
        bot->SetStandState(UNIT_STAND_STATE_STAND);

    // behind: not for tanks, and not while the victim attacks the bot (it turns to the bot anyway)
    const bool tank = position == Position::Front || (position == Position::Auto && context.ai->IsTank(bot));
    const bool wantBehind = !tank && victim->GetVictim() != bot;

    // in reach in front of the victim for a while: around it to its back
    if (wantBehind && context.inReach && victim->HasInArc(bot))
    {
        if (!context.frontSinceMs)
            context.frontSinceMs = std::max<uint32>(1, WorldTimer::getMSTime());
        if (WorldTimer::getMSTimeDiff(context.frontSinceMs, WorldTimer::getMSTime()) >= frontGraceMs)
        {
            float x, y, z;
            victim->GetNearPoint(bot, x, y, z, bot->GetObjectBoundingRadius(), behindDistance, victim->GetOrientation() + M_PI_F);
            motion.MovePoint(0, x, y, z);
            context.goingBehind = true;
            context.frontSinceMs = 0;
            return;
        }
    }
    else
        context.frontSinceMs = 0;

    // follow the victim: a chase of the positioner, at its back for behind (0: the side the bot is on)
    if (motionType != CHASE_MOTION_TYPE || context.chased != victim->GetObjectGuid() || context.chasedBehind != wantBehind)
    {
        motion.MoveChase(victim, 0.0f, wantBehind ? M_PI_F : 0.0f);
        context.chased = victim->GetObjectGuid();
        context.chasedBehind = wantBehind;
    }

    // standing in reach: face the victim for the swings
    if (context.inReach && bot->IsStopped() && !bot->HasInArc(victim))
        sServerFacade.SetFacingTo(bot, victim);
}

EActionRunStatus MeleePositioner_GlobalTask::OnEnter(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(MeleePositioner_GlobalTask::OnEnter);
    MeleePositioner_GlobalTaskContext& context = ContextAs<MeleePositioner_GlobalTaskContext>(baseContext);
    context.chased.Clear();
    context.goingBehind = false;
    context.frontSinceMs = 0;
    context.sinceCheckMs = 0;
    Update(context);
    return EActionRunStatus::Running;
}

EActionRunStatus MeleePositioner_GlobalTask::OnTick(STAI_TaskContext& baseContext, uint32 elapsed, bool minimal) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(MeleePositioner_GlobalTask::OnTick);
    MeleePositioner_GlobalTaskContext& context = ContextAs<MeleePositioner_GlobalTaskContext>(baseContext);
    context.sinceCheckMs += elapsed;
    if (context.sinceCheckMs >= checkIntervalMs)
    {
        context.sinceCheckMs = 0;
        Update(context);
    }
    return EActionRunStatus::Running;
}

const std::vector<STAI_PropertyDef>& MeleePositioner_GlobalTask::GetProperties() const
{
    static const std::vector<STAI_PropertyDef> properties =
    {
        STAI_PropertyDef::Of("victim", &MeleePositioner_GlobalTaskContext::victim),
        STAI_PropertyDef::Of("inReach", &MeleePositioner_GlobalTaskContext::inReach),
        STAI_PropertyDef::Of("blocked", &MeleePositioner_GlobalTaskContext::blocked),
    };
    return properties;
}
