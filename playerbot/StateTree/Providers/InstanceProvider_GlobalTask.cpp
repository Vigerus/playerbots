#include "InstanceProvider_GlobalTask.h"

#include "playerbot/PlayerSpec.h"
#include "playerbot/PlayerbotAI.h"
#include "playerbot/ServerFacade.h"

#include "Maps/InstanceSubsystem.h"
#include "Maps/Map.h"
#include "MotionGenerators/PathFinder.h"
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

    // the way of the bot to the spot crosses a stage that is not cleared, other than the stage the bot is in and
    // targetStage (the master's stage when the spot is its uncleared point)
    bool CrossesUnclearedStage(InstanceSubsystem* instances, Player* bot, int32 botStage, int32 targetStage, float x, float y, float z)
    {
        PathFinder path(bot);
        path.calculate(x, y, z);
        const Movement::PointsArray& points = path.getPath();
        if (points.size() < 2)
            return false;

        Map* map = bot->GetMap();
        std::map<int32, bool> cleared;
        for (size_t i = 1; i < points.size(); ++i)
        {
            const Vector3& from = points[i - 1];
            const Vector3& to = points[i];
            const float length = (to - from).length();
            const uint32 samples = std::max<uint32>(1, uint32(length / PATH_SAMPLE_STEP));
            for (uint32 j = 1; j <= samples; ++j)
            {
                const Vector3 point = from + (to - from) * (float(j) / samples);
                const int32 stage = instances->FindStage(map->GetId(), point.x, point.y, point.z);
                if (stage < 0 || stage == botStage || stage == targetStage)
                    continue;

                auto itr = cleared.find(stage);
                if (itr == cleared.end())
                    itr = cleared.emplace(stage, instances->IsStageCleared(map, uint32(stage))).first;
                if (!itr->second)
                    return true;
            }
        }
        return false;
    }
}

EActionRunStatus InstanceProvider_GlobalTask::OnEnter(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(InstanceProvider_GlobalTask::OnEnter);
    InstanceProvider_GlobalTaskContext& context = ContextAs<InstanceProvider_GlobalTaskContext>(baseContext);
    context.Subscribe();
    if (context.bot->IsInWorld())
        context.SetMap(context.bot->GetMap(), false);
    context.UpdateStages();
    return EActionRunStatus::Running;
}

void InstanceProvider_GlobalTask::OnExit(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(InstanceProvider_GlobalTask::OnExit);
    // mapId / instanceId stay: a change while the tree does not run fires when it starts again
    ContextAs<InstanceProvider_GlobalTaskContext>(baseContext).Unsubscribe();
}

EActionRunStatus InstanceProvider_GlobalTask::OnTick(STAI_TaskContext& baseContext, uint32 elapsed, bool minimal) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(InstanceProvider_GlobalTask::OnTick);
    InstanceProvider_GlobalTaskContext& context = ContextAs<InstanceProvider_GlobalTaskContext>(baseContext);
    if (!context.hasStages)
        return EActionRunStatus::Running;

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
}

void InstanceProvider_GlobalTaskContext::Unsubscribe()
{
    if (!subscribed)
        return;

    subscribed = false;
    if (InstanceSubsystem* instances = sWorld.GetSubsystem<InstanceSubsystem>())
        instances->RemoveAll(bot->GetObjectGuid(), this);
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

void InstanceProvider_GlobalTaskContext::UpdateStages()
{
    stageTimer = 0;
    InstanceSubsystem* instances = sWorld.GetSubsystem<InstanceSubsystem>();
    if (!hasStages || !instances || !bot->IsInWorld())
        return;

    Map* map = bot->GetMap();
    stage = instances->FindStage(map->GetId(), bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ());

    // the entrance is always cleared: the bot's first arrival is both of its points there
    if (entrancePending)
    {
        entrancePending = false;
        PlayerbotDungeonPositions& positions = ai->GetDungeonPositions();
        PlayerbotDungeonPositions::Position existing;
        for (bool cleared : { true, false })
            if (!positions.Get(bot, map->GetId(), 0, cleared, existing))
                positions.Set(bot, map->GetId(), 0, cleared, { entrance.x, entrance.y, entrance.z });
    }

    // blocked: the bot can go neither to a stage point nor after the master
    const bool wasBlocked = pathBlocked || (!masterStageCleared && !stagePosition.valid);
    const bool wasCleared = masterStageCleared;
    const int32 lastMasterStage = masterStage;
    masterStage = -1;
    masterStageCleared = true;
    pathBlocked = false;
    stagePosition = STAI_Position();
    stagePositionFrom.clear();
    stagePositionFar = false;

    Player* master = ai->GetMaster();
    const bool hasMaster = master && master != bot && master->IsInWorld() && master->GetMap() == map;
    if (hasMaster)
    {
        masterStage = instances->FindStage(map->GetId(), master->GetPositionX(), master->GetPositionY(), master->GetPositionZ());
        // outside every stage (corridors between them) the master is not followed either: the bot stays
        masterStageCleared = masterStage >= 0 && instances->IsStageCleared(map, uint32(masterStage));
    }

    // the bot's point in the master's stage, the cleared or the uncleared one by the stage's state: its own override
    // (rtsc, entrance), else the stage's position for it
    if (masterStage >= 0)
    {
        // where it comes from, for the movement goal: "<stage> cleared|uncleared, rtsc|entrance|stage file"
        const std::vector<InstanceStageDef> stages = instances->GetStages(map->GetId());
        const std::string stageName = uint32(masterStage) < stages.size() && !stages[masterStage].name.empty() ?
            stages[masterStage].name : "stage " + std::to_string(masterStage);
        const std::string which = stageName + (masterStageCleared ? " cleared, " : " uncleared, ");

        PlayerbotDungeonPositions::Position position;
        if (ai->GetDungeonPositions().Get(bot, map->GetId(), uint32(masterStage), masterStageCleared, position))
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

    // the way to the stage position (an uncleared point lies in the master's stage, which does not block it), else
    // to the master while it is out of follow range
    if (stagePosition.valid)
    {
        stagePositionFar = bot->GetDistance(stagePosition.x, stagePosition.y, stagePosition.z) > STAGE_POSITION_RADIUS;
        if (stagePositionFar)
            pathBlocked = CrossesUnclearedStage(instances, bot, stage, masterStage, stagePosition.x, stagePosition.y, stagePosition.z);
    }
    else if (hasMaster && masterStageCleared && sServerFacade.GetDistance2d(bot, master) > ai->GetRange("follow"))
        pathBlocked = CrossesUnclearedStage(instances, bot, stage, -1, master->GetPositionX(), master->GetPositionY(), master->GetPositionZ());

    if (!wasBlocked && (pathBlocked || (!masterStageCleared && !stagePosition.valid)))
        onStageBlocked.Broadcast();
    // the master's room got cleared while the master stayed in it
    else if (masterStageCleared && !wasCleared && masterStage == lastMasterStage)
        onStageCleared.Broadcast();

    if (stagePositionFar && !pathBlocked)
        onStagePositionFar.Broadcast();
}
