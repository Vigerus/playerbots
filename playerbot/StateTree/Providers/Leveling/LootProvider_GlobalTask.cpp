#include "LootProvider_GlobalTask.h"

#include "playerbot/PlayerbotAI.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/StateTree/STAI_TaskFactory.h"

#include "Entities/Creature.h"
#include "Grids/CellImpl.h"
#include "Grids/GridNotifiers.h"
#include "Grids/GridNotifiersImpl.h"
#include "Loot/LootMgr.h"

#include <algorithm>
#include <stdexcept>

using namespace ai;

namespace
{
    constexpr uint32 checkIntervalMs = 500;
    constexpr uint32 keepTargetMs = 30 * IN_MILLISECONDS;

    // dead creatures in range of the bot, for a CreatureListSearcher
    class DeadCreatureInRangeCheck
    {
    public:
        DeadCreatureInRangeCheck(WorldObject const* obj, float range) : obj(obj), range(range) {}
        WorldObject const& GetFocusObject() const { return *obj; }
        bool operator()(Creature* creature) { return !creature->IsAlive() && obj->IsWithinDistInMap(creature, range); }

    private:
        WorldObject const* obj;
        float range;
    };
}

float LootProvider_GlobalTask::ParseRange(const STAI_TaskConfig& config)
{
    const std::string text = STAI_TaskFactory::GetSetting(config, "Range").value_or("30");
    char* end = nullptr;
    const float value = std::strtof(text.c_str(), &end);
    if (end == text.c_str() || *end || value <= 0.0f)
        throw std::runtime_error("<Range value=\"" + text + "\">: expected yards greater than 0");
    return value;
}

void LootProvider_GlobalTask::Validate(const STAI_TaskConfig& config)
{
    ParseRange(config);
}

std::unique_ptr<STAI_TaskBase> LootProvider_GlobalTask::Create(const STAI_TaskConfig& config)
{
    return std::make_unique<LootProvider_GlobalTask>(ParseRange(config));
}

void LootProvider_GlobalTask::Update(LootProvider_GlobalTaskContext& context) const
{
    Player* bot = context.bot;
    const ObjectGuid previous = context.lootTarget;
    context.lootTarget = ObjectGuid();
    context.count = 0;
    if (!previous.IsEmpty() && context.targetSinceMs >= keepTargetMs)
    {
        context.ignored.push_back(previous);
        context.targetSinceMs = 0;
    }
    if (!bot->IsInWorld() || !sServerFacade.IsAlive(bot))
        return;

    CreatureList found;
    DeadCreatureInRangeCheck check(bot, range);
    MaNGOS::CreatureListSearcher<DeadCreatureInRangeCheck> searcher(found, check);
    Cell::VisitGridObjects(bot, searcher, range);

    Creature* closest = nullptr;
    for (Creature* creature : found)
    {
        if (!creature->m_loot || !creature->m_loot->CanLoot(bot))
            continue;
        if (std::find(context.ignored.begin(), context.ignored.end(), creature->GetObjectGuid()) != context.ignored.end())
            continue;

        ++context.count;
        if (!closest || bot->GetDistance(creature) < bot->GetDistance(closest))
            closest = creature;
    }

    if (closest)
        context.lootTarget = closest->GetObjectGuid();
    if (context.lootTarget != previous)
        context.targetSinceMs = 0;
    // a corpse that is gone is not met again
    if (found.empty())
        context.ignored.clear();
    if (!context.lootTarget.IsEmpty() && context.lootTarget != previous)
        context.onLootable.Broadcast();
}

EActionRunStatus LootProvider_GlobalTask::OnEnterState(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(LootProvider_GlobalTask::OnEnterState);
    LootProvider_GlobalTaskContext& context = ContextAs<LootProvider_GlobalTaskContext>(baseContext);
    context.lootTarget = ObjectGuid();
    context.count = 0;
    context.sinceCheckMs = 0;
    context.targetSinceMs = 0;
    Update(context);
    return EActionRunStatus::Running;
}

EActionRunStatus LootProvider_GlobalTask::OnTick(STAI_TaskContext& baseContext, uint32 elapsed, bool minimal) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(LootProvider_GlobalTask::OnTick);
    LootProvider_GlobalTaskContext& context = ContextAs<LootProvider_GlobalTaskContext>(baseContext);
    context.sinceCheckMs += elapsed;
    if (!context.lootTarget.IsEmpty())
        context.targetSinceMs += elapsed;
    if (context.sinceCheckMs >= checkIntervalMs)
    {
        context.sinceCheckMs = 0;
        Update(context);
    }
    return EActionRunStatus::Running;
}

const std::vector<STAI_PropertyDef>& LootProvider_GlobalTask::GetProperties() const
{
    static const std::vector<STAI_PropertyDef> properties =
    {
        STAI_PropertyDef::Of("lootTarget", &LootProvider_GlobalTaskContext::lootTarget),
        STAI_PropertyDef::Of("count", &LootProvider_GlobalTaskContext::count),
    };
    return properties;
}

const std::vector<STAI_DelegateDef>& LootProvider_GlobalTask::GetDelegates() const
{
    static const std::vector<STAI_DelegateDef> delegates = { STAI_DelegateDef::Of("onLootable", &LootProvider_GlobalTaskContext::onLootable) };
    return delegates;
}
