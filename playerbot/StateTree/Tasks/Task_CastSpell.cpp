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
    struct ParsedSpell
    {
        uint32 spellId = 0;
        std::string spellName;
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

        const std::string name = ToLower(value);
        for (uint32 id = 1; id < sSpellTemplate.GetMaxEntry(); ++id)
            if (const SpellEntry* spellInfo = sSpellTemplate.LookupEntry<SpellEntry>(id))
                if (ToLower(spellInfo->SpellName[0]) == name)
                    return { id, name };

        throw std::runtime_error("spellid=\"" + value + "\": no spell has that name");
    }
}

void Task_CastSpell::Validate(const STAI_TaskConfig& config)
{
    ParseSpell(config);
}

std::unique_ptr<STAI_Task> Task_CastSpell::Create(const STAI_TaskConfig& config)
{
    const ParsedSpell spell = ParseSpell(config);
    return std::make_unique<Task_CastSpell>(spell.spellId, spell.spellName);
}

uint32 Task_CastSpell::ResolveSpell(const Player* bot) const
{
    if (spellName.empty())
        return bot->HasSpell(spellId) ? spellId : 0;

    uint32 best = 0;
    for (const auto& [id, spell] : bot->GetSpellMap())
    {
        if (spell.state == PLAYERSPELL_REMOVED || spell.disabled)
            continue;

        const SpellEntry* spellInfo = sSpellTemplate.LookupEntry<SpellEntry>(id);
        if (!spellInfo || ToLower(spellInfo->SpellName[0]) != spellName)
            continue;

        if (!best || sSpellMgr.GetSpellRank(id) > sSpellMgr.GetSpellRank(best))
            best = id;
    }
    return best;
}

void Task_CastSpell::OnEnter(STAI_TaskContext& baseContext) const
{
    Task_CastSpellContext& context = ContextAs<Task_CastSpellContext>(baseContext);
    context.spellId = 0;
    context.castTarget.Clear();
    context.failed = false;
    context.resultAtStart = false;
    context.result = -1;

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
        return;

    const uint32 spell = ResolveSpell(bot);
    if (!spell)
    {
        context.failed = true;
        return;
    }

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
        return;
    }

    // every living target is out of reach right now (range, sight, mana...)
    context.failed = true;
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
    Task_CastSpellContext& context = ContextAs<Task_CastSpellContext>(baseContext);
    if (context.failed)
        return EActionRunStatus::Failed;
    if (!context.spellId)
        return EActionRunStatus::Succeeded;

    // a cast time: the spell went off when its spell go came, failed with a failure (interrupt, ...)
    Player* bot = context.bot;
    if (context.result < 0)
        context.result = CastResult(context);

    // wait in the node for the cast and the global cooldown instead of casting again right away
    const SpellEntry* spellInfo = sSpellTemplate.LookupEntry<SpellEntry>(context.spellId);
    const bool busy = bot->IsNonMeleeSpellCasted(false) || (spellInfo && bot->HasGCD(spellInfo));
    if (busy && WorldTimer::getMSTimeDiff(context.castStartMs, WorldTimer::getMSTime()) < castTimeoutMs)
        return EActionRunStatus::Running;

    // the cast is over without a spell go: it did not go off
    if (context.result < 0)
        context.result = 0;
    return EActionRunStatus::Succeeded;
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

bool Task_CastSpell::IsSuccessful(const STAI_TaskContext& baseContext, EActionRunStatus status) const
{
    const Task_CastSpellContext& context = ContextAs<Task_CastSpellContext>(baseContext);
    if (context.failed)
        return false;
    if (!context.spellId)
        return status == EActionRunStatus::Succeeded;     // nobody to cast on: nothing went wrong

    // left during the cast: it went off only if its spell go came already (an instant / channeled one did)
    return (context.result < 0 ? CastResult(context) : context.result) == 1;
}
