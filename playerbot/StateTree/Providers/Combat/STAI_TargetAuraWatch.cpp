#include "STAI_TargetAuraWatch.h"

#include "Entities/Player.h"
#include "Spells/SpellAuraWorldSubsystem.h"
#include "Spells/SpellAuras.h"
#include "Spells/SpellMgr.h"
#include "World/World.h"

#include <algorithm>
#include <cstdlib>
#include <stdexcept>

using namespace ai;

std::vector<STAI_WatchedAura> STAI_TargetAuraWatch::Parse(const STAI_TaskConfig& config)
{
    std::vector<STAI_WatchedAura> auras;
    for (const auto& [key, child] : config)
    {
        if (key != "Aura")
            continue;

        const std::string idText = child.get<std::string>("<xmlattr>.spellid", "");
        const uint32 spellId = uint32(std::strtoul(idText.c_str(), nullptr, 10));
        if (!spellId || !sSpellTemplate.LookupEntry<SpellEntry>(spellId))
            throw std::runtime_error("<Aura spellid=\"" + idText + "\">: unknown spell");
        if (std::any_of(auras.begin(), auras.end(), [&](const STAI_WatchedAura& aura) { return aura.spellId == spellId; }))
            continue;

        // every rank of the spell's chain (any of them may be on the target)
        STAI_WatchedAura aura{ spellId, {} };
        for (uint32 rank = sSpellMgr.GetFirstSpellInChain(spellId); rank && aura.ranks.size() < 32; rank = sSpellMgr.GetNextSpellInChain(rank))
            aura.ranks.push_back(rank);
        if (std::find(aura.ranks.begin(), aura.ranks.end(), spellId) == aura.ranks.end())
            aura.ranks = { spellId };
        auras.push_back(std::move(aura));
    }
    return auras;
}

void STAI_TargetAuraWatch::Unsubscribe()
{
    if (subscribed.IsEmpty())
        return;

    if (SpellAuraWorldSubsystem* auraSubsystem = sWorld.GetSubsystem<SpellAuraWorldSubsystem>())
        auraSubsystem->RemoveAll(subscribed, this);
    subscribed.Clear();
}

bool STAI_TargetAuraWatch::Update(Unit* target, const std::vector<STAI_WatchedAura>& auras, std::map<uint32, uint32>& stacks)
{
    if (auras.empty())
        return false;

    // follow the target: its aura events mark the stacks stale
    const ObjectGuid guid = target ? target->GetObjectGuid() : ObjectGuid();
    if (guid != subscribed)
    {
        Unsubscribe();
        if (target)
        {
            if (SpellAuraWorldSubsystem* auraSubsystem = sWorld.GetSubsystem<SpellAuraWorldSubsystem>())
            {
                auraSubsystem->AddOnAuraApplied(guid, this, &STAI_TargetAuraWatch::OnAuraApplied);
                auraSubsystem->AddOnAuraRemoved(guid, this, &STAI_TargetAuraWatch::OnAuraRemoved);
                auraSubsystem->AddOnAuraStackChanged(guid, this, &STAI_TargetAuraWatch::OnAuraStackChanged);
            }
            subscribed = guid;
        }
        stale = true;
    }

    // the first update fills every entry
    if (!stale.exchange(false) && stacks.size() == auras.size())
        return false;

    bool changed = false;
    for (const STAI_WatchedAura& aura : auras)
    {
        uint32 stack = 0;
        if (target)
            for (uint32 rank : aura.ranks)
            {
                const Unit::SpellAuraHolderConstBounds bounds = static_cast<const Unit*>(target)->GetSpellAuraHolderBounds(rank);
                for (auto itr = bounds.first; itr != bounds.second; ++itr)
                    stack = std::max(stack, itr->second->GetStackAmount());
            }

        auto [entry, inserted] = stacks.try_emplace(aura.spellId, stack);
        changed |= inserted || entry->second != stack;
        entry->second = stack;
    }
    return changed;
}
