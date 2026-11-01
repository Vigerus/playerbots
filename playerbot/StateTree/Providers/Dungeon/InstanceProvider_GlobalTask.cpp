#include "InstanceProvider_GlobalTask.h"

#include "playerbot/PlayerSpec.h"
#include "playerbot/PlayerbotAI.h"
#include "playerbot/ServerFacade.h"

#include "Maps/DungeonPlannerSubsystem.h"
#include "Maps/InstanceSubsystem.h"
#include "Maps/Map.h"
#include "MotionGenerators/PathFinder.h"
#include "Util/Timer.h"
#include "World/World.h"

#include <iomanip>
#include <map>
#include <sstream>

using namespace ai;

namespace
{
    const uint32 STAGE_CHECK_INTERVAL = 500;      // ms
    const float STAGE_POSITION_RADIUS = 2.0f;     // yards: the bot stands at its stage position
    const float PATH_SAMPLE_STEP = 2.0f;          // yards between the points of the way tested against the stages

    // the stage's name for the reasons, "stage <index>" without one
    std::string StageName(const std::vector<InstanceStageDef>& stages, int32 stage)
    {
        return uint32(stage) < stages.size() && !stages[stage].name.empty() ? stages[stage].name : "stage " + std::to_string(stage);
    }

    // creatures that would aggro count from this far beyond their aggro range
    const float AGGRO_MARGIN = 3.0f;
    // a bot tells its master that a creature blocks its way at most this often
    const uint32 BLOCKED_TELL_INTERVAL = 15000;   // ms

    // a waypoint of a plan's path counts as reached this close
    const float PATH_WAYPOINT_RADIUS = 3.0f;

    // the ground at a planned point: its height comes from the planner's map (estimated), the map's height below a bit
    // above it when that is close
    float SnapHeight(Map* map, const DungeonPlanPoint& point)
    {
        const float height = map->GetHeight(point.x, point.y, point.z + 5.0f);
        return height > INVALID_HEIGHT && std::abs(height - point.z) < 15.0f ? height : point.z;
    }

    Movement::PointsArray PathTo(Player* bot, float x, float y, float z)
    {
        PathFinder path(bot);
        path.calculate(x, y, z);
        return path.getPath();
    }

    // calls check for points along the way every PATH_SAMPLE_STEP yards, until it returns true; true then
    template <class Check>
    bool AnySample(const Movement::PointsArray& points, Check check)
    {
        for (size_t i = 1; i < points.size(); ++i)
        {
            const Vector3& from = points[i - 1];
            const Vector3& to = points[i];
            const float length = (to - from).length();
            const uint32 samples = std::max<uint32>(1, uint32(length / PATH_SAMPLE_STEP));
            for (uint32 j = 1; j <= samples; ++j)
                if (check(from + (to - from) * (float(j) / samples)))
                    return true;
        }
        return false;
    }

    // the way crosses a stage that is not cleared, other than the stage the bot is in and targetStage (the master's
    // stage when the spot is its uncleared point)
    bool CrossesUnclearedStage(InstanceSubsystem* instances, Map* map, const Movement::PointsArray& points, int32 botStage, int32 targetStage)
    {
        std::map<int32, bool> cleared;
        return AnySample(points, [&](const Vector3& point)
        {
            const int32 stage = instances->FindStage(map->GetId(), point.x, point.y, point.z);
            if (stage < 0 || stage == botStage || stage == targetStage)
                return false;

            auto itr = cleared.find(stage);
            if (itr == cleared.end())
                itr = cleared.emplace(stage, instances->IsStageCleared(map, uint32(stage))).first;
            return !itr->second;
        });
    }

    // the creature of the dungeon's packs that would aggro on the bot along the way: alive, hostile, not fighting yet,
    // within its aggro range (+ AGGRO_MARGIN) of the way and in its line of sight there. Not one the bot is in range of already (it does not aggro,
    // passive) nor, for a spot the master chose (chosen), one in range of the spot. Null when none.
    Creature* AggroOnPath(Player* bot, const Movement::PointsArray& points, bool chosen)
    {
        if (points.size() < 2)
            return nullptr;

        Map* map = bot->GetMap();
        DungeonPlannerSubsystem* planner = sWorld.GetSubsystem<DungeonPlannerSubsystem>();
        if (!planner)
            return nullptr;
        float length = 0.0f;
        for (size_t i = 1; i < points.size(); ++i)
            length += (points[i] - points[i - 1]).length();
        const Vector3& end = points.back();

        std::vector<std::pair<Creature*, float>> nearby;
        for (const DungeonPack& pack : *planner->GetPacks(map->GetId()))
            for (const DungeonPackSpawn& spawn : pack.spawns)
            {
                Creature* creature = map->GetCreature(spawn.dbGuid);
                if (!creature || !creature->IsAlive() || creature->IsInCombat() || !creature->IsEnemy(bot))
                    continue;
                const float radius = creature->GetAttackDistance(bot) + AGGRO_MARGIN;
                if (bot->GetDistance(creature) > length + radius || bot->GetDistance(creature) <= radius)
                    continue;
                if (chosen && creature->GetDistance(end.x, end.y, end.z) <= radius)
                    continue;
                nearby.emplace_back(creature, radius);
            }
        if (nearby.empty())
            return nullptr;

        Creature* found = nullptr;
        AnySample(points, [&](const Vector3& point)
        {
            // in its aggro range and in its sight there (a creature on another floor or behind a wall does not aggro)
            for (const auto& [creature, radius] : nearby)
                if (creature->GetDistance(point.x, point.y, point.z) <= radius &&
                    creature->IsWithinLOS(point.x, point.y, point.z + bot->GetCollisionHeight()))
                {
                    found = creature;
                    return true;
                }
            return false;
        });
        return found;
    }
}

EActionRunStatus InstanceProvider_GlobalTask::OnEnterState(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(InstanceProvider_GlobalTask::OnEnterState);
    InstanceProvider_GlobalTaskContext& context = ContextAs<InstanceProvider_GlobalTaskContext>(baseContext);
    context.Subscribe();
    if (context.bot->IsInWorld())
        context.SetMap(context.bot->GetMap(), false);
    context.UpdateStages();
    return EActionRunStatus::Running;
}

void InstanceProvider_GlobalTask::OnExitState(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(InstanceProvider_GlobalTask::OnExitState);
    // mapId / instanceId stay: a change while the tree does not run fires when it starts again
    ContextAs<InstanceProvider_GlobalTaskContext>(baseContext).Unsubscribe();
}

EActionRunStatus InstanceProvider_GlobalTask::OnTick(STAI_TaskContext& baseContext, uint32 elapsed, bool minimal) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(InstanceProvider_GlobalTask::OnTick);
    InstanceProvider_GlobalTaskContext& context = ContextAs<InstanceProvider_GlobalTaskContext>(baseContext);
    if (!context.hasStages && !context.inDungeon)
    {
        context.planChanged = false;
        return EActionRunStatus::Running;
    }

    // another preset / pull of the master's plan: at once
    if (context.planChanged.exchange(false))
    {
        context.UpdateStages();
        context.onPlanChanged.Broadcast();
        return EActionRunStatus::Running;
    }

    context.stageTimer += elapsed;
    if (context.stageTimer >= STAGE_CHECK_INTERVAL)
        context.UpdateStages();
    return EActionRunStatus::Running;
}

const std::vector<STAI_PropertyDef>& InstanceProvider_GlobalTask::GetProperties() const
{
    static const std::vector<STAI_PropertyDef> properties =
    {
        STAI_PropertyDef::Of("mapId", &InstanceProvider_GlobalTaskContext::mapId),
        STAI_PropertyDef::Of("instanceId", &InstanceProvider_GlobalTaskContext::instanceId),
        STAI_PropertyDef::Of("inDungeon", &InstanceProvider_GlobalTaskContext::inDungeon),
        STAI_PropertyDef::Of("stage", &InstanceProvider_GlobalTaskContext::stage),
        STAI_PropertyDef::Of("masterStage", &InstanceProvider_GlobalTaskContext::masterStage),
        STAI_PropertyDef::Of("masterStageCleared", &InstanceProvider_GlobalTaskContext::masterStageCleared),
        STAI_PropertyDef::Of("pathBlocked", &InstanceProvider_GlobalTaskContext::pathBlocked),
        STAI_PropertyDef::Of("stagePosition", &InstanceProvider_GlobalTaskContext::stagePosition),
        STAI_PropertyDef::Of("stagePositionFar", &InstanceProvider_GlobalTaskContext::stagePositionFar),
        STAI_PropertyDef::Of("stagePositionFrom", &InstanceProvider_GlobalTaskContext::stagePositionFrom),
        STAI_PropertyDef::Of("preset", &InstanceProvider_GlobalTaskContext::preset),
        STAI_PropertyDef::Of("presetName", &InstanceProvider_GlobalTaskContext::presetName),
        STAI_PropertyDef::Of("activePull", &InstanceProvider_GlobalTaskContext::activePull),
        STAI_PropertyDef::Of("activePullName", &InstanceProvider_GlobalTaskContext::activePullName),
        STAI_PropertyDef::Of("blockedBy", &InstanceProvider_GlobalTaskContext::blockedBy),
    };
    return properties;
}

const std::vector<STAI_DelegateDef>& InstanceProvider_GlobalTask::GetDelegates() const
{
    static const std::vector<STAI_DelegateDef> delegates =
    {
        STAI_DelegateDef::Of("onInstanceChanged", &InstanceProvider_GlobalTaskContext::onInstanceChanged),
        STAI_DelegateDef::Of("onStageCleared", &InstanceProvider_GlobalTaskContext::onStageCleared),
        STAI_DelegateDef::Of("onStageBlocked", &InstanceProvider_GlobalTaskContext::onStageBlocked),
        STAI_DelegateDef::Of("onStagePositionFar", &InstanceProvider_GlobalTaskContext::onStagePositionFar),
        STAI_DelegateDef::Of("onPlanChanged", &InstanceProvider_GlobalTaskContext::onPlanChanged),
    };
    return delegates;
}

// ---------------------------------------------------------------- context

void InstanceProvider_GlobalTaskContext::Subscribe()
{
    if (subscribed || !bot)
        return;

    subscribed = true;
    if (InstanceSubsystem* instances = sWorld.GetSubsystem<InstanceSubsystem>())
        instances->AddOnPlayerEnterMap(bot->GetObjectGuid(), this, &InstanceProvider_GlobalTaskContext::OnPlayerEnterMap);
    if (DungeonPlannerSubsystem* planner = sWorld.GetSubsystem<DungeonPlannerSubsystem>())
        planner->AddOnPlanChanged(bot->GetObjectGuid(), this, &InstanceProvider_GlobalTaskContext::OnPlanChanged);
}

void InstanceProvider_GlobalTaskContext::Unsubscribe()
{
    if (!subscribed)
        return;

    subscribed = false;
    if (InstanceSubsystem* instances = sWorld.GetSubsystem<InstanceSubsystem>())
        instances->RemoveAll(bot->GetObjectGuid(), this);
    if (DungeonPlannerSubsystem* planner = sWorld.GetSubsystem<DungeonPlannerSubsystem>())
        planner->RemoveAll(bot->GetObjectGuid(), this);
}

void InstanceProvider_GlobalTaskContext::SetMap(Map* map, bool arrived)
{
    if (!map)
        return;

    const bool changed = !known || map->GetId() != mapId || map->GetInstanceId() != instanceId;
    const bool fire = changed && known;     // the first map the bot is seen on is no change

    known = true;
    mapId = map->GetId();
    instanceId = map->GetInstanceId();
    inDungeon = map->IsDungeon();
    if (changed)
    {
        InstanceSubsystem* instances = sWorld.GetSubsystem<InstanceSubsystem>();
        hasStages = instances && instances->HasStages(mapId);
        stage = -1;
        masterStage = -1;
        masterStageCleared = true;
        pathBlocked = false;
        stagePosition = STAI_Position();
        stagePositionFrom.clear();
        stagePositionFar = false;
        stageTimer = 0;

        // where the bot arrived: its entrance points when it has none yet (UpdateStages, which may query the db).
        // Seen first when the tree starts (login inside the dungeon) only counts in the entrance stage.
        entrancePending = hasStages && map->IsDungeon() &&
            (arrived || instances->FindStage(mapId, bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ()) == 0);
        if (entrancePending)
            entrance = { bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ() };
    }

    if (fire)
        onInstanceChanged.Broadcast();
}

void InstanceProvider_GlobalTaskContext::OnPlayerEnterMap(Player* player, Map* map)
{
    SetMap(map, true);
}

void InstanceProvider_GlobalTaskContext::OnPlanChanged(ObjectGuid self, ObjectGuid selector, uint32 changedMapId)
{
    // from the selector's map thread: the tick takes it over
    planChanged = true;
}

void InstanceProvider_GlobalTaskContext::UpdateStages()
{
    stageTimer = 0;
    InstanceSubsystem* instances = sWorld.GetSubsystem<InstanceSubsystem>();
    if ((!hasStages && !inDungeon) || !instances || !bot->IsInWorld())
        return;

    Map* map = bot->GetMap();
    stage = hasStages ? instances->FindStage(map->GetId(), bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ()) : -1;

    // the entrance is always cleared: the bot's first arrival is both of its points there
    if (entrancePending)
    {
        entrancePending = false;
        PlayerbotDungeonPositions& positions = ai->GetDungeonPositions();
        PlayerbotDungeonPositions::Position existing;
        for (bool cleared : { true, false })
            if (!positions.Get(bot, map->GetId(), 0, 0, cleared, existing))
                positions.Set(bot, map->GetId(), 0, 0, cleared, { entrance.x, entrance.y, entrance.z });
    }

    // blocked: the bot can go neither to a stage point nor after the master
    const bool wasBlocked = pathBlocked || (!masterStageCleared && !stagePosition.valid);
    const bool wasCleared = masterStageCleared;
    const int32 lastMasterStage = masterStage;
    const std::string lastBlockedBy = blockedBy;
    masterStage = -1;
    masterStageCleared = true;
    pathBlocked = false;
    stagePosition = STAI_Position();
    stagePositionFrom.clear();
    stagePositionFar = false;
    preset = 0;
    presetName.clear();
    activePull = 0;
    activePullName.clear();
    blockedBy.clear();

    Player* master = ai->GetMaster();
    const bool hasMaster = master && master != bot && master->IsInWorld() && master->GetMap() == map;

    // the master's Bot Dungeon Planner preset for the dungeon (DungeonPlannerSubsystem) replaces the stages: its active
    // pull gives the bot's point; without one the stages of the map do
    DungeonActivePull pull;
    DungeonPlannerSubsystem* planner = sWorld.GetSubsystem<DungeonPlannerSubsystem>();
    const bool hasPreset = hasMaster && planner && planner->GetActivePull(master, map, pull);
    const bool useStages = hasStages && !hasPreset;
    if (hasMaster && useStages)
    {
        masterStage = instances->FindStage(map->GetId(), master->GetPositionX(), master->GetPositionY(), master->GetPositionZ());
        // outside every stage (corridors between them) the master is not followed either: the bot stays
        masterStageCleared = masterStage >= 0 && instances->IsStageCleared(map, uint32(masterStage));
    }

    // the bot's point in the master's stage, the cleared or the uncleared one by the stage's state: its own override
    // (rtsc, entrance), else the stage's position for it
    const std::vector<InstanceStageDef> stages = useStages ? instances->GetStages(map->GetId()) : std::vector<InstanceStageDef>();
    if (masterStage >= 0)
    {
        // where it comes from, for the movement goal: "<stage> cleared|uncleared, rtsc|entrance|stage file"
        const std::string which = StageName(stages, masterStage) + (masterStageCleared ? " cleared, " : " uncleared, ");

        PlayerbotDungeonPositions::Position position;
        if (ai->GetDungeonPositions().Get(bot, map->GetId(), 0, uint32(masterStage), masterStageCleared, position))
        {
            stagePosition = { position.x, position.y, position.z, true };
            stagePositionFrom = which + (masterStage == 0 ? "entrance / rtsc" : "rtsc");
        }
        else if (instances->GetStagePosition(map->GetId(), uint32(masterStage), masterStageCleared, bot->getClass(), PlayerSpec::GetPlayerSpecTab(bot), position.x, position.y, position.z))
        {
            stagePosition = { position.x, position.y, position.z, true };
            stagePositionFrom = which + "stage file";
        }
    }

    // the active pull of the preset: before its creatures are dead the waypoints of its path, then the waiting
    // position; once dead the cleared position. The bot's rtsc spot for the pull and state wins over the preset's
    // position; without either (or a preset without pulls) the bot follows the master
    if (hasPreset)
    {
        preset = int32(pull.presetId);
        presetName = pull.presetName;
    }
    if (hasPreset && pull.count > 0)
    {
        activePull = int32(pull.index);
        activePullName = pull.name;
        const std::string which = "pull " + std::to_string(pull.index) + " " + pull.name + (pull.cleared ? " cleared, " : ", ");
        PlayerbotDungeonPositions::Position position;
        if (ai->GetDungeonPositions().Get(bot, map->GetId(), pull.presetId, pull.id, pull.cleared, position))
        {
            stagePosition = { position.x, position.y, position.z, true };
            stagePositionFrom = which + "rtsc";
        }
        else if (pull.hasPosition)
        {
            stagePosition = { pull.position.x, pull.position.y, SnapHeight(map, pull.position), true };
            stagePositionFrom = which + (pull.cleared ? "planned" : "waiting");
        }

        // the path: from the waypoint nearest to the bot when the pull became active, each one reached in turn
        PlayerbotAI::PlanPathProgress& progress = ai->GetPlanPathProgress();
        if (!pull.path.empty() && stagePosition.valid)
        {
            if (!progress.valid || progress.pullId != pull.id || progress.mapId != map->GetId() || progress.instanceId != map->GetInstanceId())
            {
                uint32 nearest = 0;
                for (uint32 i = 1; i < pull.path.size(); ++i)
                    if (bot->GetDistance(pull.path[i].x, pull.path[i].y, pull.path[i].z) < bot->GetDistance(pull.path[nearest].x, pull.path[nearest].y, pull.path[nearest].z))
                        nearest = i;
                progress = { map->GetId(), map->GetInstanceId(), pull.id, nearest, true };
            }
            while (progress.index < pull.path.size())
            {
                const DungeonPlanPoint& point = pull.path[progress.index];
                if (bot->GetDistance(point.x, point.y, SnapHeight(map, point)) > PATH_WAYPOINT_RADIUS)
                    break;
                ++progress.index;
            }
            if (progress.index < pull.path.size())
            {
                const DungeonPlanPoint& point = pull.path[progress.index];
                stagePosition = { point.x, point.y, SnapHeight(map, point), true };
                stagePositionFrom = which + "path " + std::to_string(progress.index + 1) + "/" + std::to_string(pull.path.size());
            }
        }
        else if (pull.cleared)
            progress.valid = false;
    }

    // the way back: a bot that got into a stage that is not cleared other than the master's (knocked back, feared,
    // after a unit) leaves it where it last stood outside such a stage, the way it came, instead of crossing it to its
    // point; outside one that point is kept every check
    PlayerbotAI::StageWayBack& wayBack = ai->GetStageWayBack();
    if (wayBack.mapId != map->GetId() || wayBack.instanceId != map->GetInstanceId())
        wayBack.valid = false;
    if (!useStages || stage < 0 || stage == masterStage || instances->IsStageCleared(map, uint32(stage)))
        wayBack = { map->GetId(), map->GetInstanceId(), bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), true };
    else if (wayBack.valid)
    {
        stagePosition = { wayBack.x, wayBack.y, wayBack.z, true };
        stagePositionFrom = StageName(stages, stage) + " uncleared, way back";
    }

    // the way to the stage position (an uncleared point lies in the master's stage, which does not block it), else
    // to the master while it is out of follow range: not through a stage that is not cleared, nor within aggro range
    // of a creature of the dungeon's packs (but one at the chosen spot itself)
    bool check = false;
    bool chosen = false;
    float x = 0.0f, y = 0.0f, z = 0.0f;
    int32 targetStage = -1;
    if (stagePosition.valid)
    {
        stagePositionFar = bot->GetDistance(stagePosition.x, stagePosition.y, stagePosition.z) > STAGE_POSITION_RADIUS;
        check = stagePositionFar;
        chosen = true;
        x = stagePosition.x;
        y = stagePosition.y;
        z = stagePosition.z;
        targetStage = masterStage;
    }
    else if (hasMaster && masterStageCleared && sServerFacade.GetDistance2d(bot, master) > ai->GetRange("follow"))
    {
        check = true;
        x = master->GetPositionX();
        y = master->GetPositionY();
        z = master->GetPositionZ();
    }
    if (check)
    {
        const Movement::PointsArray points = PathTo(bot, x, y, z);
        pathBlocked = useStages && CrossesUnclearedStage(instances, map, points, stage, targetStage);
        if (!pathBlocked && inDungeon)
        {
            if (Creature* creature = AggroOnPath(bot, points, chosen))
            {
                pathBlocked = true;
                blockedBy = creature->GetName();
            }
        }
    }

    // a creature newly in the way: the master hears it (not every check)
    if (!blockedBy.empty() && blockedBy != lastBlockedBy && hasMaster)
    {
        const uint32 now = WorldTimer::getMSTime();
        if (!lastBlockedTell || WorldTimer::getMSTimeDiff(lastBlockedTell, now) >= BLOCKED_TELL_INTERVAL)
        {
            lastBlockedTell = now;
            ai->TellPlayer(master, "I wait here: " + blockedBy + " would aggro on my way" + (stagePosition.valid ? " to " + stagePositionFrom : " to you"));
        }
    }

    if (!wasBlocked && (pathBlocked || (!masterStageCleared && !stagePosition.valid)))
        onStageBlocked.Broadcast();
    // the master's room got cleared while the master stayed in it
    else if (masterStageCleared && !wasCleared && masterStage == lastMasterStage)
        onStageCleared.Broadcast();

    if (stagePositionFar && !pathBlocked)
        onStagePositionFar.Broadcast();
}
