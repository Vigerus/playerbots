#include "VendorProvider_GlobalTask.h"

#include "playerbot/PlayerbotAI.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/StateTree/STAI_TaskFactory.h"
#include "playerbot/StateTree/Providers/STAI_SpawnIndex.h"

#include "Entities/Creature.h"
#include "Globals/ObjectMgr.h"
#include "Grids/CellImpl.h"
#include "Grids/GridNotifiers.h"
#include "Grids/GridNotifiersImpl.h"

#include <stdexcept>

using namespace ai;

namespace
{
    constexpr uint32 checkIntervalMs = 1000;
    constexpr uint32 chooseIntervalMs = 30000;
    constexpr float nearVendor = 50.0f;     // yards: the vendor creature is looked up (for its guid)
}

float VendorProvider_GlobalTask::ParseRange(const STAI_TaskConfig& config)
{
    const std::string text = STAI_TaskFactory::GetSetting(config, "Range").value_or("2000");
    char* end = nullptr;
    const float value = std::strtof(text.c_str(), &end);
    if (end == text.c_str() || *end || value <= 0.0f)
        throw std::runtime_error("<Range value=\"" + text + "\">: expected yards greater than 0");
    return value;
}

void VendorProvider_GlobalTask::Validate(const STAI_TaskConfig& config)
{
    ParseRange(config);
}

std::unique_ptr<STAI_TaskBase> VendorProvider_GlobalTask::Create(const STAI_TaskConfig& config)
{
    return std::make_unique<VendorProvider_GlobalTask>(ParseRange(config));
}

void VendorProvider_GlobalTask::ChooseVendor(VendorProvider_GlobalTaskContext& context) const
{
    Player* bot = context.bot;
    context.vendorEntry = 0;
    context.vendorLocation = STAI_Location();
    const FactionTemplateEntry* botFaction = bot->GetFactionTemplateEntry();
    float best = range;
    for (const STAI_Spawn& spawn : STAI_GetSpawns(bot->GetMapId()))
    {
        const float distance = bot->GetDistance2d(spawn.x, spawn.y);
        if (distance >= best)
            continue;

        const CreatureInfo* info = ObjectMgr::GetCreatureTemplate(spawn.entry);
        if (!info || !(info->NpcFlags & UNIT_NPC_FLAG_VENDOR))
            continue;
        const FactionTemplateEntry* faction = sFactionTemplateStore.LookupEntry(info->Faction);
        if (!faction || !botFaction || faction->IsHostileTo(*botFaction))
            continue;

        best = distance;
        context.vendorEntry = spawn.entry;
        context.vendorLocation = { bot->GetMapId(), spawn.x, spawn.y, spawn.z, true };
    }
}

void VendorProvider_GlobalTask::Update(VendorProvider_GlobalTaskContext& context) const
{
    Player* bot = context.bot;
    context.vendor = ObjectGuid();
    if (!bot->IsInWorld())
        return;

    context.vendorDistance = context.vendorLocation.IsEmpty() ? 0 : uint32(bot->GetDistance2d(context.vendorLocation.x, context.vendorLocation.y));
    if (!context.vendorEntry || context.vendorDistance > nearVendor)
        return;

    CreatureList found;
    MaNGOS::AllCreaturesOfEntryInRangeCheck check(bot, context.vendorEntry, nearVendor);
    MaNGOS::CreatureListSearcher<MaNGOS::AllCreaturesOfEntryInRangeCheck> searcher(found, check);
    Cell::VisitGridObjects(bot, searcher, nearVendor);
    for (Creature* creature : found)
        if (sServerFacade.IsAlive(creature))
            context.vendor = creature->GetObjectGuid();
}

EActionRunStatus VendorProvider_GlobalTask::OnEnterState(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(VendorProvider_GlobalTask::OnEnterState);
    VendorProvider_GlobalTaskContext& context = ContextAs<VendorProvider_GlobalTaskContext>(baseContext);
    context.sinceCheckMs = 0;
    context.sinceChooseMs = 0;
    if (context.bot->IsInWorld())
        ChooseVendor(context);
    Update(context);
    return EActionRunStatus::Running;
}

EActionRunStatus VendorProvider_GlobalTask::OnTick(STAI_TaskContext& baseContext, uint32 elapsed, bool minimal) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(VendorProvider_GlobalTask::OnTick);
    VendorProvider_GlobalTaskContext& context = ContextAs<VendorProvider_GlobalTaskContext>(baseContext);
    context.sinceCheckMs += elapsed;
    context.sinceChooseMs += elapsed;
    // the bot walks on: another vendor may be nearer now (not while one is at hand)
    if (context.sinceChooseMs >= chooseIntervalMs)
    {
        context.sinceChooseMs = 0;
        if (context.bot->IsInWorld() && context.vendor.IsEmpty())
            ChooseVendor(context);
    }
    if (context.sinceCheckMs >= checkIntervalMs)
    {
        context.sinceCheckMs = 0;
        Update(context);
    }
    return EActionRunStatus::Running;
}

const std::vector<STAI_PropertyDef>& VendorProvider_GlobalTask::GetProperties() const
{
    static const std::vector<STAI_PropertyDef> properties =
    {
        STAI_PropertyDef::Of("vendorLocation", &VendorProvider_GlobalTaskContext::vendorLocation),
        STAI_PropertyDef::Of("vendorDistance", &VendorProvider_GlobalTaskContext::vendorDistance),
        STAI_PropertyDef::Of("vendor", &VendorProvider_GlobalTaskContext::vendor),
    };
    return properties;
}
