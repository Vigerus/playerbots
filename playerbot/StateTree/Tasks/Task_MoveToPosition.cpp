#include "Task_MoveToPosition.h"

#include "playerbot/PlayerbotAI.h"
#include "playerbot/ServerFacade.h"

#include <iomanip>
#include <regex>
#include <sstream>
#include <stdexcept>

using namespace ai;

namespace
{
    // time the bot may take to get there before the task fails
    constexpr uint32 timeoutMs = 30000;

    const std::regex numberPattern(R"(^\d+(?:\.\d+)?$)");

    float ParseRadius(const STAI_TaskConfig& config)
    {
        const std::string value = STAI_TaskFactory::GetSetting(config, "AcceptableRadius").value_or("2");
        if (!std::regex_match(value, numberPattern) || std::stof(value) <= 0.0f)
            throw std::runtime_error("AcceptableRadius: expected yards greater than 0, got '" + value + "'");
        return std::stof(value);
    }
}

void Task_MoveToPosition::Validate(const STAI_TaskConfig& config)
{
    ParseRadius(config);
}

std::unique_ptr<STAI_Task> Task_MoveToPosition::Create(const STAI_TaskConfig& config)
{
    return std::make_unique<Task_MoveToPosition>(ParseRadius(config), STAI_TaskFactory::GetBindingSource(config, "position"));
}

void Task_MoveToPosition::OnEnter(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(Task_MoveToPosition::OnEnter);
    Task_MoveToPositionContext& context = ContextAs<Task_MoveToPositionContext>(baseContext);
    context.moving = false;
    context.destination = STAI_Position();
    context.elapsedMs = 0;
    context.lastDistance = -1.0f;
}

EActionRunStatus Task_MoveToPosition::OnTick(STAI_TaskContext& baseContext, uint32 elapsed, bool minimal) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(Task_MoveToPosition::OnTick);
    Task_MoveToPositionContext& context = ContextAs<Task_MoveToPositionContext>(baseContext);
    PlayerbotAI* ai = context.ai;
    Player* bot = context.bot;

    if (!context.position.valid)
        return EActionRunStatus::Succeeded;

    const STAI_Position& position = context.position;
    context.lastDistance = bot->GetDistance(position.x, position.y, position.z);
    if (context.lastDistance <= acceptableRadius)
        return EActionRunStatus::Succeeded;

    context.elapsedMs += elapsed;
    if (context.elapsedMs > timeoutMs)
        return EActionRunStatus::Failed;

    if (!ai->CanMove())
        return EActionRunStatus::Running;

    const bool stopped = bot->GetMotionMaster()->GetCurrentMovementGeneratorType() != POINT_MOTION_TYPE;
    if (context.destination != position || stopped)
    {
        if (bot->IsSitState())
            bot->SetStandState(UNIT_STAND_STATE_STAND);

        // the path is calculated on the PathfindingWorker, the bot stands until it is there
        bot->GetMotionMaster()->MovePointAsync(0, position.x, position.y, position.z, FORCED_MOVEMENT_RUN);
        context.destination = position;
        context.moving = true;
    }

    return EActionRunStatus::Running;
}

void Task_MoveToPosition::OnExit(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(Task_MoveToPosition::OnExit);
    Task_MoveToPositionContext& context = ContextAs<Task_MoveToPositionContext>(baseContext);
    if (context.moving && context.bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == POINT_MOTION_TYPE)
        context.ai->StopMoving();

    context.moving = false;
    context.destination = STAI_Position();
}

const std::vector<STAI_PropertyDef>& Task_MoveToPosition::GetProperties() const
{
    static const std::vector<STAI_PropertyDef> properties = { STAI_PropertyDef::Of("position", &Task_MoveToPositionContext::position) };
    return properties;
}

std::string Task_MoveToPosition::GetDebugInfo() const
{
    std::ostringstream out;
    out << "Move to " << positionSource << ", radius " << acceptableRadius;
    return out.str();
}
