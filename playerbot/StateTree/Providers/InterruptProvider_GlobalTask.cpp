#include "InterruptProvider_GlobalTask.h"

#include "playerbot/PlayerbotAI.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"

#include "Maps/Map.h"
#include "Spells/Spell.h"

#include <algorithm>

using namespace ai;

namespace
{
    // how often the casts are checked: most casts take 1.5s or more
    constexpr uint32 checkIntervalMs = 100;
}

void InterruptProvider_GlobalTask::Validate(const STAI_TaskConfig& config)
{
    STAI_ConfiguredSpell::Parse(config);
    STAI_SpellFilter::Parse(config);
}

std::unique_ptr<STAI_Task> InterruptProvider_GlobalTask::Create(const STAI_TaskConfig& config)
{
    return std::make_unique<InterruptProvider_GlobalTask>(STAI_ConfiguredSpell::Parse(config), STAI_SpellFilter::Parse(config));
}

const SpellEntry* InterruptProvider_GlobalTask::GetInterruptibleCast(Unit* unit) const
{
    for (CurrentSpellTypes type : { CURRENT_GENERIC_SPELL, CURRENT_CHANNELED_SPELL })
    {
        const Spell* spell = unit->GetCurrentSpell(type);
        if (spell && spell->IsInterruptible() && filter.Allows(spell->m_spellInfo->Id))
            return spell->m_spellInfo;
    }
    return nullptr;
}

void InterruptProvider_GlobalTask::Update(InterruptProvider_GlobalTaskContext& context) const
{
    context.whoNeeds.clear();
    context.causes.clear();
    Player* bot = context.bot;
    const uint32 spellId = bot->IsInWorld() && sServerFacade.IsAlive(bot) ? spell.Resolve(bot) : 0;

    // the casters the interrupt reaches now, healing casts first
    std::vector<ObjectGuid> heals;
    std::vector<ObjectGuid> others;
    uint32 firstSpell = 0;
    if (spellId)
    {
        for (const auto& [guid, victims] : context.ai->GetTeamAttackers())
        {
            Unit* unit = context.ai->GetUnit(guid);
            if (!unit || !unit->IsInWorld() || !sServerFacade.IsAlive(unit) || !sServerFacade.IsHostileTo(bot, unit))
                continue;

            const SpellEntry* cast = GetInterruptibleCast(unit);
            if (!cast || !context.ai->CanCastSpell(spellId, unit, 0, true))
                continue;

            if (PlayerbotAI::IsHealSpell(cast))
                heals.push_back(guid);
            else
                others.push_back(guid);
            context.causes.push_back({ guid, cast->Id, 0 });
        }
        context.whoNeeds = heals;
        context.whoNeeds.insert(context.whoNeeds.end(), others.begin(), others.end());
    }

    if (context.whoNeeds.empty())
    {
        context.announcedCaster.Clear();
        context.announcedSpell = 0;
        return;
    }

    // a new caster first, or the same one casting something else
    Unit* first = context.ai->GetUnit(context.whoNeeds.front());
    const SpellEntry* cast = first ? GetInterruptibleCast(first) : nullptr;
    firstSpell = cast ? cast->Id : 0;
    if (context.whoNeeds.front() != context.announcedCaster || firstSpell != context.announcedSpell)
    {
        context.announcedCaster = context.whoNeeds.front();
        context.announcedSpell = firstSpell;
        context.onInterruptNeeded.Broadcast();
    }
}

EActionRunStatus InterruptProvider_GlobalTask::OnEnter(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(InterruptProvider_GlobalTask::OnEnter);
    InterruptProvider_GlobalTaskContext& context = ContextAs<InterruptProvider_GlobalTaskContext>(baseContext);
    context.whoNeeds.clear();
    context.causes.clear();
    context.sinceCheckMs = 0;
    context.announcedCaster.Clear();
    context.announcedSpell = 0;
    Update(context);
    return EActionRunStatus::Running;
}

EActionRunStatus InterruptProvider_GlobalTask::OnTick(STAI_TaskContext& baseContext, uint32 elapsed, bool minimal) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(InterruptProvider_GlobalTask::OnTick);
    InterruptProvider_GlobalTaskContext& context = ContextAs<InterruptProvider_GlobalTaskContext>(baseContext);
    context.sinceCheckMs += elapsed;
    if (context.sinceCheckMs >= checkIntervalMs)
    {
        context.sinceCheckMs = 0;
        Update(context);
    }
    return EActionRunStatus::Running;
}

const std::vector<STAI_PropertyDef>& InterruptProvider_GlobalTask::GetProperties() const
{
    static const std::vector<STAI_PropertyDef> properties =
    {
        STAI_PropertyDef::Of("whoNeeds", &InterruptProvider_GlobalTaskContext::whoNeeds),
        STAI_PropertyDef::Of("causes", &InterruptProvider_GlobalTaskContext::causes),
    };
    return properties;
}

const std::vector<STAI_DelegateDef>& InterruptProvider_GlobalTask::GetDelegates() const
{
    static const std::vector<STAI_DelegateDef> delegates = { STAI_DelegateDef::Of("onInterruptNeeded", &InterruptProvider_GlobalTaskContext::onInterruptNeeded) };
    return delegates;
}
