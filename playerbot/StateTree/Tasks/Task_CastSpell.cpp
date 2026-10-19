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

    // spellid="..." of the <Task>: a spell id, or the name of a spell (lower case); and a spell of that name
    struct ParsedSpell
    {
        uint32 spellId = 0;
        std::string spellName;
        uint32 displaySpellId = 0;
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
            return { spellId, "", spellId };
        }

        const std::string name = ToLower(value);
        for (uint32 id = 1; id < sSpellTemplate.GetMaxEntry(); ++id)
            if (const SpellEntry* spellInfo = sSpellTemplate.LookupEntry<SpellEntry>(id))
                if (ToLower(spellInfo->SpellName[0]) == name)
                    return { 0, name, id };

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
    return std::make_unique<Task_CastSpell>(spell.spellId, spell.spellName, spell.displaySpellId);
}

uint32 Task_CastSpell::ResolveSpell(const Player* bot) const
{
    if (spellId)
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
    context.target.Clear();
    context.failed = false;

    // nobody needs it (any more): the task succeeds right away
    if (context.whoNeeds.empty())
        return;

    Player* bot = context.bot;
    const uint32 spell = ResolveSpell(bot);
    if (!spell)
    {
        context.failed = true;
        return;
    }

    for (ObjectGuid guid : context.whoNeeds)
    {
        Unit* target = guid == bot->GetObjectGuid() ? bot : bot->GetMap()->GetPlayer(guid);
        if (!target || !context.ai->CanCastSpell(spell, target, 0, true))
            continue;

        if (!context.ai->CastSpell(spell, target))
            continue;

        context.spellId = spell;
        context.target = guid;
        context.castStartMs = WorldTimer::getMSTime();
        return;
    }

    // every target is out of reach right now (range, mana...)
    context.failed = true;
}

EActionRunStatus Task_CastSpell::OnTick(STAI_TaskContext& baseContext, uint32 elapsed, bool minimal) const
{
    Task_CastSpellContext& context = ContextAs<Task_CastSpellContext>(baseContext);
    if (context.failed)
        return EActionRunStatus::Failed;
    if (!context.spellId)
        return EActionRunStatus::Succeeded;

    // wait in the node for the cast and the global cooldown instead of casting again right away
    Player* bot = context.bot;
    const SpellEntry* spellInfo = sSpellTemplate.LookupEntry<SpellEntry>(context.spellId);
    const bool busy = bot->IsNonMeleeSpellCasted(false) || (spellInfo && bot->HasGCD(spellInfo));
    if (busy && WorldTimer::getMSTimeDiff(context.castStartMs, WorldTimer::getMSTime()) < castTimeoutMs)
        return EActionRunStatus::Running;

    return EActionRunStatus::Succeeded;
}

const std::vector<STAI_PropertyDef>& Task_CastSpell::GetProperties() const
{
    static const std::vector<STAI_PropertyDef> properties = { STAI_PropertyDef::Of("whoNeeds", &Task_CastSpellContext::whoNeeds) };
    return properties;
}

std::vector<uint32> Task_CastSpell::GetDebugSpells(const STAI_TaskContext& baseContext) const
{
    const Task_CastSpellContext& context = ContextAs<Task_CastSpellContext>(baseContext);
    if (context.spellId)
        return { context.spellId };
    if (const uint32 known = context.bot ? ResolveSpell(context.bot) : 0)
        return { known };
    return { displaySpellId };
}

std::string Task_CastSpell::GetDebugInfo(const STAI_TaskContext& baseContext) const
{
    const Task_CastSpellContext& context = ContextAs<Task_CastSpellContext>(baseContext);
    if (context.failed)
        return "cannot cast";
    if (!context.spellId)
        return context.whoNeeds.empty() ? "nobody to help" : "";

    const SpellEntry* spellInfo = sSpellTemplate.LookupEntry<SpellEntry>(context.spellId);
    std::ostringstream out;
    out << "cast " << (spellInfo ? spellInfo->SpellName[0] : "?");
    return out.str();
}
