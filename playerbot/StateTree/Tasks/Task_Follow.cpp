#include "Task_Follow.h"

#include "playerbot/PlayerbotAI.h"
#include "playerbot/ServerFacade.h"

#include <iomanip>
#include <sstream>
#include <stdexcept>

using namespace ai;

namespace
{
    constexpr float defaultAcceptableRadius = 10.0f;
    constexpr uint32 timeoutMs = 30000;

    float ParseRadius(const STAI_TaskConfig& config)
    {
        const std::optional<std::string> value = STAI_TaskFactory::GetSetting(config, "AcceptableRadius");
        if (!value)
            return defaultAcceptableRadius;

        float radius = 0.0f;
        try { radius = std::stof(*value); }
        catch (const std::exception&) { throw std::runtime_error("AcceptableRadius: expected a number, got '" + *value + "'"); }

        if (radius <= 0.0f)
            throw std::runtime_error("AcceptableRadius must be greater than 0");
        return radius;
    }
}

Task_Follow::Task_Follow(const STAI_TaskContext& context, float acceptableRadius)
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

void Task_Follow::StartMoving(Unit* target)
{
    if (bot->IsSitState())
        bot->SetStandState(UNIT_STAND_STATE_STAND);

    // stop well inside the radius so the bot does not hover at its edge
    const float followDistance = std::min(sPlayerbotAIConfig.followDistance, acceptableRadius * 0.5f);
    const float angle = target->GetAngle(bot) - target->GetOrientation();

    bot->GetMotionMaster()->MoveFollow(target, followDistance, angle);
    moving = true;
}

void Task_Follow::OnEnter()
{
    elapsedMs = 0;
    lastDistance = -1.0f;

    if (Unit* target = GetTarget())
        StartMoving(target);
}

EActionRunStatus Task_Follow::Update(uint32 elapsed, bool minimal)
{
    Unit* target = GetTarget();
    if (!target)
        return EActionRunStatus::Failed;

    lastDistance = sServerFacade.GetDistance2d(bot, target);
    if (lastDistance <= acceptableRadius)
        return EActionRunStatus::Succeeded;

    elapsedMs += elapsed;
    if (elapsedMs > timeoutMs)
        return EActionRunStatus::Failed;

    // something else replaced the movement (knockback, fear, ...): follow again
    if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() != FOLLOW_MOTION_TYPE)
        StartMoving(target);

    return EActionRunStatus::Running;
}

void Task_Follow::OnExit()
{
    if (moving && bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == FOLLOW_MOTION_TYPE)
        ai->StopMoving();

    moving = false;
}

std::string Task_Follow::GetDebugInfo() const
{
    std::ostringstream out;
    out << std::fixed << std::setprecision(1) << "player, radius " << acceptableRadius;
    if (lastDistance >= 0.0f)
        out << ", distance " << lastDistance;
    return out.str();
}
