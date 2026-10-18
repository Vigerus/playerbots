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

Task_Follow::Task_Follow(const STAI_TaskContext& context, const std::string& acceptableRadius)
    : ai(context.ai), bot(context.bot), acceptableRadius(acceptableRadius)
{
}

void Task_Follow::Validate(const STAI_TaskConfig& config)
{
    const std::string target = STAI_TaskFactory::GetSetting(config, "Target").value_or("player");
    if (target != "player")
        throw std::runtime_error("Target: only \"player\" (the bot's master) is supported, got '" + target + "'");

    ParseRadius(config);
}

std::unique_ptr<STAI_Task> Task_Follow::Create(const STAI_TaskContext& context, const STAI_TaskConfig& config)
{
    return std::make_unique<Task_Follow>(context, ParseRadius(config));
}

Unit* Task_Follow::GetTarget() const
{
    Player* master = ai->GetMaster();
    if (!master || master == bot || !master->IsInWorld() || bot->IsBeingTeleported() || master->IsBeingTeleported())
        return nullptr;

    if (master->GetMapId() != bot->GetMapId() || master->GetInstanceId() != bot->GetInstanceId())
        return nullptr;

    return master;
}

float Task_Follow::GetRadius() const
{
    if (std::regex_match(acceptableRadius, numberPattern))
        return std::stof(acceptableRadius);
    return ai->GetRange(acceptableRadius);
}

void Task_Follow::MoveTo(float x, float y, float z)
{
    if (bot->IsSitState())
        bot->SetStandState(UNIT_STAND_STATE_STAND);

    // always run, also when the master walks
    bot->GetMotionMaster()->MovePoint(0, x, y, z, FORCED_MOVEMENT_RUN, true);

    destinationX = x;
    destinationY = y;
    destinationZ = z;
    hasDestination = true;
    moving = true;
}

void Task_Follow::OnEnter()
{
    elapsedMs = 0;
    lastDistance = -1.0f;
    hasDestination = false;
    moving = false;

    // keep the side the bot comes from
    if (Unit* target = GetTarget())
        relativeAngle = target->GetAngle(bot) - target->GetOrientation();
}

EActionRunStatus Task_Follow::Update(uint32 elapsed, bool minimal)
{
    Unit* target = GetTarget();
    if (!target || target->IsTaxiFlying() || bot->GetTransport() != target->GetTransport())
        return EActionRunStatus::Failed;

    const float radius = GetRadius();
    lastDistance = sServerFacade.GetDistance2d(bot, target);

    if (lastDistance <= radius)
    {
        // caught up: done once the target stands still, otherwise keep walking along
        if (!target->IsMoving())
            return EActionRunStatus::Succeeded;
        elapsedMs = 0;
    }
    else
    {
        elapsedMs += elapsed;
        if (elapsedMs > timeoutMs)
            return EActionRunStatus::Failed;
    }

    if (!ai->CanMove())
        return EActionRunStatus::Running;

    // destination next to the target, on the bot's side, well inside the radius
    float x, y, z;
    const float standDistance = std::min(ai->GetRange("follow"), radius * 0.5f);
    target->GetNearPoint(bot, x, y, z, bot->GetObjectBoundingRadius(), standDistance, target->GetOrientation() + relativeAngle);

    const bool destinationMoved = !hasDestination
        || (x - destinationX) * (x - destinationX) + (y - destinationY) * (y - destinationY) > repathDistance * repathDistance;
    const bool stopped = bot->GetMotionMaster()->GetCurrentMovementGeneratorType() != POINT_MOTION_TYPE;

    if (destinationMoved || (stopped && lastDistance > radius))
        MoveTo(x, y, z);

    return EActionRunStatus::Running;
}

void Task_Follow::OnExit()
{
    if (moving && bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == POINT_MOTION_TYPE)
        ai->StopMoving();

    moving = false;
    hasDestination = false;
}

std::string Task_Follow::GetDebugInfo() const
{
    std::ostringstream out;
    out << std::fixed << std::setprecision(1) << "player, radius ";
    if (std::regex_match(acceptableRadius, numberPattern))
        out << GetRadius();
    else
        out << acceptableRadius << " (" << GetRadius() << ")";
    if (lastDistance >= 0.0f)
        out << ", distance " << lastDistance;
    return out.str();
}
