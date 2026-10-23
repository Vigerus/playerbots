#include "SpecProvider_GlobalTask.h"

#include "playerbot/PlayerSpec.h"

#include "Entities/Player.h"

using namespace ai;

namespace
{
    // how often the talents are checked: they change rarely (learned, reset)
    constexpr uint32 checkIntervalMs = 2000;
}

std::string SpecProvider_GlobalTask::GetSpecName(const Player* player)
{
    // talent tabs in the client's order
    const int tab = PlayerSpec::GetPlayerSpecTab(player);
    switch (player->getClass())
    {
        case CLASS_MAGE:    return tab == 1 ? "fire mage" : "frost mage";                                 // arcane, fire, frost
        case CLASS_PRIEST:  return tab == 0 ? "disc priest" : tab == 1 ? "holy priest" : "shadow priest";  // discipline, holy, shadow
        case CLASS_WARLOCK: return "warlock";
        case CLASS_ROGUE:   return "rogue";
        case CLASS_HUNTER:  return "hunter";
        case CLASS_SHAMAN:  return tab == 2 ? "resto shaman" : "enh shaman";                              // elemental, enhancement, restoration
        case CLASS_DRUID:   return tab == 2 ? "resto druid" : "prot druid";                               // balance, feral, restoration
        case CLASS_PALADIN: return tab == 0 ? "holy paladin" : tab == 1 ? "prot paladin" : "retri paladin"; // holy, protection, retribution
        case CLASS_WARRIOR: return tab == 0 ? "arms warrior" : tab == 1 ? "fury warrior" : "prot warrior";  // arms, fury, protection
        default:            return "";
    }
}

void SpecProvider_GlobalTask::Update(SpecProvider_GlobalTaskContext& context)
{
    std::string specName = GetSpecName(context.bot);
    if (specName == context.specName)
        return;

    context.specName = std::move(specName);
    context.onSpecChanged.Broadcast();
}

EActionRunStatus SpecProvider_GlobalTask::OnEnter(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(SpecProvider_GlobalTask::OnEnter);
    SpecProvider_GlobalTaskContext& context = ContextAs<SpecProvider_GlobalTaskContext>(baseContext);
    context.specName.clear();
    context.sinceCheckMs = 0;
    Update(context);
    return EActionRunStatus::Running;
}

EActionRunStatus SpecProvider_GlobalTask::OnTick(STAI_TaskContext& baseContext, uint32 elapsed, bool minimal) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(SpecProvider_GlobalTask::OnTick);
    SpecProvider_GlobalTaskContext& context = ContextAs<SpecProvider_GlobalTaskContext>(baseContext);
    context.sinceCheckMs += elapsed;
    if (context.sinceCheckMs >= checkIntervalMs)
    {
        context.sinceCheckMs = 0;
        Update(context);
    }
    return EActionRunStatus::Running;
}

const std::vector<STAI_PropertyDef>& SpecProvider_GlobalTask::GetProperties() const
{
    static const std::vector<STAI_PropertyDef> properties = { STAI_PropertyDef::Of("specName", &SpecProvider_GlobalTaskContext::specName) };
    return properties;
}

const std::vector<STAI_DelegateDef>& SpecProvider_GlobalTask::GetDelegates() const
{
    static const std::vector<STAI_DelegateDef> delegates = { STAI_DelegateDef::Of("onSpecChanged", &SpecProvider_GlobalTaskContext::onSpecChanged) };
    return delegates;
}
