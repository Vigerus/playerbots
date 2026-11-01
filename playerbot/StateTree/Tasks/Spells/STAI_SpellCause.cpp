#include "STAI_SpellCause.h"

#include "Spells/SpellMgr.h"

#include <algorithm>
#include <sstream>
#include <stdexcept>

using namespace ai;

STAI_SpellFilter STAI_SpellFilter::Parse(const STAI_TaskConfig& config)
{
    const size_t observeCount = config.count("Observe");
    const size_t ignoreCount = config.count("Ignore");
    if (observeCount && ignoreCount)
        throw std::runtime_error("<Observe> and <Ignore> are mutually exclusive: react only to some spells, or to all but some");
    if (observeCount > 1 || ignoreCount > 1)
        throw std::runtime_error(std::string("only one <") + (observeCount ? "Observe" : "Ignore") + ">: list the spells in its spellid, comma separated");

    STAI_SpellFilter filter;
    filter.observe = observeCount > 0;
    if (!observeCount && !ignoreCount)
        return filter;

    const std::string name = filter.observe ? "Observe" : "Ignore";
    // the element counts even without spellid: <Observe/> reacts to none
    const std::string list = STAI_TaskFactory::GetSetting(config, name, "spellid").value_or("");
    std::stringstream ids(list);
    std::string idText;
    while (std::getline(ids, idText, ','))
    {
        idText.erase(std::remove_if(idText.begin(), idText.end(), ::isspace), idText.end());
        if (idText.empty())
            continue;

        const uint32 spellId = uint32(std::strtoul(idText.c_str(), nullptr, 10));
        if (!spellId || !sSpellTemplate.LookupEntry<SpellEntry>(spellId))
            throw std::runtime_error("<" + name + " spellid=\"" + list + "\">: unknown spell " + idText);

        const uint32 firstSpellId = sSpellMgr.GetFirstSpellInChain(spellId);
        if (std::find(filter.spells.begin(), filter.spells.end(), firstSpellId) == filter.spells.end())
            filter.spells.push_back(firstSpellId);
    }
    return filter;
}

bool STAI_SpellFilter::Allows(uint32 spellId) const
{
    const bool listed = !spells.empty() && std::find(spells.begin(), spells.end(), sSpellMgr.GetFirstSpellInChain(spellId)) != spells.end();
    return observe ? listed : !listed;
}

uint32 ai::STAI_GetDispelMask(const SpellEntry* spellInfo)
{
    uint32 mask = 0;
    for (int i = 0; spellInfo && i < MAX_EFFECT_INDEX; ++i)
    {
        if (spellInfo->Effect[i] == SPELL_EFFECT_DISPEL)
            mask |= GetDispellMask(DispelType(spellInfo->EffectMiscValue[i]));
        else if (spellInfo->Effect[i] == SPELL_EFFECT_APPLY_AURA && spellInfo->EffectApplyAuraName[i] == SPELL_AURA_PERIODIC_TRIGGER_SPELL &&
            spellInfo->EffectTriggerSpell[i] != spellInfo->Id)
        {
            const SpellEntry* triggered = sSpellTemplate.LookupEntry<SpellEntry>(spellInfo->EffectTriggerSpell[i]);
            for (int j = 0; triggered && j < MAX_EFFECT_INDEX; ++j)
                if (triggered->Effect[j] == SPELL_EFFECT_DISPEL)
                    mask |= GetDispellMask(DispelType(triggered->EffectMiscValue[j]));
        }
    }
    return mask;
}
