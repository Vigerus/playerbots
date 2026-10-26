#include "STAI_SpellConfig.h"

#include "Entities/Player.h"
#include "Spells/SpellMgr.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>

using namespace ai;

namespace
{
    std::string ToLower(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        return text;
    }

    bool IsNumber(const std::string& text)
    {
        return !text.empty() && std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isdigit(c); });
    }
}

STAI_ConfiguredSpell STAI_ConfiguredSpell::Parse(const STAI_TaskConfig& config)
{
    const std::string value = config.get<std::string>("<xmlattr>.spellid", "");
    if (value.empty())
        throw std::runtime_error("needs spellid=\"<spell id or name>\"");

    if (IsNumber(value))
    {
        const uint32 spellId = uint32(std::strtoul(value.c_str(), nullptr, 10));
        if (!sSpellTemplate.LookupEntry<SpellEntry>(spellId))
            throw std::runtime_error("spellid=\"" + value + "\": unknown spell");
        return { spellId, "", {} };
    }

    STAI_ConfiguredSpell spell;
    spell.spellName = ToLower(value);
    for (uint32 id = 1; id < sSpellTemplate.GetMaxEntry(); ++id)
        if (const SpellEntry* spellInfo = sSpellTemplate.LookupEntry<SpellEntry>(id))
            if (ToLower(spellInfo->SpellName[0]) == spell.spellName)
                spell.namedSpells.push_back(id);

    if (spell.namedSpells.empty())
        throw std::runtime_error("spellid=\"" + value + "\": no spell has that name");

    // stable: spells of the same rank keep their id order
    std::stable_sort(spell.namedSpells.begin(), spell.namedSpells.end(), [](uint32 a, uint32 b) { return sSpellMgr.GetSpellRank(a) > sSpellMgr.GetSpellRank(b); });
    spell.spellId = spell.namedSpells.front();
    return spell;
}

uint32 STAI_ConfiguredSpell::Resolve(const Player* bot) const
{
    if (spellName.empty())
        return bot->HasSpell(spellId) ? spellId : 0;

    // the highest rank the bot knows (HasSpell: learned, not removed or disabled)
    for (uint32 id : namedSpells)
        if (bot->HasSpell(id))
            return id;
    return 0;
}
