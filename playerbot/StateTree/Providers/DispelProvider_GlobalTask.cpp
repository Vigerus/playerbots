#include "DispelProvider_GlobalTask.h"

#include "playerbot/PlayerbotAI.h"
#include "playerbot/StateTree/STAI_TaskFactory.h"

#include "Spells/SpellAuras.h"
#include "Spells/SpellMgr.h"

#include <algorithm>
#include <sstream>
#include <stdexcept>

using namespace ai;

namespace
{
    constexpr uint32 curseMask = 1 << DISPEL_CURSE;

    // <Dispel type="..."/>: the dispel types a provider watches
    uint32 DispelTypeBit(const std::string& type)
    {
        if (type == "magic")   return 1 << DISPEL_MAGIC;
        if (type == "curse")   return 1 << DISPEL_CURSE;
        if (type == "disease") return 1 << DISPEL_DISEASE;
        if (type == "poison")  return 1 << DISPEL_POISON;
        throw std::runtime_error("<Dispel type=\"" + type + "\">: expected magic, curse, disease or poison");
    }
}

uint32 DispelProvider_GlobalTask::ParseDispelMask(const STAI_TaskConfig& config, uint32 fallback)
{
    uint32 mask = 0;
    for (const auto& [key, child] : config)
        if (key == "Dispel")
            mask |= DispelTypeBit(child.get<std::string>("<xmlattr>.type", ""));
    return mask ? mask : fallback;
}

std::vector<uint32> DispelProvider_GlobalTask::ParseAuras(const STAI_TaskConfig& config)
{
    std::vector<uint32> auras;
    for (const auto& [key, child] : config)
    {
        if (key == "<xmlattr>" || key == "<xmlcomment>" || key == "Dispel")
            continue;
        if (key != "Aura")
            throw std::runtime_error("unexpected <" + key + "> (expected <Dispel type=\"...\"/> or <Aura spellid=\"...\"/>)");

        const std::string idText = child.get<std::string>("<xmlattr>.spellid", "");
        const uint32 spellId = uint32(std::strtoul(idText.c_str(), nullptr, 10));
        if (!spellId || !sSpellTemplate.LookupEntry<SpellEntry>(spellId))
            throw std::runtime_error("<Aura spellid=\"" + idText + "\">: unknown spell");

        const uint32 firstSpellId = sSpellMgr.GetFirstSpellInChain(spellId);
        if (std::find(auras.begin(), auras.end(), firstSpellId) == auras.end())
            auras.push_back(firstSpellId);
    }
    return auras;
}

void DispelProvider_GlobalTask::Validate(const STAI_TaskConfig& config)
{
    if (!ParseDispelMask(config, 0))
        throw std::runtime_error("needs at least one <Dispel type=\"...\"/>");
    ParseAuras(config);
}

std::unique_ptr<STAI_Task> DispelProvider_GlobalTask::Create(const STAI_TaskConfig& config)
{
    return std::make_unique<DispelProvider_GlobalTask>(ParseDispelMask(config, 0), ParseAuras(config));
}

void CurseProvider_GlobalTask::Validate(const STAI_TaskConfig& config)
{
    if (ParseDispelMask(config, curseMask) != curseMask)
        throw std::runtime_error("only watches curses: use DispelProvider_GlobalTask for other types");
    ParseAuras(config);
}

std::unique_ptr<STAI_Task> CurseProvider_GlobalTask::Create(const STAI_TaskConfig& config)
{
    return std::make_unique<CurseProvider_GlobalTask>(curseMask, ParseAuras(config));
}

EActionRunStatus DispelProvider_GlobalTask::OnEnter(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(DispelProvider_GlobalTask::OnEnter);
    DispelProvider_GlobalTaskContext& context = ContextAs<DispelProvider_GlobalTaskContext>(baseContext);
    context.task = this;
    context.Subscribe();
    context.Evaluate();
    return EActionRunStatus::Running;
}

void DispelProvider_GlobalTask::OnExit(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(DispelProvider_GlobalTask::OnExit);
    DispelProvider_GlobalTaskContext& context = ContextAs<DispelProvider_GlobalTaskContext>(baseContext);
    context.Unsubscribe();
    context.whoNeeds.clear();
    context.magic.clear();
    context.curse.clear();
    context.disease.clear();
    context.poison.clear();
}

const std::vector<STAI_PropertyDef>& DispelProvider_GlobalTask::GetProperties() const
{
    static const std::vector<STAI_PropertyDef> properties =
    {
        STAI_PropertyDef::Of("whoNeeds", &DispelProvider_GlobalTaskContext::whoNeeds),
        STAI_PropertyDef::Of("magic", &DispelProvider_GlobalTaskContext::magic),
        STAI_PropertyDef::Of("curse", &DispelProvider_GlobalTaskContext::curse),
        STAI_PropertyDef::Of("disease", &DispelProvider_GlobalTaskContext::disease),
        STAI_PropertyDef::Of("poison", &DispelProvider_GlobalTaskContext::poison),
    };
    return properties;
}

const std::vector<STAI_DelegateDef>& DispelProvider_GlobalTask::GetDelegates() const
{
    static const std::vector<STAI_DelegateDef> delegates = { STAI_DelegateDef::Of("onNeedsHelp", &DispelProvider_GlobalTaskContext::onNeedsHelp) };
    return delegates;
}

bool DispelProvider_GlobalTask::IsWatchedHolder(const SpellAuraHolder* holder) const
{
    const SpellEntry* spellInfo = holder->GetSpellProto();
    if (spellInfo->Dispel >= 32 || !(dispelMask & (1 << spellInfo->Dispel)) || holder->IsPositive())
        return false;
    return auras.empty() || std::find(auras.begin(), auras.end(), sSpellMgr.GetFirstSpellInChain(spellInfo->Id)) != auras.end();
}

uint32 DispelProvider_GlobalTask::GetWatchedDispels(const Unit* unit) const
{
    uint32 found = 0;
    for (const auto& [spellId, holder] : unit->GetSpellAuraHolderMap())
        if (!holder->IsDeleted() && IsWatchedHolder(holder))
            found |= 1 << holder->GetSpellProto()->Dispel;
    return found;
}

// ---------------------------------------------------------------- context

void DispelProvider_GlobalTaskContext::Evaluate(Group* leftGroup)
{
    whoNeeds.clear();
    magic.clear();
    curse.clear();
    disease.clear();
    poison.clear();
    for (Player* member : GetReachableMembers(leftGroup))
    {
        const uint32 found = task->GetWatchedDispels(member);
        if (!found)
            continue;

        const ObjectGuid guid = member->GetObjectGuid();
        whoNeeds.push_back(guid);
        if (found & (1 << DISPEL_MAGIC))   magic.push_back(guid);
        if (found & (1 << DISPEL_CURSE))   curse.push_back(guid);
        if (found & (1 << DISPEL_DISEASE)) disease.push_back(guid);
        if (found & (1 << DISPEL_POISON))  poison.push_back(guid);
    }

    // the bot helps itself first
    for (std::vector<ObjectGuid>* list : { &whoNeeds, &magic, &curse, &disease, &poison })
    {
        auto self = std::find(list->begin(), list->end(), bot->GetObjectGuid());
        if (self != list->end())
            std::rotate(list->begin(), self, self + 1);
    }

    if (!whoNeeds.empty())
        onNeedsHelp.Broadcast();
}

bool DispelProvider_GlobalTaskContext::IsWatchedAura(const SpellAuraHolder* holder) const
{
    return task->IsWatchedHolder(holder);
}
