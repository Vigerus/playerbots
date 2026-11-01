#include "TeamProvider_GlobalTask.h"

#include "playerbot/ServerFacade.h"

#include "Entities/Player.h"
#include "Maps/Map.h"

using namespace ai;

ObjectGuid TeamProvider_GlobalTask::FindMainTank(Player* bot, const STAI_TeamRoles& roles)
{
    if (!bot->IsInWorld())
        return ObjectGuid();

    for (ObjectGuid guid : roles.WithRole(BOT_ROLE_TANK))
    {
        if (guid == bot->GetObjectGuid())
            continue;
        Player* tank = bot->GetMap()->GetPlayer(guid);
        if (tank && tank->IsInWorld() && sServerFacade.IsAlive(tank))
            return guid;
    }
    return ObjectGuid();
}

void TeamProvider_GlobalTask::Publish(TeamProvider_GlobalTaskContext& context)
{
    const ObjectGuid self = context.bot->GetObjectGuid();
    context.tanks = context.roles.WithRole(BOT_ROLE_TANK);
    context.healers = context.roles.WithRole(BOT_ROLE_HEALER);
    context.damage = context.roles.WithRole(BOT_ROLE_DPS);
    const STAI_MemberRole* own = context.roles.Find(self);
    context.specName = own ? own->specName : "";
    context.isTank = context.roles.HasRole(self, BOT_ROLE_TANK);
    context.isHealer = context.roles.HasRole(self, BOT_ROLE_HEALER);
}

EActionRunStatus TeamProvider_GlobalTask::OnEnterState(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(TeamProvider_GlobalTask::OnEnterState);
    TeamProvider_GlobalTaskContext& context = ContextAs<TeamProvider_GlobalTaskContext>(baseContext);
    context.roles.Subscribe(context.bot);
    context.roles.Update(context.bot, 0);
    Publish(context);
    context.mainTank = FindMainTank(context.bot, context.roles);
    context.onRolesChanged.Broadcast();
    return EActionRunStatus::Running;
}

EActionRunStatus TeamProvider_GlobalTask::OnTick(STAI_TaskContext& baseContext, uint32 elapsed, bool minimal) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(TeamProvider_GlobalTask::OnTick);
    TeamProvider_GlobalTaskContext& context = ContextAs<TeamProvider_GlobalTaskContext>(baseContext);
    bool changed = context.roles.Update(context.bot, elapsed);
    if (changed)
        Publish(context);

    // the main tank also changes when it dies or is resurrected
    const ObjectGuid mainTank = FindMainTank(context.bot, context.roles);
    if (mainTank != context.mainTank)
    {
        context.mainTank = mainTank;
        changed = true;
    }

    if (changed)
        context.onRolesChanged.Broadcast();
    return EActionRunStatus::Running;
}

void TeamProvider_GlobalTask::OnExitState(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(TeamProvider_GlobalTask::OnExitState);
    TeamProvider_GlobalTaskContext& context = ContextAs<TeamProvider_GlobalTaskContext>(baseContext);
    context.roles.Unsubscribe();
}

const std::vector<STAI_PropertyDef>& TeamProvider_GlobalTask::GetProperties() const
{
    static const std::vector<STAI_PropertyDef> properties =
    {
        STAI_PropertyDef::Of("tanks", &TeamProvider_GlobalTaskContext::tanks),
        STAI_PropertyDef::Of("healers", &TeamProvider_GlobalTaskContext::healers),
        STAI_PropertyDef::Of("damage", &TeamProvider_GlobalTaskContext::damage),
        STAI_PropertyDef::Of("mainTank", &TeamProvider_GlobalTaskContext::mainTank),
        STAI_PropertyDef::Of("specName", &TeamProvider_GlobalTaskContext::specName),
        STAI_PropertyDef::Of("isTank", &TeamProvider_GlobalTaskContext::isTank),
        STAI_PropertyDef::Of("isHealer", &TeamProvider_GlobalTaskContext::isHealer),
    };
    return properties;
}

const std::vector<STAI_DelegateDef>& TeamProvider_GlobalTask::GetDelegates() const
{
    static const std::vector<STAI_DelegateDef> delegates = { STAI_DelegateDef::Of("onRolesChanged", &TeamProvider_GlobalTaskContext::onRolesChanged) };
    return delegates;
}
