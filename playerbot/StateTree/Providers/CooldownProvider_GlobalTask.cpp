#include "CooldownProvider_GlobalTask.h"

#include "playerbot/StateTree/STAI_TaskFactory.h"

#include "Entities/Player.h"
#include "Spells/SpellMgr.h"

#include <algorithm>
#include <stdexcept>

using namespace ai;

namespace
{
    // how often the cooldowns are checked
    constexpr uint32 checkIntervalMs = 250;
}

std::vector<CooldownProvider_GlobalTask::WatchedSpell> CooldownProvider_GlobalTask::ParseSpells(const STAI_TaskConfig& config)
{
    std::vector<WatchedSpell> spells;
    for (const auto& [key, child] : config)
    {
        if (key == "<xmlattr>" || key == "<xmlcomment>")
            continue;
        if (key != "Spell")
            throw std::runtime_error("unexpected <" + key + "> (expected <Spell id=\"...\"/>)");

        const std::string idText = child.get<std::string>("<xmlattr>.id", "");
        const uint32 spellId = uint32(std::strtoul(idText.c_str(), nullptr, 10));
        if (!spellId || !sSpellTemplate.LookupEntry<SpellEntry>(spellId))
            throw std::runtime_error("<Spell id=\"" + idText + "\">: unknown spell");
        if (std::any_of(spells.begin(), spells.end(), [&](const WatchedSpell& spell) { return spell.spellId == spellId; }))
            continue;

        // every rank of the spell's chain, highest first (the bot knows one of them)
        WatchedSpell spell{ spellId, {} };
        for (uint32 rank = sSpellMgr.GetFirstSpellInChain(spellId); rank && spell.ranks.size() < 32; rank = sSpellMgr.GetNextSpellInChain(rank))
            spell.ranks.insert(spell.ranks.begin(), rank);
        if (std::find(spell.ranks.begin(), spell.ranks.end(), spellId) == spell.ranks.end())
            spell.ranks = { spellId };
        spells.push_back(std::move(spell));
    }
    if (spells.empty())
        throw std::runtime_error("needs at least one <Spell id=\"...\"/>");
    return spells;
}

void CooldownProvider_GlobalTask::Validate(const STAI_TaskConfig& config)
{
    ParseSpells(config);
}

std::unique_ptr<STAI_TaskBase> CooldownProvider_GlobalTask::Create(const STAI_TaskConfig& config)
{
    return std::make_unique<CooldownProvider_GlobalTask>(ParseSpells(config));
}

void CooldownProvider_GlobalTask::Update(CooldownProvider_GlobalTaskContext& context, bool first) const
{
    Player* bot = context.bot;
    bool changed = first;
    for (const WatchedSpell& spell : spells)
    {
        // the highest rank the bot knows: its own and its category's cooldowns (GCD not included)
        bool ready = false;
        for (uint32 rank : spell.ranks)
        {
            if (!bot->HasSpell(rank))
                continue;
            ready = bot->IsSpellReady(rank);
            break;
        }

        bool& entry = context.ready[spell.spellId];
        changed |= entry != ready;
        entry = ready;
    }

    if (changed)
        context.onCooldownChanged.Broadcast();
}

EActionRunStatus CooldownProvider_GlobalTask::OnEnterState(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(CooldownProvider_GlobalTask::OnEnterState);
    CooldownProvider_GlobalTaskContext& context = ContextAs<CooldownProvider_GlobalTaskContext>(baseContext);
    context.ready.clear();
    context.sinceCheckMs = 0;
    Update(context, true);
    return EActionRunStatus::Running;
}

EActionRunStatus CooldownProvider_GlobalTask::OnTick(STAI_TaskContext& baseContext, uint32 elapsed, bool minimal) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(CooldownProvider_GlobalTask::OnTick);
    CooldownProvider_GlobalTaskContext& context = ContextAs<CooldownProvider_GlobalTaskContext>(baseContext);
    context.sinceCheckMs += elapsed;
    if (context.sinceCheckMs >= checkIntervalMs)
    {
        context.sinceCheckMs = 0;
        Update(context, false);
    }
    return EActionRunStatus::Running;
}

const std::vector<STAI_PropertyDef>& CooldownProvider_GlobalTask::GetProperties() const
{
    static const std::vector<STAI_PropertyDef> properties = { STAI_PropertyDef::Of("ready", &CooldownProvider_GlobalTaskContext::ready) };
    return properties;
}

const std::vector<STAI_DelegateDef>& CooldownProvider_GlobalTask::GetDelegates() const
{
    static const std::vector<STAI_DelegateDef> delegates = { STAI_DelegateDef::Of("onCooldownChanged", &CooldownProvider_GlobalTaskContext::onCooldownChanged) };
    return delegates;
}

std::vector<uint32> CooldownProvider_GlobalTask::GetDebugSpells(const STAI_TaskContext& context) const
{
    std::vector<uint32> ids;
    for (const WatchedSpell& spell : spells)
        ids.push_back(spell.spellId);
    return ids;
}
