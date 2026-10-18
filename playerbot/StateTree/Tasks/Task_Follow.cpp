#include "Task_Follow.h"

#include "playerbot/PlayerbotAI.h"
#include "playerbot/ServerFacade.h"

#include <iomanip>
#include <regex>
#include <sstream>
#include <stdexcept>

using namespace ai;

namespace
{
    // time the bot may spend outside the radius before the task fails
    constexpr uint32 timeoutMs = 30000;
    // a new move is ordered when the destination moved further than this
    constexpr float repathDistance = 1.0f;

    const std::regex numberPattern(R"(^\d+(?:\.\d+)?$)");
    const std::regex rangePattern(R"(^[a-z][a-z ]*$)");

    // yards or a range name for ai->GetRange(); default: the bot's follow distance
    std::string ParseRadius(const STAI_TaskConfig& config)
    {
        const std::string value = STAI_TaskFactory::GetSetting(config, "AcceptableRadius").value_or("follow");

        if (std::regex_match(value, numberPattern))
        {
            if (std::stof(value) <= 0.0f)
                throw std::runtime_error("AcceptableRadius must be greater than 0");
            return value;
        }

        if (!std::regex_match(value, rangePattern))
            throw std::runtime_error("AcceptableRadius: expected yards or a range name such as \"follow\", got '" + value + "'");
        return value;
    }
}

Task_Follow::Task_Follow(const std::string& acceptableRadius)
    : acceptableRadius(acceptableRadius)
{
}

void Task_Follow::Validate(const STAI_TaskConfig& config)
{
    const std::string target = STAI_TaskFactory::GetSetting(config, "Target").value_or("player");
    if (target != "player")
        throw std::runtime_error("Target: only \"player\" (the bot's master) is supported, got '" + target + "'");

    ParseRadius(config);
}

std::unique_ptr<STAI_Task> Task_Follow::Create(const STAI_TaskConfig& config)
{
    return std::make_unique<Task_Follow>(ParseRadius(config));
}

Unit* Task_Follow::GetTarget(const STAI_TaskContext& context)
{
    Player* bot = context.bot;
    Player* master = context.ai->GetMaster();
    if (!master || master == bot || !master->IsInWorld() || bot->IsBeingTeleported() || master->IsBeingTeleported())
        return nullptr;

    if (master->GetMapId() != bot->GetMapId() || master->GetInstanceId() != bot->GetInstanceId())
        return nullptr;

    return master;
}

float Task_Follow::GetRadius(const STAI_TaskContext& context) const
{
    if (std::regex_match(acceptableRadius, numberPattern))
        return std::stof(acceptableRadius);
    return context.ai->GetRange(acceptableRadius);
}

void Task_Follow::MoveTo(Task_FollowContext& context, float x, float y, float z)
{
    Player* bot = context.bot;
    if (bot->IsSitState())
        bot->SetStandState(UNIT_STAND_STATE_STAND);

    // always run, also when the master walks
    bot->GetMotionMaster()->MovePoint(0, x, y, z, FORCED_MOVEMENT_RUN, true);

    context.destinationX = x;
    context.destinationY = y;
    context.destinationZ = z;
    context.hasDestination = true;
    context.moving = true;
}

void Task_Follow::OnEnter(STAI_TaskContext& baseContext) const
{
    Task_FollowContext& context = ContextAs<Task_FollowContext>(baseContext);
    context.elapsedMs = 0;
    context.lastDistance = -1.0f;
    context.hasDestination = false;
    context.moving = false;

    // keep the side the bot comes from
    if (Unit* target = GetTarget(context))
        context.relativeAngle = target->GetAngle(context.bot) - target->GetOrientation();
}

EActionRunStatus Task_Follow::OnTick(STAI_TaskContext& baseContext, uint32 elapsed, bool minimal) const
{
    Task_FollowContext& context = ContextAs<Task_FollowContext>(baseContext);
    PlayerbotAI* ai = context.ai;
    Player* bot = context.bot;

    Unit* target = GetTarget(context);
    if (!target || target->IsTaxiFlying() || bot->GetTransport() != target->GetTransport())
        return EActionRunStatus::Failed;

    const float radius = GetRadius(context);
    context.lastDistance = sServerFacade.GetDistance2d(bot, target);

    if (context.lastDistance <= radius)
    {
        // caught up: done once the target stands still, otherwise keep walking along
        if (!target->IsMoving())
            return EActionRunStatus::Succeeded;
        context.elapsedMs = 0;
    }
    else
    {
        context.elapsedMs += elapsed;
        if (context.elapsedMs > timeoutMs)
            return EActionRunStatus::Failed;
    }

    if (!ai->CanMove())
        return EActionRunStatus::Running;

    // destination next to the target, on the bot's side, well inside the radius
    float x, y, z;
    const float standDistance = std::min(ai->GetRange("follow"), radius * 0.5f);
    target->GetNearPoint(bot, x, y, z, bot->GetObjectBoundingRadius(), standDistance, target->GetOrientation() + context.relativeAngle);

    const float dx = x - context.destinationX, dy = y - context.destinationY;
    const bool destinationMoved = !context.hasDestination || dx * dx + dy * dy > repathDistance * repathDistance;
    const bool stopped = bot->GetMotionMaster()->GetCurrentMovementGeneratorType() != POINT_MOTION_TYPE;

    if (destinationMoved || (stopped && context.lastDistance > radius))
        MoveTo(context, x, y, z);

    return EActionRunStatus::Running;
}

void Task_Follow::OnExit(STAI_TaskContext& baseContext) const
{
    Task_FollowContext& context = ContextAs<Task_FollowContext>(baseContext);
    if (context.moving && context.bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == POINT_MOTION_TYPE)
        context.ai->StopMoving();

    context.moving = false;
    context.hasDestination = false;
}

std::string Task_Follow::GetDebugInfo(const STAI_TaskContext& baseContext) const
{
    const Task_FollowContext& context = ContextAs<Task_FollowContext>(baseContext);

    std::ostringstream out;
    out << std::fixed << std::setprecision(1) << "player, radius ";
    if (std::regex_match(acceptableRadius, numberPattern))
        out << GetRadius(context);
    else
        out << acceptableRadius << " (" << GetRadius(context) << ")";
    if (context.lastDistance >= 0.0f)
        out << ", distance " << context.lastDistance;
    return out.str();
}
