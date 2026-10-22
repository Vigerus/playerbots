#include "Task_MoveTo.h"

#include "playerbot/PlayerbotAI.h"
#include "playerbot/ServerFacade.h"

#include "MotionGenerators/MotionMaster.h"
#include "Server/DBCStores.h"
#include "World/World.h"

#include <regex>
#include <stdexcept>

using namespace ai;

namespace
{
    // walking time of the path the bot may take on top before the task fails
    constexpr uint32 extraTimeMs = 10000;

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

    // the leg of the path on the bot's map, nullptr when the path does not cross it
    const NavPathSegment* FindLeg(const STAI_Path& path, uint32 mapId, bool& last)
    {
        const std::vector<NavPathSegment>& segments = path.request->segments;
        for (size_t i = 0; i < segments.size(); ++i)
            if (segments[i].mapId == mapId)
            {
                last = i + 1 == segments.size();
                return &segments[i];
            }
        return nullptr;
    }

    // where the leg ends: the destination, or the centre of its navlink's area trigger
    Vector3 GetLegEnd(const STAI_Path& path, const NavPathSegment& leg, bool last)
    {
        if (last)
            return path.request->dest;
        if (AreaTriggerEntry const* entry = sAreaTriggerStore.LookupEntry(leg.areaTriggerId))
            return Vector3(entry->x, entry->y, entry->z);
        return leg.points.back();
    }

    float Distance(Player* bot, const Vector3& point)
    {
        return bot->GetDistance(point.x, point.y, point.z);
    }

    bool IsMoving(Player* bot)
    {
        const MovementGeneratorType type = bot->GetMotionMaster()->GetCurrentMovementGeneratorType();
        return type == PATH_MOTION_TYPE || type == POINT_MOTION_TYPE;
    }
}

void Task_MoveTo::Validate(const STAI_TaskConfig& config)
{
    ParseRadius(config);
}

std::unique_ptr<STAI_Task> Task_MoveTo::Create(const STAI_TaskConfig& config)
{
    return std::make_unique<Task_MoveTo>(ParseRadius(config), STAI_TaskFactory::GetBindingSource(config, "path"));
}

float Task_MoveTo::GetRadius(const STAI_TaskContext& context) const
{
    if (std::regex_match(acceptableRadius, numberPattern))
        return std::stof(acceptableRadius);
    return context.ai->GetRange(acceptableRadius);
}

EActionRunStatus Task_MoveTo::OnEnter(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(Task_MoveTo::OnEnter);
    StartLog(baseContext);
    Task_MoveToContext& context = ContextAs<Task_MoveToContext>(baseContext);
    Player* bot = context.bot;

    context.mapId = bot->GetMapId();
    context.moving = false;
    context.pushed = false;
    context.keepMoving = false;
    context.elapsedMs = 0;
    context.timeoutMs = extraTimeMs;
    context.startDistance = 0.0f;

    bool last;
    const NavPathSegment* leg = context.path.IsEmpty() ? nullptr : FindLeg(context.path, context.mapId, last);
    // no leg on this map: OnTick decides (Running while teleported, else Failed)
    if (!leg)
        return EActionRunStatus::Running;

    context.startDistance = Distance(bot, GetLegEnd(context.path, *leg, last));

    float length = 0.0f;
    for (size_t i = 1; i < leg->points.size(); ++i)
        length += (leg->points[i] - leg->points[i - 1]).length();
    const float speed = std::max(bot->GetSpeed(MOVE_RUN), 1.0f);
    context.timeoutMs = uint32(length / speed * float(IN_MILLISECONDS)) + extraTimeMs;
    return EActionRunStatus::Running;
}

EActionRunStatus Task_MoveTo::OnTick(STAI_TaskContext& baseContext, uint32 elapsed, bool minimal) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(Task_MoveTo::OnTick);
    Task_MoveToContext& context = ContextAs<Task_MoveToContext>(baseContext);
    PlayerbotAI* ai = context.ai;
    Player* bot = context.bot;

    if (bot->IsBeingTeleported())
        return LogStatus(baseContext, EActionRunStatus::Running);

    // went through a navlink: the node plans again on the new map
    if (bot->GetMapId() != context.mapId)
        return LogStatus(baseContext, EActionRunStatus::Succeeded);

    bool last;
    const NavPathSegment* leg = context.path.IsEmpty() ? nullptr : FindLeg(context.path, context.mapId, last);
    if (!leg)
        return LogStatus(baseContext, EActionRunStatus::Failed);

    const float radius = GetRadius(context);
    const Vector3 end = GetLegEnd(context.path, *leg, last);
    const float distance = Distance(bot, end);

    if (last)
    {
        if (distance <= radius)
        {
            if (context.moving && IsMoving(bot))
                ai->StopMoving();
            context.moving = false;
            return LogStatus(baseContext, EActionRunStatus::Succeeded);
        }

        // the target moved away from where the path leads: plan again, walking on meanwhile
        const STAI_Location& location = context.location;
        if (location.valid && (location.mapId != context.mapId || (Vector3(location.x, location.y, location.z) - end).length() > radius))
        {
            context.keepMoving = true;
            return LogStatus(baseContext, EActionRunStatus::Succeeded);
        }
    }

    if (!ai->CanMove())
        return LogStatus(baseContext, EActionRunStatus::Running);

    context.elapsedMs += elapsed;
    if (context.elapsedMs > context.timeoutMs)
        return LogStatus(baseContext, EActionRunStatus::Failed);

    if (!context.moving)
    {
        if (bot->IsSitState())
            bot->SetStandState(UNIT_STAND_STATE_STAND);

        // the worker has no unit to put the points on the ground with; the first point is where the bot stood
        std::vector<G3D::Vector3> points(leg->points.begin() + 1, leg->points.end());
        if (sWorld.getConfig(CONFIG_BOOL_PATH_FIND_NORMALIZE_Z))
            for (G3D::Vector3& point : points)
                bot->UpdateAllowedPositionZ(point.x, point.y, point.z);
        if (points.empty())
            points.push_back(end);

        bot->GetMotionMaster()->MovePath(points, FORCED_MOVEMENT_RUN, false, false);
        context.moving = true;
        return LogStatus(baseContext, EActionRunStatus::Running);
    }

    if (IsMoving(bot))
        return LogStatus(baseContext, EActionRunStatus::Running);

    // the path ended
    if (!last)
    {
        // short of the portal: straight into its area trigger, then wait for the teleport (or the timeout when the
        // bot may not enter)
        if (!context.pushed)
        {
            bot->GetMotionMaster()->MovePoint(0, end.x, end.y, end.z, FORCED_MOVEMENT_RUN, false);
            context.pushed = true;
        }
        return LogStatus(baseContext, EActionRunStatus::Running);
    }

    // cut short (length limit, incomplete navmesh path): plan again from here when it brought the bot closer
    context.moving = false;
    return LogStatus(baseContext, context.startDistance - distance > radius ? EActionRunStatus::Succeeded : EActionRunStatus::Failed);
}

void Task_MoveTo::OnExit(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(Task_MoveTo::OnExit);
    // left while running: the run did not get done
    EndLog(baseContext, false);
    Task_MoveToContext& context = ContextAs<Task_MoveToContext>(baseContext);
    if (context.moving && !context.keepMoving && IsMoving(context.bot))
        context.ai->StopMoving();

    context.moving = false;
    context.keepMoving = false;
}

const std::vector<STAI_PropertyDef>& Task_MoveTo::GetProperties() const
{
    static const std::vector<STAI_PropertyDef> properties =
    {
        STAI_PropertyDef::Of("path", &Task_MoveToContext::path),
        STAI_PropertyDef::Of("location", &Task_MoveToContext::location),
    };
    return properties;
}
