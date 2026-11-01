#include "Task_CastBuff.h"

#include "playerbot/PlayerbotAI.h"

#include "Maps/Map.h"

#include <algorithm>
#include <sstream>

using namespace ai;

namespace
{
    // the task gives up waiting for the cast and the GCD after this long
    constexpr uint32 castTimeoutMs = 10000;
}

EActionRunStatus Task_CastBuff::OnEnterState(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(Task_CastBuff::OnEnterState);
    Task_CastBuffContext& context = ContextAs<Task_CastBuffContext>(baseContext);
    context.spellId = 0;
    context.target.Clear();
    StartLog(context);

    // nothing needed (any more): the task succeeds right away
    if (context.needs.empty())
        return LogStatus(context, EActionRunStatus::Succeeded);

    Player* bot = context.bot;
    for (const BuffNeed& need : context.needs)
    {
        Unit* target = need.target == bot->GetObjectGuid() ? bot : bot->GetMap()->GetPlayer(need.target);
        if (!target || !context.ai->CanCastSpell(need.spellId, target, 0, true))
            continue;

        if (!context.ai->CastSpell(need.spellId, target))
            continue;

        context.spellId = need.spellId;
        context.target = need.target;
        context.castStartMs = WorldTimer::getMSTime();
        return EActionRunStatus::Running;
    }

    // every missing buff is out of reach right now (range, mana, reagents...)
    return LogStatus(context, EActionRunStatus::Failed);
}

EActionRunStatus Task_CastBuff::OnTick(STAI_TaskContext& baseContext, uint32 elapsed, bool minimal) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(Task_CastBuff::OnTick);
    Task_CastBuffContext& context = ContextAs<Task_CastBuffContext>(baseContext);

    // wait in the node for the cast and the global cooldown instead of casting the next buff right away
    Player* bot = context.bot;
    const SpellEntry* spellInfo = sSpellTemplate.LookupEntry<SpellEntry>(context.spellId);
    const bool busy = bot->IsNonMeleeSpellCasted(false) || (spellInfo && bot->HasGCD(spellInfo));
    if (busy && WorldTimer::getMSTimeDiff(context.castStartMs, WorldTimer::getMSTime()) < castTimeoutMs)
        return LogStatus(context, EActionRunStatus::Running);

    return LogStatus(context, EActionRunStatus::Succeeded);
}

const std::vector<STAI_PropertyDef>& Task_CastBuff::GetProperties() const
{
    static const std::vector<STAI_PropertyDef> properties = { STAI_PropertyDef::Of("needs", &Task_CastBuffContext::needs) };
    return properties;
}

void Task_CastBuff::OnExitState(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(Task_CastBuff::OnExitState);
    // a cast in progress is left alone: a transition to follow stops it by moving if it has to; a run left during
    // the GCD went well (the buff was cast and is not being cast any more)
    EndLog(baseContext, !baseContext.bot->IsNonMeleeSpellCasted(false));
}
