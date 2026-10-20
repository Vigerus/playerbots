#include "BuffProvider_GlobalTask.h"

#include "playerbot/PlayerbotAI.h"
#include "playerbot/StateTree/STAI_TaskFactory.h"

#include "Groups/Group.h"
#include "Spells/SpellAuras.h"
#include "Spells/SpellMgr.h"

#include <algorithm>
#include <map>
#include <sstream>
#include <stdexcept>

using namespace ai;

namespace
{
    bool IsAuraEffect(const SpellEntry* spellInfo, int i)
    {
        return spellInfo->Effect[i] == SPELL_EFFECT_APPLY_AURA || spellInfo->Effect[i] == SPELL_EFFECT_APPLY_AREA_AURA_PARTY;
    }

    // self: every aura lands on the caster; group: an aura lands on the party; else a single target
    BuffScope GetSpellScope(const SpellEntry* spellInfo)
    {
        bool self = true;
        for (int i = 0; i < MAX_EFFECT_INDEX; ++i)
        {
            if (!IsAuraEffect(spellInfo, i))
                continue;

            if (spellInfo->Effect[i] == SPELL_EFFECT_APPLY_AREA_AURA_PARTY)
                return BuffScope::Group;

            switch (spellInfo->EffectImplicitTargetA[i])
            {
                case TARGET_ENUM_UNITS_PARTY_WITHIN_CASTER_RANGE:
                case TARGET_ENUM_UNITS_PARTY_AOE_AT_SRC_LOC:
                case TARGET_ENUM_UNITS_PARTY_AOE_AT_DEST_LOC:
                case TARGET_UNIT_FRIEND_AND_PARTY:
                    return BuffScope::Group;
                case TARGET_UNIT_CASTER:
                    break;
                default:
                    self = false;
                    break;
            }
        }
        return self ? BuffScope::Self : BuffScope::Single;
    }

    const char* GetScopeName(BuffScope scope)
    {
        return scope == BuffScope::Self ? "self" : scope == BuffScope::Group ? "group" : "single";
    }

}

// ---------------------------------------------------------------- task

std::vector<BuffProvider_GlobalTask::Buff> BuffProvider_GlobalTask::ParseBuffs(const STAI_TaskConfig& config)
{
    std::vector<Buff> buffs;
    for (const auto& [key, child] : config)
    {
        if (key == "<xmlattr>" || key == "<xmlcomment>")
            continue;
        if (key != "Buff")
            throw std::runtime_error("unexpected <" + key + "> (expected <Buff spellid=\"...\"/>)");

        const std::string idText = child.get<std::string>("<xmlattr>.spellid", "");
        const uint32 spellId = uint32(std::strtoul(idText.c_str(), nullptr, 10));
        const SpellEntry* spellInfo = spellId ? sSpellTemplate.LookupEntry<SpellEntry>(spellId) : nullptr;
        if (!spellInfo)
            throw std::runtime_error("<Buff spellid=\"" + idText + "\">: unknown spell");

        int auraEffect = -1;
        for (int i = 0; i < MAX_EFFECT_INDEX && auraEffect < 0; ++i)
            if (IsAuraEffect(spellInfo, i))
                auraEffect = i;
        if (auraEffect < 0)
            throw std::runtime_error("<Buff spellid=\"" + idText + "\">: spell " + spellInfo->SpellName[0] + " applies no aura");

        BuffSpell spell;
        spell.firstSpellId = sSpellMgr.GetFirstSpellInChain(spellId);
        spell.scope = GetSpellScope(spellInfo);

        const std::string scope = child.get<std::string>("<xmlattr>.scope", "");
        if (scope == "self")
            spell.scope = BuffScope::Self;
        else if (scope == "single")
            spell.scope = BuffScope::Single;
        else if (scope == "group")
            spell.scope = BuffScope::Group;
        else if (!scope.empty())
            throw std::runtime_error("<Buff spellid=\"" + idText + "\">: scope must be self, single or group, got '" + scope + "'");

        // spells applying the same aura are the same buff (Arcane Intellect / Arcane Brilliance)
        const uint32 auraType = spellInfo->EffectApplyAuraName[auraEffect];
        const int32 miscValue = spellInfo->EffectMiscValue[auraEffect];
        auto buff = std::find_if(buffs.begin(), buffs.end(), [&](const Buff& b) { return b.auraType == auraType && b.miscValue == miscValue; });
        if (buff == buffs.end())
        {
            buffs.push_back({ auraType, miscValue, {} });
            buff = buffs.end() - 1;
        }

        for (const BuffSpell& existing : buff->spells)
            if (existing.firstSpellId == spell.firstSpellId)
                throw std::runtime_error("<Buff spellid=\"" + idText + "\">: another rank of " + spellInfo->SpellName[0] + " is listed already");
        buff->spells.push_back(spell);
    }

    if (buffs.empty())
        throw std::runtime_error("needs at least one <Buff spellid=\"...\"/>");
    return buffs;
}

void BuffProvider_GlobalTask::Validate(const STAI_TaskConfig& config)
{
    ParseBuffs(config);
}

std::unique_ptr<STAI_Task> BuffProvider_GlobalTask::Create(const STAI_TaskConfig& config)
{
    return std::make_unique<BuffProvider_GlobalTask>(ParseBuffs(config));
}

void BuffProvider_GlobalTask::InitContext(STAI_TaskContext& baseContext) const
{
    ContextAs<BuffProvider_GlobalTaskContext>(baseContext).task = this;
}

void BuffProvider_GlobalTask::OnEnter(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(BuffProvider_GlobalTask::OnEnter);
    BuffProvider_GlobalTaskContext& context = ContextAs<BuffProvider_GlobalTaskContext>(baseContext);
    context.Subscribe();
    context.Evaluate();
}

void BuffProvider_GlobalTask::OnExit(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(BuffProvider_GlobalTask::OnExit);
    BuffProvider_GlobalTaskContext& context = ContextAs<BuffProvider_GlobalTaskContext>(baseContext);
    context.Unsubscribe();
    context.needs.clear();
}

const std::vector<STAI_PropertyDef>& BuffProvider_GlobalTask::GetProperties() const
{
    static const std::vector<STAI_PropertyDef> properties = { STAI_PropertyDef::Of("needs", &BuffProvider_GlobalTaskContext::needs) };
    return properties;
}

const std::vector<STAI_DelegateDef>& BuffProvider_GlobalTask::GetDelegates() const
{
    static const std::vector<STAI_DelegateDef> delegates = { STAI_DelegateDef::Of("onMissingBuff", &BuffProvider_GlobalTaskContext::onMissingBuff) };
    return delegates;
}

std::vector<uint32> BuffProvider_GlobalTask::GetDebugSpells(const STAI_TaskContext& context) const
{
    std::vector<uint32> spells;
    for (const Buff& buff : buffs)
        for (const BuffSpell& spell : buff.spells)
            spells.push_back(spell.firstSpellId);
    return spells;
}

bool BuffProvider_GlobalTask::IsWatchedSpell(uint32 spellId) const
{
    const uint32 firstSpellId = sSpellMgr.GetFirstSpellInChain(spellId);
    for (const Buff& buff : buffs)
        for (const BuffSpell& spell : buff.spells)
            if (spell.firstSpellId == firstSpellId)
                return true;
    return false;
}

bool BuffProvider_GlobalTask::HasBuff(const Unit* unit, const Buff& buff)
{
    for (const BuffSpell& spell : buff.spells)
        for (uint32 rank = spell.firstSpellId; rank; rank = sSpellMgr.GetNextSpellInChain(rank))
            if (unit->HasAura(rank))
                return true;
    return false;
}

uint32 BuffProvider_GlobalTask::GetKnownRank(const Player* bot, uint32 firstSpellId)
{
    uint32 known = 0;
    for (uint32 rank = firstSpellId; rank; rank = sSpellMgr.GetNextSpellInChain(rank))
        if (bot->HasSpell(rank))
            known = rank;
    return known;
}

std::vector<BuffNeed> BuffProvider_GlobalTask::FindNeeds(Player* bot, Group* group, const std::vector<Player*>& members) const
{
    // no members: the bot itself cannot cast (dead, not in world)
    std::vector<BuffNeed> needs;
    if (members.empty())
        return needs;

    for (const Buff& buff : buffs)
    {
        uint32 selfSpell = 0, singleSpell = 0, groupSpell = 0;
        for (const BuffSpell& spell : buff.spells)
        {
            const uint32 rank = GetKnownRank(bot, spell.firstSpellId);
            uint32& slot = spell.scope == BuffScope::Self ? selfSpell : spell.scope == BuffScope::Group ? groupSpell : singleSpell;
            if (rank && !slot)
                slot = rank;
        }

        if (selfSpell)
        {
            if (!HasBuff(bot, buff))
                needs.push_back({ bot->GetObjectGuid(), selfSpell, BuffScope::Self, 1 });
            continue;
        }

        if (!singleSpell && !groupSpell)
            continue;

        std::map<uint8, std::vector<Player*>> missingBySubgroup;
        for (Player* member : members)
            if (!HasBuff(member, buff))
                missingBySubgroup[group ? group->GetMemberGroup(member->GetObjectGuid()) : 0].push_back(member);

        for (const auto& [subgroup, missing] : missingBySubgroup)
        {
            // a group spell covers the target's subgroup: worth it from 2 members on
            if (groupSpell && (missing.size() >= 2 || !singleSpell))
            {
                needs.push_back({ missing.front()->GetObjectGuid(), groupSpell, BuffScope::Group, uint32(missing.size()) });
                continue;
            }

            for (Player* member : missing)
                needs.push_back({ member->GetObjectGuid(), singleSpell, BuffScope::Single, 1 });
        }
    }

    return needs;
}

// ---------------------------------------------------------------- context

void BuffProvider_GlobalTaskContext::Evaluate(Group* leftGroup)
{
    Group* group = bot->GetGroup();
    needs = task->FindNeeds(bot, group == leftGroup ? nullptr : group, GetReachableMembers(leftGroup));
    if (!needs.empty())
        onMissingBuff.Broadcast();
}

bool BuffProvider_GlobalTaskContext::IsWatchedAura(const SpellAuraHolder* holder) const
{
    return task->IsWatchedSpell(holder->GetId());
}
