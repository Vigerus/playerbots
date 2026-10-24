#include "Task_GetInRange.h"

#include "playerbot/PlayerbotAI.h"
#include "playerbot/ServerFacade.h"

#include "Entities/Corpse.h"
#include "Maps/Map.h"
#include "MotionGenerators/MotionMaster.h"

#include <regex>
#include <sstream>
#include <stdexcept>

using namespace ai;

namespace
{
    // the task fails when the bot did not get into range in this time
    constexpr uint32 timeoutMs = 10000;
    // the bot chases to this share of the range, so it is safely inside once it stops
    constexpr float chaseFactor = 0.85f;
    // in range without line of sight the bot closes in to this distance
    constexpr float closeDistance = 3.0f;

    const std::regex numberPattern(R"(^\d+(?:\.\d+)?$)");
    const std::regex rangePattern(R"(^[a-z][a-z ]*$)");

    // yards or a range name for ai->GetRange(); default: the bot's spell range
    std::string ParseRange(const STAI_TaskConfig& config)
    {
        const std::string value = STAI_TaskFactory::GetSetting(config, "Range").value_or("spell");
        if (std::regex_match(value, numberPattern))
        {
            if (std::stof(value) <= 0.0f)
                throw std::runtime_error("Range must be greater than 0");
            return value;
        }

        if (!std::regex_match(value, rangePattern))
            throw std::runtime_error("Range: expected yards or a range name such as \"spell\", got '" + value + "'");
        return value;
    }

    // <Target value="dead"/>: the target is dead
    bool ParseDead(const STAI_TaskConfig& config)
    {
        const std::optional<std::string> target = STAI_TaskFactory::GetSetting(config, "Target");
        if (!target)
            return false;
        if (*target != "dead")
            throw std::runtime_error("<Target value=\"" + *target + "\">: only \"dead\" is known");
        return true;
    }

    WorldObject* GetTarget(const Task_GetInRangeContext& context, bool dead)
    {
        Player* bot = context.bot;
        if (context.target.IsEmpty() || !bot->IsInWorld())
            return nullptr;

        // the corpse of a released member
        if (context.target.IsCorpse())
        {
            Corpse* corpse = dead ? bot->GetMap()->GetCorpse(context.target) : nullptr;
            return corpse && corpse->IsInWorld() ? corpse : nullptr;
        }

        Unit* target = bot->GetMap()->GetUnit(context.target);
        return target && target->IsInWorld() && sServerFacade.IsAlive(target) != dead ? target : nullptr;
    }
}

void Task_GetInRange::Validate(const STAI_TaskConfig& config)
{
    ParseRange(config);
    ParseDead(config);
}

std::unique_ptr<STAI_Task> Task_GetInRange::Create(const STAI_TaskConfig& config)
{
    return std::make_unique<Task_GetInRange>(ParseRange(config), STAI_TaskFactory::GetBindingSource(config, "target"), ParseDead(config));
}

float Task_GetInRange::GetRange(const STAI_TaskContext& context) const
{
    if (std::regex_match(range, numberPattern))
        return std::stof(range);
    return context.ai->GetRange(range);
}

void Task_GetInRange::Stop(Task_GetInRangeContext& context)
{
    if (!context.chasing)
        return;

    context.chasing = false;
    context.bot->GetMotionMaster()->Clear();
    context.bot->StopMoving();
}

EActionRunStatus Task_GetInRange::OnEnter(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(Task_GetInRange::OnEnter);
    StartLog(baseContext);
    Task_GetInRangeContext& context = ContextAs<Task_GetInRangeContext>(baseContext);
    context.enterMs = WorldTimer::getMSTime();
    context.chasing = false;
    return EActionRunStatus::Running;
}

EActionRunStatus Task_GetInRange::OnTick(STAI_TaskContext& baseContext, uint32 elapsed, bool minimal) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(Task_GetInRange::OnTick);
    Task_GetInRangeContext& context = ContextAs<Task_GetInRangeContext>(baseContext);
    Player* bot = context.bot;

    // nothing to get to: the node finishes, the tree selects again
    WorldObject* target = GetTarget(context, dead);
    if (!target)
    {
        Stop(context);
        return LogStatus(baseContext, EActionRunStatus::Succeeded);
    }

    const float yards = GetRange(context);
    if (sServerFacade.GetDistance2d(bot, target) <= yards && bot->IsWithinLOSInMap(target))
    {
        Stop(context);
        return LogStatus(baseContext, EActionRunStatus::Succeeded);
    }

    if (WorldTimer::getMSTimeDiff(context.enterMs, WorldTimer::getMSTime()) >= timeoutMs)
    {
        Stop(context);
        return LogStatus(baseContext, EActionRunStatus::Failed);
    }

    // not after a target in a dungeon stage that is not cleared (the master pulls it out)
    if (context.ai->IsStageEntryBlocked(target->GetPositionX(), target->GetPositionY(), target->GetPositionZ()))
    {
        Stop(context);
        return LogStatus(baseContext, EActionRunStatus::Failed);
    }

    // chase into range; in range without sight the bot closes in until it sees the target
    const bool inRange = sServerFacade.GetDistance2d(bot, target) <= yards;
    const float chaseDistance = inRange ? closeDistance : yards * chaseFactor;
    // a corpse does not move: walk to it until in range and sight
    if (!target->isType(TYPEMASK_UNIT))
    {
        if (!context.chasing || bot->GetMotionMaster()->GetCurrentMovementGeneratorType() != POINT_MOTION_TYPE)
        {
            if (bot->IsSitState())
                bot->SetStandState(UNIT_STAND_STATE_STAND);
            bot->GetMotionMaster()->MovePoint(0, target->GetPositionX(), target->GetPositionY(), target->GetPositionZ());
            context.chasing = true;
            context.chaseDistance = 0.0f;
        }
        return LogStatus(baseContext, EActionRunStatus::Running);
    }
    if (!context.chasing || context.chaseDistance != chaseDistance || bot->GetMotionMaster()->GetCurrentMovementGeneratorType() != CHASE_MOTION_TYPE)
    {
        if (bot->IsSitState())
            bot->SetStandState(UNIT_STAND_STATE_STAND);
        bot->GetMotionMaster()->MoveChase(static_cast<Unit*>(target), chaseDistance);
        context.chasing = true;
        context.chaseDistance = chaseDistance;
    }
    return LogStatus(baseContext, EActionRunStatus::Running);
}

void Task_GetInRange::OnExit(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(Task_GetInRange::OnExit);
    // left while running: the run did not get done
    EndLog(baseContext, false);
    Stop(ContextAs<Task_GetInRangeContext>(baseContext));
}

const std::vector<STAI_PropertyDef>& Task_GetInRange::GetProperties() const
{
    static const std::vector<STAI_PropertyDef> properties = { STAI_PropertyDef::Of("target", &Task_GetInRangeContext::target) };
    return properties;
}

std::string Task_GetInRange::GetDebugInfo() const
{
    return "Get in " + range + " range of " + targetSource;
}
