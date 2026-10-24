#include "MovementGoal_GlobalTask.h"

#include "playerbot/PlayerbotAI.h"
#include "playerbot/StateTree/STAI_TaskFactory.h"

#include "Entities/Player.h"

#include <regex>
#include <stdexcept>

using namespace ai;

namespace
{
    const std::regex numberPattern(R"(^\d+(?:\.\d+)?$)");
    const std::regex rangePattern(R"(^[a-z][a-z ]*$)");

    // yards or a range name for ai->GetRange()
    void CheckDistance(const std::string& value, const std::string& what)
    {
        if (!std::regex_match(value, numberPattern) && !std::regex_match(value, rangePattern))
            throw std::runtime_error(what + ": expected yards or a range name such as \"follow\" or \"spell\", got '" + value + "'");
    }

    bool ParseBool(const STAI_TaskConfig& config, const std::string& name, bool fallback)
    {
        const std::string value = STAI_TaskFactory::GetSetting(config, name).value_or(fallback ? "true" : "false");
        if (value == "true" || value == "1")
            return true;
        if (value == "false" || value == "0")
            return false;
        throw std::runtime_error("<" + name + " value=\"" + value + "\">: expected true or false");
    }
}

MovementGoal_GlobalTask::Settings MovementGoal_GlobalTask::ParseSettings(const STAI_TaskConfig& config)
{
    Settings settings;
    settings.name = config.get<std::string>("<xmlattr>.name", "");
    if (settings.name.empty())
        throw std::runtime_error("needs name=\"...\" (shown as where the movement comes from, picked by Root|MovementGoal_GlobalTask[<name>]::...)");

    const std::string priority = config.get<std::string>("<xmlattr>.priority", "");
    char* end = nullptr;
    settings.priority = int32(std::strtol(priority.c_str(), &end, 10));
    if (priority.empty() || *end)
        throw std::runtime_error("needs priority=\"<number>\" (the highest wanted goal drives the bot)");

    const std::string kind = config.get<std::string>("<xmlattr>.kind", "");
    if (kind == "location")
        settings.kind = MovementGoalKind::Location;
    else if (kind == "position")
        settings.kind = MovementGoalKind::Position;
    else if (kind == "range")
        settings.kind = MovementGoalKind::Range;
    else
        throw std::runtime_error("kind=\"" + kind + "\": expected location, position or range");

    const std::string interrupt = config.get<std::string>("<xmlattr>.interruptCast", "false");
    if (interrupt != "true" && interrupt != "false")
        throw std::runtime_error("interruptCast=\"" + interrupt + "\": expected true or false");
    settings.interruptCast = interrupt == "true";

    settings.radius = STAI_TaskFactory::GetSetting(config, "Radius").value_or("2");
    CheckDistance(settings.radius, "<Radius value>");

    if (settings.kind == MovementGoalKind::Range)
    {
        settings.maxRange = STAI_TaskFactory::GetSetting(config, "Range", "max").value_or("");
        if (settings.maxRange.empty())
            throw std::runtime_error("kind=\"range\" needs <Range max=\"melee|<yards>|<range name>\"/>");
        if (settings.maxRange != "melee")
            CheckDistance(settings.maxRange, "<Range max>");
        const std::string min = STAI_TaskFactory::GetSetting(config, "Range", "min").value_or("0");
        if (!std::regex_match(min, numberPattern))
            throw std::runtime_error("<Range min>: expected yards, got '" + min + "'");
        settings.minRange = std::stof(min);

        const std::string side = STAI_TaskFactory::GetSetting(config, "Side").value_or("any");
        if (side == "any")
            settings.side = MovementSide::Any;
        else if (side == "behind")
            settings.side = MovementSide::Behind;
        else if (side == "front")
            settings.side = MovementSide::Front;
        else
            throw std::runtime_error("<Side value=\"" + side + "\">: expected any, behind or front");

        settings.sight = ParseBool(config, "Sight", true);
        settings.dead = ParseBool(config, "Dead", false);
    }
    return settings;
}

void MovementGoal_GlobalTask::Validate(const STAI_TaskConfig& config)
{
    ParseSettings(config);
}

std::unique_ptr<STAI_Task> MovementGoal_GlobalTask::Create(const STAI_TaskConfig& config)
{
    return std::make_unique<MovementGoal_GlobalTask>(ParseSettings(config));
}

float MovementGoal_GlobalTask::ToYards(const STAI_TaskContext& context, const std::string& value)
{
    if (std::regex_match(value, numberPattern))
        return std::stof(value);
    return context.ai->GetRange(value);
}

void MovementGoal_GlobalTask::Update(MovementGoal_GlobalTaskContext& context) const
{
    Player* bot = context.bot;
    PlayerbotMovement& movement = context.ai->GetMovement();

    MovementGoalRequest goal;
    goal.owner = &context;
    goal.name = settings.name;
    goal.source = settings.name;
    if (!context.reason.empty())
        goal.source += " (" + context.reason + ")";
    goal.priority = settings.priority;
    goal.kind = settings.kind;
    goal.interruptCast = settings.interruptCast;
    goal.radius = ToYards(context, settings.radius);

    // the goal has its spot; for range the target is there (alive, or dead for Dead)
    bool hasSpot = false;
    WorldObject* target = nullptr;
    switch (settings.kind)
    {
        case MovementGoalKind::Location:
            goal.location = context.location;
            hasSpot = context.location.valid;
            context.inRange = hasSpot && context.location.mapId == bot->GetMapId() &&
                bot->GetDistance(context.location.x, context.location.y, context.location.z) <= goal.radius;
            break;
        case MovementGoalKind::Position:
            goal.position = context.position;
            hasSpot = context.position.valid;
            context.inRange = hasSpot && bot->GetDistance(context.position.x, context.position.y, context.position.z) <= goal.radius;
            break;
        case MovementGoalKind::Range:
            goal.target = context.target;
            goal.melee = settings.maxRange == "melee";
            goal.radius = goal.melee ? 0.0f : ToYards(context, settings.maxRange);
            goal.minRange = settings.minRange;
            goal.side = settings.side;
            goal.sight = settings.sight;
            goal.dead = settings.dead;
            target = PlayerbotMovement::FindTarget(bot, context.target, settings.dead);
            hasSpot = target != nullptr;
            context.inRange = hasSpot && PlayerbotMovement::IsInRange(bot, goal, target);
            break;
    }

    context.wanted = context.bActive && hasSpot && bot->IsInWorld();
    if (context.wanted)
        movement.Submit(goal);

    // how the movement stands (decided in the last update, after the trees)
    const bool wasActive = context.active;
    const std::string wasMovement = context.movement;
    const MovementGoalRequest& driven = movement.GetActive();
    context.active = context.wanted && movement.HasTreeGoal() && driven.owner == &context;
    context.activeGoal = driven.name;
    context.activeSource = driven.source;
    context.movement = movement.GetStateName();
    if (context.active != wasActive)
        context.onGoalChanged.Broadcast();
    if (context.movement != wasMovement)
        context.onMovementChanged.Broadcast();
}

EActionRunStatus MovementGoal_GlobalTask::OnEnter(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(MovementGoal_GlobalTask::OnEnter);
    MovementGoal_GlobalTaskContext& context = ContextAs<MovementGoal_GlobalTaskContext>(baseContext);
    context.active = false;
    context.inRange = false;
    Update(context);
    return EActionRunStatus::Running;
}

EActionRunStatus MovementGoal_GlobalTask::OnTick(STAI_TaskContext& baseContext, uint32 elapsed, bool minimal) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(MovementGoal_GlobalTask::OnTick);
    MovementGoal_GlobalTaskContext& context = ContextAs<MovementGoal_GlobalTaskContext>(baseContext);
    const bool wasInRange = context.inRange;
    Update(context);
    if (context.inRange && !wasInRange)
        context.onArrived.Broadcast();
    return EActionRunStatus::Running;
}

void MovementGoal_GlobalTask::OnExit(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(MovementGoal_GlobalTask::OnExit);
    // not submitted any more: the movement drops the goal on its next update
    MovementGoal_GlobalTaskContext& context = ContextAs<MovementGoal_GlobalTaskContext>(baseContext);
    context.wanted = false;
    context.active = false;
}

const std::vector<STAI_PropertyDef>& MovementGoal_GlobalTask::GetProperties() const
{
    static const std::vector<STAI_PropertyDef> properties =
    {
        STAI_PropertyDef::Of("location", &MovementGoal_GlobalTaskContext::location),
        STAI_PropertyDef::Of("position", &MovementGoal_GlobalTaskContext::position),
        STAI_PropertyDef::Of("target", &MovementGoal_GlobalTaskContext::target),
        STAI_PropertyDef::Of("reason", &MovementGoal_GlobalTaskContext::reason),
        STAI_PropertyDef::Of("wanted", &MovementGoal_GlobalTaskContext::wanted),
        STAI_PropertyDef::Of("active", &MovementGoal_GlobalTaskContext::active),
        STAI_PropertyDef::Of("inRange", &MovementGoal_GlobalTaskContext::inRange),
        STAI_PropertyDef::Of("activeGoal", &MovementGoal_GlobalTaskContext::activeGoal),
        STAI_PropertyDef::Of("activeSource", &MovementGoal_GlobalTaskContext::activeSource),
        STAI_PropertyDef::Of("movement", &MovementGoal_GlobalTaskContext::movement),
    };
    return properties;
}

const std::vector<STAI_DelegateDef>& MovementGoal_GlobalTask::GetDelegates() const
{
    static const std::vector<STAI_DelegateDef> delegates =
    {
        STAI_DelegateDef::Of("onGoalChanged", &MovementGoal_GlobalTaskContext::onGoalChanged),
        STAI_DelegateDef::Of("onArrived", &MovementGoal_GlobalTaskContext::onArrived),
        STAI_DelegateDef::Of("onMovementChanged", &MovementGoal_GlobalTaskContext::onMovementChanged),
    };
    return delegates;
}
