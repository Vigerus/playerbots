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

    // the way of the bot to the spot crosses a stage that is not cleared, other than the stage the bot is in
    bool CrossesUnclearedStage(InstanceSubsystem* instances, Player* bot, int32 botStage, float x, float y, float z)
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
                if (stage < 0 || stage == botStage)
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
        context.SetMap(context.bot->GetMap());
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

void InstanceProvider_GlobalTaskContext::SetMap(Map* map)
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
        stagePositionFar = false;
        stageTimer = 0;
    }

    if (fire)
        onInstanceChanged.Broadcast();
}

void InstanceProvider_GlobalTaskContext::OnPlayerEnterMap(Player* player, Map* map)
{
    SetMap(map);
}

void InstanceProvider_GlobalTaskContext::UpdateStages()
{
    stageTimer = 0;
    InstanceSubsystem* instances = sWorld.GetSubsystem<InstanceSubsystem>();
    if (!hasStages || !instances || !bot->IsInWorld())
        return;

    Map* map = bot->GetMap();
    stage = instances->FindStage(map->GetId(), bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ());

    const bool wasBlocked = !masterStageCleared || pathBlocked;
    const bool wasCleared = masterStageCleared;
    const int32 lastMasterStage = masterStage;
    masterStage = -1;
    masterStageCleared = true;
    pathBlocked = false;
    stagePosition = STAI_Position();
    stagePositionFar = false;

    Player* master = ai->GetMaster();
    const bool hasMaster = master && master != bot && master->IsInWorld() && master->GetMap() == map;
    if (hasMaster)
    {
        masterStage = instances->FindStage(map->GetId(), master->GetPositionX(), master->GetPositionY(), master->GetPositionZ());
        if (masterStage >= 0)
            masterStageCleared = instances->IsStageCleared(map, uint32(masterStage));
    }

    // the bot's spot in the master's (cleared) stage: its own rtsc override, else the stage's position for it
    if (masterStage >= 0 && masterStageCleared)
    {
        PlayerbotDungeonPositions::Position position;
        if (ai->GetDungeonPositions().Get(bot, map->GetId(), uint32(masterStage), position))
            stagePosition = { position.x, position.y, position.z, true };
        else if (instances->GetStagePosition(map->GetId(), uint32(masterStage), bot->getClass(), PlayerSpec::GetPlayerSpecTab(bot), position.x, position.y, position.z))
            stagePosition = { position.x, position.y, position.z, true };
    }

    // the way to the stage position, else to the master while it is out of follow range
    if (stagePosition.valid)
    {
        stagePositionFar = bot->GetDistance(stagePosition.x, stagePosition.y, stagePosition.z) > STAGE_POSITION_RADIUS;
        if (stagePositionFar)
            pathBlocked = CrossesUnclearedStage(instances, bot, stage, stagePosition.x, stagePosition.y, stagePosition.z);
    }
    else if (hasMaster && masterStageCleared && sServerFacade.GetDistance2d(bot, master) > ai->GetRange("follow"))
        pathBlocked = CrossesUnclearedStage(instances, bot, stage, master->GetPositionX(), master->GetPositionY(), master->GetPositionZ());

    if (!wasBlocked && (!masterStageCleared || pathBlocked))
        onStageBlocked.Broadcast();
    // the master's room got cleared while the master stayed in it
    else if (masterStageCleared && !wasCleared && masterStage == lastMasterStage)
        onStageCleared.Broadcast();

    if (stagePositionFar && !pathBlocked)
        onStagePositionFar.Broadcast();
}
