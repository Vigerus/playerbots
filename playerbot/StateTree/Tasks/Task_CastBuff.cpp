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

void Task_CastBuff::OnEnter(STAI_TaskContext& baseContext) const
{
    Task_CastBuffContext& context = ContextAs<Task_CastBuffContext>(baseContext);
    context.spellId = 0;
    context.target.Clear();
    context.failed = false;

    // nothing needed (any more): the task succeeds right away
    if (context.needs.empty())
        return;

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
        return;
    }

    // every missing buff is out of reach right now (range, mana, reagents...)
    context.failed = true;
}

EActionRunStatus Task_CastBuff::OnTick(STAI_TaskContext& baseContext, uint32 elapsed, bool minimal) const
{
    Task_CastBuffContext& context = ContextAs<Task_CastBuffContext>(baseContext);
    if (context.failed)
        return EActionRunStatus::Failed;
    if (!context.spellId)
        return EActionRunStatus::Succeeded;

    // wait in the node for the cast and the global cooldown instead of casting the next buff right away
    Player* bot = context.bot;
    const SpellEntry* spellInfo = sSpellTemplate.LookupEntry<SpellEntry>(context.spellId);
    const bool busy = bot->IsNonMeleeSpellCasted(false) || (spellInfo && bot->HasGCD(spellInfo));
    if (busy && WorldTimer::getMSTimeDiff(context.castStartMs, WorldTimer::getMSTime()) < castTimeoutMs)
        return EActionRunStatus::Running;

    return EActionRunStatus::Succeeded;
}

const std::vector<STAI_PropertyDef>& Task_CastBuff::GetProperties() const
{
    static const std::vector<STAI_PropertyDef> properties = { STAI_PropertyDef::Of("needs", &Task_CastBuffContext::needs) };
    return properties;
}

void Task_CastBuff::OnExit(STAI_TaskContext& baseContext) const
{
    // a cast in progress is left alone: a transition to follow stops it by moving if it has to
}

std::vector<uint32> Task_CastBuff::GetDebugSpells(const STAI_TaskContext& baseContext) const
{
    const Task_CastBuffContext& context = ContextAs<Task_CastBuffContext>(baseContext);
    if (context.spellId)
        return { context.spellId };

    std::vector<uint32> spells;
    for (const BuffNeed& need : context.needs)
        if (std::find(spells.begin(), spells.end(), need.spellId) == spells.end())
            spells.push_back(need.spellId);
    return spells;
}

std::string Task_CastBuff::GetDebugInfo(const STAI_TaskContext& baseContext) const
{
    const Task_CastBuffContext& context = ContextAs<Task_CastBuffContext>(baseContext);
    if (context.failed)
        return "no buff castable";
    if (!context.spellId)
        return "nothing to buff";

    const SpellEntry* spellInfo = sSpellTemplate.LookupEntry<SpellEntry>(context.spellId);
    std::ostringstream out;
    out << "cast " << (spellInfo ? spellInfo->SpellName[0] : "?");
    return out.str();
}
