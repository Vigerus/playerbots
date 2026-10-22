#include "CurseProvider_GlobalTask.h"

#include "playerbot/PlayerbotAI.h"
#include "playerbot/StateTree/STAI_TaskFactory.h"

#include "Spells/SpellAuras.h"
#include "Spells/SpellMgr.h"

#include <algorithm>
#include <sstream>
#include <stdexcept>

using namespace ai;

std::vector<uint32> CurseProvider_GlobalTask::ParseAuras(const STAI_TaskConfig& config)
{
    std::vector<uint32> auras;
    for (const auto& [key, child] : config)
    {
        if (key == "<xmlattr>" || key == "<xmlcomment>")
            continue;
        if (key != "Aura")
            throw std::runtime_error("unexpected <" + key + "> (expected <Aura spellid=\"...\"/>)");

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

void CurseProvider_GlobalTask::Validate(const STAI_TaskConfig& config)
{
    ParseAuras(config);
}

std::unique_ptr<STAI_Task> CurseProvider_GlobalTask::Create(const STAI_TaskConfig& config)
{
    return std::make_unique<CurseProvider_GlobalTask>(ParseAuras(config));
}

EActionRunStatus CurseProvider_GlobalTask::OnEnter(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(CurseProvider_GlobalTask::OnEnter);
    CurseProvider_GlobalTaskContext& context = ContextAs<CurseProvider_GlobalTaskContext>(baseContext);
    context.task = this;
    context.Subscribe();
    context.Evaluate();
    return EActionRunStatus::Running;
}

void CurseProvider_GlobalTask::OnExit(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(CurseProvider_GlobalTask::OnExit);
    CurseProvider_GlobalTaskContext& context = ContextAs<CurseProvider_GlobalTaskContext>(baseContext);
    context.Unsubscribe();
    context.whoNeeds.clear();
}

const std::vector<STAI_PropertyDef>& CurseProvider_GlobalTask::GetProperties() const
{
    static const std::vector<STAI_PropertyDef> properties = { STAI_PropertyDef::Of("whoNeeds", &CurseProvider_GlobalTaskContext::whoNeeds) };
    return properties;
}

const std::vector<STAI_DelegateDef>& CurseProvider_GlobalTask::GetDelegates() const
{
    static const std::vector<STAI_DelegateDef> delegates = { STAI_DelegateDef::Of("onNeedsHelp", &CurseProvider_GlobalTaskContext::onNeedsHelp) };
    return delegates;
}

bool CurseProvider_GlobalTask::IsWatchedSpell(const SpellEntry* spellInfo) const
{
    if (auras.empty())
        return spellInfo->Dispel == DISPEL_CURSE;
    return std::find(auras.begin(), auras.end(), sSpellMgr.GetFirstSpellInChain(spellInfo->Id)) != auras.end();
}

bool CurseProvider_GlobalTask::HasWatchedAura(const Unit* unit) const
{
    for (const auto& [spellId, holder] : unit->GetSpellAuraHolderMap())
        if (!holder->IsDeleted() && IsWatchedSpell(holder->GetSpellProto()))
            return true;
    return false;
}

// ---------------------------------------------------------------- context

void CurseProvider_GlobalTaskContext::Evaluate(Group* leftGroup)
{
    whoNeeds.clear();
    for (Player* member : GetReachableMembers(leftGroup))
        if (task->HasWatchedAura(member))
            whoNeeds.push_back(member->GetObjectGuid());

    // the bot helps itself first
    auto self = std::find(whoNeeds.begin(), whoNeeds.end(), bot->GetObjectGuid());
    if (self != whoNeeds.end())
        std::rotate(whoNeeds.begin(), self, self + 1);

    if (!whoNeeds.empty())
        onNeedsHelp.Broadcast();
}

bool CurseProvider_GlobalTaskContext::IsWatchedAura(const SpellAuraHolder* holder) const
{
    return task->IsWatchedSpell(holder->GetSpellProto());
}
