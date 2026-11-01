#include "CorpseProvider_GlobalTask.h"

#include "Entities/Corpse.h"
#include "Entities/Player.h"

using namespace ai;

namespace
{
    // how often the corpse is checked
    constexpr uint32 checkIntervalMs = 500;
    // the run ends this far inside the reclaim radius
    constexpr float reclaimMargin = 5.0f;
}

void CorpseProvider_GlobalTask::Update(CorpseProvider_GlobalTaskContext& context)
{
    Player* bot = context.bot;
    const bool wasFar = context.corpseFar;
    context.released = bot->IsInWorld() && !bot->IsAlive() && bot->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST);
    context.corpseLocation = STAI_Location();
    context.corpseFar = false;

    if (Corpse* corpse = context.released ? bot->GetCorpse() : nullptr)
    {
        context.corpseLocation = { corpse->GetMapId(), corpse->GetPositionX(), corpse->GetPositionY(), corpse->GetPositionZ(), true };
        context.corpseFar = corpse->GetMapId() != bot->GetMapId() ||
            !bot->IsWithinDist3d(corpse->GetPositionX(), corpse->GetPositionY(), corpse->GetPositionZ(), CORPSE_RECLAIM_RADIUS - reclaimMargin);
    }

    if (context.corpseFar && !wasFar)
        context.onCorpseFar.Broadcast();
}

EActionRunStatus CorpseProvider_GlobalTask::OnEnterState(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(CorpseProvider_GlobalTask::OnEnterState);
    CorpseProvider_GlobalTaskContext& context = ContextAs<CorpseProvider_GlobalTaskContext>(baseContext);
    context.corpseFar = false;
    context.sinceCheckMs = 0;
    Update(context);
    return EActionRunStatus::Running;
}

EActionRunStatus CorpseProvider_GlobalTask::OnTick(STAI_TaskContext& baseContext, uint32 elapsed, bool minimal) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(CorpseProvider_GlobalTask::OnTick);
    CorpseProvider_GlobalTaskContext& context = ContextAs<CorpseProvider_GlobalTaskContext>(baseContext);
    context.sinceCheckMs += elapsed;
    if (context.sinceCheckMs >= checkIntervalMs)
    {
        context.sinceCheckMs = 0;
        Update(context);
    }
    return EActionRunStatus::Running;
}

const std::vector<STAI_PropertyDef>& CorpseProvider_GlobalTask::GetProperties() const
{
    static const std::vector<STAI_PropertyDef> properties =
    {
        STAI_PropertyDef::Of("released", &CorpseProvider_GlobalTaskContext::released),
        STAI_PropertyDef::Of("corpseLocation", &CorpseProvider_GlobalTaskContext::corpseLocation),
        STAI_PropertyDef::Of("corpseFar", &CorpseProvider_GlobalTaskContext::corpseFar),
    };
    return properties;
}

const std::vector<STAI_DelegateDef>& CorpseProvider_GlobalTask::GetDelegates() const
{
    static const std::vector<STAI_DelegateDef> delegates = { STAI_DelegateDef::Of("onCorpseFar", &CorpseProvider_GlobalTaskContext::onCorpseFar) };
    return delegates;
}
