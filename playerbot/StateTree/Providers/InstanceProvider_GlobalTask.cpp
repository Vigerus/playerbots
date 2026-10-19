#include "InstanceProvider_GlobalTask.h"

#include "playerbot/PlayerbotAI.h"

#include "Maps/InstanceSubsystem.h"
#include "Maps/Map.h"
#include "World/World.h"

#include <sstream>

using namespace ai;

void InstanceProvider_GlobalTask::OnEnter(STAI_TaskContext& baseContext) const
{
    InstanceProvider_GlobalTaskContext& context = ContextAs<InstanceProvider_GlobalTaskContext>(baseContext);
    context.Subscribe();
    if (context.bot->IsInWorld())
        context.SetMap(context.bot->GetMap());
}

void InstanceProvider_GlobalTask::OnExit(STAI_TaskContext& baseContext) const
{
    // mapId / instanceId stay: a change while the tree does not run fires when it starts again
    ContextAs<InstanceProvider_GlobalTaskContext>(baseContext).Unsubscribe();
}

const std::vector<STAI_PropertyDef>& InstanceProvider_GlobalTask::GetProperties() const
{
    static const std::vector<STAI_PropertyDef> properties =
    {
        STAI_PropertyDef::Of("mapId", &InstanceProvider_GlobalTaskContext::mapId),
        STAI_PropertyDef::Of("instanceId", &InstanceProvider_GlobalTaskContext::instanceId),
        STAI_PropertyDef::Of("inDungeon", &InstanceProvider_GlobalTaskContext::inDungeon),
    };
    return properties;
}

const std::vector<STAI_DelegateDef>& InstanceProvider_GlobalTask::GetDelegates() const
{
    static const std::vector<STAI_DelegateDef> delegates = { STAI_DelegateDef::Of("onInstanceChanged", &InstanceProvider_GlobalTaskContext::onInstanceChanged) };
    return delegates;
}

std::string InstanceProvider_GlobalTask::GetDebugInfo(const STAI_TaskContext& baseContext) const
{
    const InstanceProvider_GlobalTaskContext& context = ContextAs<InstanceProvider_GlobalTaskContext>(baseContext);
    if (!context.known)
        return "no map";

    std::ostringstream out;
    out << "map " << context.mapId << " instance " << context.instanceId << (context.inDungeon ? " (dungeon)" : "");
    return out.str();
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

    if (fire)
        onInstanceChanged.Broadcast();
}

void InstanceProvider_GlobalTaskContext::OnPlayerEnterMap(Player* player, Map* map)
{
    SetMap(map);
}
