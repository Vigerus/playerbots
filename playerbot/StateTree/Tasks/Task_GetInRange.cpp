#include "Task_GetInRange.h"

#include "playerbot/PlayerbotAI.h"
#include "playerbot/ServerFacade.h"

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

    Unit* GetTarget(const Task_GetInRangeContext& context)
    {
        Player* bot = context.bot;
        if (context.target.IsEmpty() || !bot->IsInWorld())
            return nullptr;

        Unit* target = bot->GetMap()->GetUnit(context.target);
        return target && target->IsInWorld() && sServerFacade.IsAlive(target) ? target : nullptr;
    }
}

void Task_GetInRange::Validate(const STAI_TaskConfig& config)
{
    ParseRange(config);
}

std::unique_ptr<STAI_Task> Task_GetInRange::Create(const STAI_TaskConfig& config)
{
    return std::make_unique<Task_GetInRange>(ParseRange(config));
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

void Task_GetInRange::OnEnter(STAI_TaskContext& baseContext) const
{
    Task_GetInRangeContext& context = ContextAs<Task_GetInRangeContext>(baseContext);
    context.enterMs = WorldTimer::getMSTime();
    context.chasing = false;
}

EActionRunStatus Task_GetInRange::OnTick(STAI_TaskContext& baseContext, uint32 elapsed, bool minimal) const
{
    Task_GetInRangeContext& context = ContextAs<Task_GetInRangeContext>(baseContext);
    Player* bot = context.bot;

    // nothing to get to: the node finishes, the tree selects again
    Unit* target = GetTarget(context);
    if (!target)
    {
        Stop(context);
        return EActionRunStatus::Succeeded;
    }

    const float yards = GetRange(context);
    if (sServerFacade.GetDistance2d(bot, target) <= yards && bot->IsWithinLOSInMap(target))
    {
        Stop(context);
        return EActionRunStatus::Succeeded;
    }

    if (WorldTimer::getMSTimeDiff(context.enterMs, WorldTimer::getMSTime()) >= timeoutMs)
    {
        Stop(context);
        return EActionRunStatus::Failed;
    }

    // not after a target in a dungeon stage that is not cleared (the master pulls it out)
    if (context.ai->IsStageEntryBlocked(target->GetPositionX(), target->GetPositionY(), target->GetPositionZ()))
    {
        Stop(context);
        return EActionRunStatus::Failed;
    }

    // chase into range; in range without sight the bot closes in until it sees the target
    const bool inRange = sServerFacade.GetDistance2d(bot, target) <= yards;
    const float chaseDistance = inRange ? closeDistance : yards * chaseFactor;
    if (!context.chasing || context.chaseDistance != chaseDistance || bot->GetMotionMaster()->GetCurrentMovementGeneratorType() != CHASE_MOTION_TYPE)
    {
        if (bot->IsSitState())
            bot->SetStandState(UNIT_STAND_STATE_STAND);
        bot->GetMotionMaster()->MoveChase(target, chaseDistance);
        context.chasing = true;
        context.chaseDistance = chaseDistance;
    }
    return EActionRunStatus::Running;
}

void Task_GetInRange::OnExit(STAI_TaskContext& baseContext) const
{
    Stop(ContextAs<Task_GetInRangeContext>(baseContext));
}

const std::vector<STAI_PropertyDef>& Task_GetInRange::GetProperties() const
{
    static const std::vector<STAI_PropertyDef> properties = { STAI_PropertyDef::Of("target", &Task_GetInRangeContext::target) };
    return properties;
}

std::string Task_GetInRange::GetDebugInfo(const STAI_TaskContext& baseContext) const
{
    const Task_GetInRangeContext& context = ContextAs<Task_GetInRangeContext>(baseContext);
    Unit* target = GetTarget(context);
    if (!target)
        return "no target";

    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(1);
    out << sServerFacade.GetDistance2d(context.bot, target) << " / " << GetRange(context) << " yd" << (context.chasing ? ", moving" : "");
    return out.str();
}
