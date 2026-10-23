#include "Task_CastTotem.h"

#include "playerbot/PlayerbotAI.h"

using namespace ai;

namespace
{
    // the task gives up waiting for the cast and the GCD after this long
    constexpr uint32 castTimeoutMs = 10000;
}

EActionRunStatus Task_CastTotem::OnEnter(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(Task_CastTotem::OnEnter);
    Task_CastTotemContext& context = ContextAs<Task_CastTotemContext>(baseContext);
    context.spellId = 0;
    StartLog(context);

    // every totem is up (again): the task succeeds right away
    if (context.needs.empty())
        return LogStatus(context, EActionRunStatus::Succeeded);

    // totems are summoned at the caster
    Player* bot = context.bot;
    for (uint32 spellId : context.needs)
    {
        if (!context.ai->CanCastSpell(spellId, bot, 0, true) || !context.ai->CastSpell(spellId, bot))
            continue;

        context.spellId = spellId;
        context.castStartMs = WorldTimer::getMSTime();
        return EActionRunStatus::Running;
    }

    // no missing totem can be cast right now (mana, its totem item, cooldown...)
    return LogStatus(context, EActionRunStatus::Failed);
}

EActionRunStatus Task_CastTotem::OnTick(STAI_TaskContext& baseContext, uint32 elapsed, bool minimal) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(Task_CastTotem::OnTick);
    Task_CastTotemContext& context = ContextAs<Task_CastTotemContext>(baseContext);

    // wait in the node for the cast and the global cooldown instead of casting the next totem right away
    Player* bot = context.bot;
    const SpellEntry* spellInfo = sSpellTemplate.LookupEntry<SpellEntry>(context.spellId);
    const bool busy = bot->IsNonMeleeSpellCasted(false) || (spellInfo && bot->HasGCD(spellInfo));
    if (busy && WorldTimer::getMSTimeDiff(context.castStartMs, WorldTimer::getMSTime()) < castTimeoutMs)
        return LogStatus(context, EActionRunStatus::Running);

    return LogStatus(context, EActionRunStatus::Succeeded);
}

void Task_CastTotem::OnExit(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(Task_CastTotem::OnExit);
    EndLog(baseContext, !baseContext.bot->IsNonMeleeSpellCasted(false));
}

const std::vector<STAI_PropertyDef>& Task_CastTotem::GetProperties() const
{
    static const std::vector<STAI_PropertyDef> properties = { STAI_PropertyDef::Of("needs", &Task_CastTotemContext::needs) };
    return properties;
}

std::vector<uint32> Task_CastTotem::GetDebugSpells(const STAI_TaskContext& baseContext) const
{
    const Task_CastTotemContext& context = static_cast<const Task_CastTotemContext&>(baseContext);
    if (context.spellId)
        return { context.spellId };
    return context.needs;
}
