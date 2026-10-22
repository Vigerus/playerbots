#include "Task_MakePath.h"

#include "playerbot/PlayerbotAI.h"

#include <iomanip>
#include <sstream>
#include <stdexcept>

using namespace ai;

namespace
{
    // time the worker may take before the task fails
    constexpr uint32 timeoutMs = 10000;

    float ParseCoordinate(const STAI_TaskConfig& config, const std::string& axis)
    {
        const std::optional<std::string> value = STAI_TaskFactory::GetSetting(config, "Location", axis);
        if (!value)
            throw std::runtime_error("<Location>: missing " + axis);
        try
        {
            size_t end = 0;
            const float coordinate = std::stof(*value, &end);
            if (end != value->size())
                throw std::invalid_argument(*value);
            return coordinate;
        }
        catch (const std::exception&)
        {
            throw std::runtime_error("<Location " + axis + "=\"...\">: expected a number, got '" + *value + "'");
        }
    }

    // <Location map="..." x="..." y="..." z="..."/>, unset without one (the location is bound); mapId 0 with
    // map left out is resolved to the bot's map in OnEnter
    STAI_Location ParseLocation(const STAI_TaskConfig& config, bool& hasMap)
    {
        hasMap = false;
        if (!config.get_child_optional("Location"))
            return STAI_Location();

        STAI_Location location;
        if (const std::optional<std::string> map = STAI_TaskFactory::GetSetting(config, "Location", "map"))
        {
            try
            {
                location.mapId = uint32(std::stoul(*map));
            }
            catch (const std::exception&)
            {
                throw std::runtime_error("<Location map=\"...\">: expected a map id, got '" + *map + "'");
            }
            hasMap = true;
        }
        location.x = ParseCoordinate(config, "x");
        location.y = ParseCoordinate(config, "y");
        location.z = ParseCoordinate(config, "z");
        location.valid = true;
        return location;
    }
}

void Task_MakePath::Validate(const STAI_TaskConfig& config)
{
    // without <Location> the location is bound; unset at runtime the task fails
    bool hasMap;
    ParseLocation(config, hasMap);
}

std::unique_ptr<STAI_Task> Task_MakePath::Create(const STAI_TaskConfig& config)
{
    bool hasMap;
    STAI_Location location = ParseLocation(config, hasMap);
    if (location.valid && !hasMap)
        location.mapId = UINT32_MAX;    // the bot's map
    return std::make_unique<Task_MakePath>(location, STAI_TaskFactory::GetBindingSource(config, "location"));
}

void Task_MakePath::OnEnter(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(Task_MakePath::OnEnter);
    Task_MakePathContext& context = ContextAs<Task_MakePathContext>(baseContext);
    Player* bot = context.bot;

    context.path = STAI_Path();
    context.request.reset();
    context.elapsedMs = 0;

    if (fixedLocation.valid)
    {
        context.location = fixedLocation;
        if (context.location.mapId == UINT32_MAX)
            context.location.mapId = bot->GetMapId();
    }

    if (!context.location.valid || !bot->IsInWorld() || bot->IsBeingTeleported())
        return;

    const STAI_Location& location = context.location;
    context.request = sPathfindingWorker.Request(bot->GetMapId(), Vector3(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ()),
        location.mapId, Vector3(location.x, location.y, location.z));
}

EActionRunStatus Task_MakePath::OnTick(STAI_TaskContext& baseContext, uint32 elapsed, bool minimal) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(Task_MakePath::OnTick);
    Task_MakePathContext& context = ContextAs<Task_MakePathContext>(baseContext);

    if (!context.request)
        return EActionRunStatus::Failed;

    if (!context.request->IsDone())
    {
        context.elapsedMs += elapsed;
        return context.elapsedMs > timeoutMs ? EActionRunStatus::Failed : EActionRunStatus::Running;
    }

    if (!context.request->IsReady())
        return EActionRunStatus::Failed;

    context.path.request = context.request;
    return EActionRunStatus::Succeeded;
}

void Task_MakePath::OnExit(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(Task_MakePath::OnExit);
    // a request nobody holds is skipped by the worker; the path stays for Task_MoveTo
    ContextAs<Task_MakePathContext>(baseContext).request.reset();
}

const std::vector<STAI_PropertyDef>& Task_MakePath::GetProperties() const
{
    static const std::vector<STAI_PropertyDef> properties =
    {
        STAI_PropertyDef::Of("location", &Task_MakePathContext::location),
        STAI_PropertyDef::Of("path", &Task_MakePathContext::path),
    };
    return properties;
}

std::string Task_MakePath::GetDebugInfo() const
{
    std::ostringstream out;
    out << "Make path to ";
    if (fixedLocation.valid)
    {
        out << std::fixed << std::setprecision(1);
        if (fixedLocation.mapId != UINT32_MAX)
            out << "map " << fixedLocation.mapId << " ";
        out << fixedLocation.x << " " << fixedLocation.y << " " << fixedLocation.z;
    }
    else
        out << locationSource;
    return out.str();
}
