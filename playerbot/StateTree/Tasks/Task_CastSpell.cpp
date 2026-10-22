#include "Task_CastSpell.h"

#include "playerbot/PlayerbotAI.h"

#include "Maps/Map.h"
#include "Spells/SpellMgr.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>

using namespace ai;

namespace
{
    // the task gives up waiting for the cast and the GCD after this long
    constexpr uint32 castTimeoutMs = 10000;

    std::string ToLower(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        return text;
    }

    bool IsNumber(const std::string& text)
    {
        return !text.empty() && std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isdigit(c); });
    }

    // spellid="..." of the <Task>: a spell id, or the name of a spell (lower case) with the first spell of that name
    // and every spell of that name, highest rank first
    struct ParsedSpell
    {
        uint32 spellId = 0;
        std::string spellName;
        std::vector<uint32> namedSpells;
    };

    ParsedSpell ParseSpell(const STAI_TaskConfig& config)
    {
        const std::string value = config.get<std::string>("<xmlattr>.spellid", "");
        if (value.empty())
            throw std::runtime_error("needs spellid=\"<spell id or name>\"");

        if (IsNumber(value))
        {
            const uint32 spellId = uint32(std::strtoul(value.c_str(), nullptr, 10));
            if (!sSpellTemplate.LookupEntry<SpellEntry>(spellId))
                throw std::runtime_error("spellid=\"" + value + "\": unknown spell");
            return { spellId, "" };
        }

        ParsedSpell spell;
        spell.spellName = ToLower(value);
        for (uint32 id = 1; id < sSpellTemplate.GetMaxEntry(); ++id)
            if (const SpellEntry* spellInfo = sSpellTemplate.LookupEntry<SpellEntry>(id))
                if (ToLower(spellInfo->SpellName[0]) == spell.spellName)
                    spell.namedSpells.push_back(id);

        if (spell.namedSpells.empty())
            throw std::runtime_error("spellid=\"" + value + "\": no spell has that name");

        spell.spellId = spell.namedSpells.front();
        // stable: spells of the same rank keep their id order
        std::stable_sort(spell.namedSpells.begin(), spell.namedSpells.end(), [](uint32 a, uint32 b) { return sSpellMgr.GetSpellRank(a) > sSpellMgr.GetSpellRank(b); });
        return spell;
    }
}

void Task_CastSpell::Validate(const STAI_TaskConfig& config)
{
    ParseSpell(config);
}

std::unique_ptr<STAI_Task> Task_CastSpell::Create(const STAI_TaskConfig& config)
{
    const ParsedSpell spell = ParseSpell(config);
    return std::make_unique<Task_CastSpell>(spell.spellId, spell.spellName, spell.namedSpells);
}

uint32 Task_CastSpell::ResolveSpell(const Player* bot) const
{
    if (spellName.empty())
        return bot->HasSpell(spellId) ? spellId : 0;

    // the highest rank the bot knows (HasSpell: learned, not removed or disabled)
    for (uint32 id : namedSpells)
        if (bot->HasSpell(id))
            return id;
    return 0;
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
    // need nothing any more
    Player* bot = context.bot;
    std::vector<Unit*> targets;
    std::vector<ObjectGuid> guids = context.whoNeeds;
    if (!context.target.IsEmpty())
        guids.insert(guids.begin(), context.target);
    for (ObjectGuid guid : guids)
    {
        Unit* target = guid == bot->GetObjectGuid() ? bot : bot->GetMap()->GetUnit(guid);
        if (target && target->IsInWorld() && target->IsAlive())
            targets.push_back(target);
    }

    // nobody needs it (any more): the task succeeds right away
    if (targets.empty())
        return LogStatus(context, EActionRunStatus::Succeeded);

    const uint32 spell = ResolveSpell(bot);
    if (!spell)
        return LogStatus(context, EActionRunStatus::Failed);

    for (Unit* target : targets)
    {
        if (!context.ai->CanCastSpell(spell, target, 0, true))
            continue;

        // before the cast: an instant spell's spell go arrives during CastSpell
        context.castStartMs = WorldTimer::getMSTime();
        if (!context.ai->CastSpell(spell, target))
            continue;

        context.spellId = spell;
        context.castTarget = target->GetObjectGuid();

        // instant and channeled spells went off when they started; a cast time is decided by the spell go
        const SpellEntry* spellInfo = sSpellTemplate.LookupEntry<SpellEntry>(spell);
        context.resultAtStart = spellInfo && (IsChanneledSpell(spellInfo) || GetSpellCastTime(spellInfo, bot) == 0);
        if (context.resultAtStart)
            context.result = 1;
        return EActionRunStatus::Running;
    }

    // every living target is out of reach right now (range, sight, mana...)
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
