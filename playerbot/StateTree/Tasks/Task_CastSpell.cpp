#include "Task_CastSpell.h"

#include "playerbot/PlayerbotAI.h"

#include "Entities/Corpse.h"
#include "Maps/Map.h"
#include "Spells/SpellAuras.h"
#include "Spells/SpellMgr.h"

#include <optional>
#include <stdexcept>

using namespace ai;

namespace
{
    // the task gives up waiting for the cast and the GCD after this long
    constexpr uint32 castTimeoutMs = 10000;

    // <Target value="self|dead"/>: the bot casts on itself / only on dead targets
    std::string ParseTarget(const STAI_TaskConfig& config)
    {
        const std::optional<std::string> target = STAI_TaskFactory::GetSetting(config, "Target");
        if (!target)
            return "";
        if (*target != "self" && *target != "dead")
            throw std::runtime_error("<Target value=\"" + *target + "\">: expected \"self\" or \"dead\"");
        return *target;
    }

    // an aura of the spell chain on the target (from caster only, unless it is empty)
    bool HasAuraOfChain(const Unit* target, uint32 firstSpellId, ObjectGuid caster)
    {
        for (const auto& [spellId, holder] : target->GetSpellAuraHolderMap())
            if (!holder->IsDeleted() && sSpellMgr.GetFirstSpellInChain(holder->GetId()) == firstSpellId &&
                (caster.IsEmpty() || holder->GetCasterGuid() == caster))
                return true;
        return false;
    }
}

std::vector<Task_CastSpell::SkipAura> Task_CastSpell::ParseSkipAuras(const STAI_TaskConfig& config)
{
    std::vector<SkipAura> skipAuras;
    for (const auto& [key, child] : config)
    {
        if (key != "SkipAura")
            continue;

        SkipAura skip;
        skip.own = child.get<std::string>("<xmlattr>.own", "false") == "true";
        const std::string idText = child.get<std::string>("<xmlattr>.spellid", "");
        if (!idText.empty())
        {
            const uint32 spellId = uint32(std::strtoul(idText.c_str(), nullptr, 10));
            if (!spellId || !sSpellTemplate.LookupEntry<SpellEntry>(spellId))
                throw std::runtime_error("<SkipAura spellid=\"" + idText + "\">: unknown spell");
            skip.firstSpellId = sSpellMgr.GetFirstSpellInChain(spellId);
        }
        else if (!skip.own)
            throw std::runtime_error("<SkipAura>: needs spellid=\"...\" or own=\"true\"");
        skipAuras.push_back(skip);
    }
    return skipAuras;
}

std::vector<STAI_ConfiguredSpell> Task_CastSpell::ParseFallbacks(const STAI_TaskConfig& config)
{
    std::vector<STAI_ConfiguredSpell> fallbacks;
    for (const auto& [key, child] : config)
        if (key == "Fallback")
            fallbacks.push_back(STAI_ConfiguredSpell::Parse(child));
    return fallbacks;
}

uint32 Task_CastSpell::ResolveSpell(const Player* bot) const
{
    if (const uint32 spellId = spell.Resolve(bot))
        return spellId;
    for (const STAI_ConfiguredSpell& fallback : fallbacks)
        if (const uint32 spellId = fallback.Resolve(bot))
            return spellId;
    return 0;
}

bool Task_CastSpell::IsSkipped(const Player* bot, const Unit* target, uint32 spellId) const
{
    for (const SkipAura& skip : skipAuras)
    {
        const uint32 firstSpellId = skip.firstSpellId ? skip.firstSpellId : sSpellMgr.GetFirstSpellInChain(spellId);
        if (HasAuraOfChain(target, firstSpellId, skip.own ? bot->GetObjectGuid() : ObjectGuid()))
            return true;
    }
    return false;
}

void Task_CastSpell::Validate(const STAI_TaskConfig& config)
{
    STAI_ConfiguredSpell::Parse(config);
    ParseTarget(config);
    ParseSkipAuras(config);
    ParseFallbacks(config);
}

std::unique_ptr<STAI_Task> Task_CastSpell::Create(const STAI_TaskConfig& config)
{
    const std::string target = ParseTarget(config);
    return std::make_unique<Task_CastSpell>(STAI_ConfiguredSpell::Parse(config), target == "self", target == "dead", ParseSkipAuras(config),
        ParseFallbacks(config));
}

EActionRunStatus Task_CastSpell::OnEnter(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(Task_CastSpell::OnEnter);
    Task_CastSpellContext& context = ContextAs<Task_CastSpellContext>(baseContext);
    context.spellId = 0;
    context.castTarget.Clear();
    context.resultAtStart = false;
    context.result = -1;
    StartLog(context);

    // the target first, then whoNeeds: members to help, or units to act on (attack, crowd control); the dead ones
    // need nothing any more (only they do for <Target value="dead"/>)
    Player* bot = context.bot;
    const uint32 spellId = ResolveSpell(bot);
    std::vector<Unit*> targets;
    std::vector<Corpse*> corpses;   // <Target value="dead"/>: the corpses of released members
    std::vector<ObjectGuid> guids = context.whoNeeds;
    if (!context.target.IsEmpty())
        guids.insert(guids.begin(), context.target);
    if (self)
        guids.insert(guids.begin(), bot->GetObjectGuid());
    bool skipped = false;
    for (ObjectGuid guid : guids)
    {
        if (guid.IsCorpse())
        {
            if (Corpse* corpse = dead ? bot->GetMap()->GetCorpse(guid) : nullptr)
                if (corpse->IsInWorld())
                    corpses.push_back(corpse);
            continue;
        }

        Unit* target = guid == bot->GetObjectGuid() ? bot : bot->GetMap()->GetUnit(guid);
        if (!target || !target->IsInWorld() || target->IsAlive() == dead)
            continue;
        // already has the aura (Weakened Soul, the bot's own Renew): needs nothing from this spell
        if (spellId && IsSkipped(bot, target, spellId))
        {
            skipped = true;
            continue;
        }
        targets.push_back(target);
    }

    // everybody left has the aura already: failed, so the node is not selected again right away (its condition
    // still passes) and the nodes below it get their turn
    if (targets.empty() && corpses.empty() && skipped)
        return LogStatus(context, EActionRunStatus::Failed);

    // nobody needs it (any more): the task succeeds right away
    if (targets.empty() && corpses.empty())
        return LogStatus(context, EActionRunStatus::Succeeded);

    if (!spellId)
        return LogStatus(context, EActionRunStatus::Failed);

    for (Unit* target : targets)
    {
        if (!context.ai->CanCastSpell(spellId, target, 0, true))
            continue;

        // before the cast: an instant spell's spell go arrives during CastSpell
        context.castStartMs = WorldTimer::getMSTime();
        if (!context.ai->CastSpell(spellId, target))
            continue;

        context.spellId = spellId;
        context.castTarget = target->GetObjectGuid();

        // instant and channeled spells went off when they started; a cast time is decided by the spell go
        const SpellEntry* spellInfo = sSpellTemplate.LookupEntry<SpellEntry>(spellId);
        context.resultAtStart = spellInfo && (IsChanneledSpell(spellInfo) || GetSpellCastTime(spellInfo, bot) == 0);
        if (context.resultAtStart)
            context.result = 1;
        return EActionRunStatus::Running;
    }

    // the corpses of released members: the resurrection reaches the ghost wherever it is
    // (range and sight are the spell's own cast checks)
    for (Corpse* corpse : corpses)
    {
        context.castStartMs = WorldTimer::getMSTime();
        if (!context.ai->CastSpellOnCorpse(spellId, corpse))
            continue;

        context.spellId = spellId;
        context.castTarget = corpse->GetObjectGuid();
        const SpellEntry* spellInfo = sSpellTemplate.LookupEntry<SpellEntry>(spellId);
        context.resultAtStart = spellInfo && (IsChanneledSpell(spellInfo) || GetSpellCastTime(spellInfo, bot) == 0);
        if (context.resultAtStart)
            context.result = 1;
        return EActionRunStatus::Running;
    }

    // every target is out of reach right now (range, sight, mana...)
    return LogStatus(context, EActionRunStatus::Failed);
}

int8 Task_CastSpell::CastResult(const Task_CastSpellContext& context)
{
    auto since = [&](const PlayerbotAI::SpellCastEvent& event)
    {
        return event.spellId == context.spellId && WorldTimer::getMSTimeDiff(context.castStartMs, event.ms) < castTimeoutMs;
    };
    if (since(context.ai->GetLastSpellFailure()))
        return 0;
    if (since(context.ai->GetLastSpellGo()))
        return 1;
    return -1;
}

EActionRunStatus Task_CastSpell::OnTick(STAI_TaskContext& baseContext, uint32 elapsed, bool minimal) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(Task_CastSpell::OnTick);
    Task_CastSpellContext& context = ContextAs<Task_CastSpellContext>(baseContext);

    // a cast time: the spell went off when its spell go came, failed with a failure (interrupt, ...)
    Player* bot = context.bot;
    if (context.result < 0)
        context.result = CastResult(context);

    // wait in the node for the cast and the global cooldown instead of casting again right away
    const SpellEntry* spellInfo = sSpellTemplate.LookupEntry<SpellEntry>(context.spellId);
    const bool busy = bot->IsNonMeleeSpellCasted(false) || (spellInfo && bot->HasGCD(spellInfo));
    if (busy && WorldTimer::getMSTimeDiff(context.castStartMs, WorldTimer::getMSTime()) < castTimeoutMs)
        return LogStatus(context, EActionRunStatus::Running);

    // the cast is over without a spell go: it did not go off
    if (context.result < 0)
        context.result = 0;
    // the History entry tells whether the spell went off
    EndLog(context, context.result == 1);
    return EActionRunStatus::Succeeded;
}

void Task_CastSpell::OnExit(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(Task_CastSpell::OnExit);
    // left during the cast: it went off only if its spell go came already (an instant / channeled one did)
    Task_CastSpellContext& context = ContextAs<Task_CastSpellContext>(baseContext);
    EndLog(context, (context.result < 0 ? CastResult(context) : context.result) == 1);
}

const std::vector<STAI_PropertyDef>& Task_CastSpell::GetProperties() const
{
    static const std::vector<STAI_PropertyDef> properties =
    {
        STAI_PropertyDef::Of("target", &Task_CastSpellContext::target),
        STAI_PropertyDef::Of("whoNeeds", &Task_CastSpellContext::whoNeeds),
    };
    return properties;
}
