#include "Task_SelfResurrect.h"

#include "playerbot/ServerFacade.h"

#include "Entities/Player.h"
#include "Server/WorldSession.h"
#include "Spells/SpellMgr.h"

#include <algorithm>
#include <stdexcept>

using namespace ai;

namespace
{
    // the self resurrection brings the bot back at once; give up after this long
    constexpr uint32 useTimeoutMs = 5000;
}

std::vector<uint32> Task_SelfResurrect::ParseSpells(const STAI_TaskConfig& config)
{
    std::vector<uint32> spells;
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
        spells.push_back(sSpellMgr.GetFirstSpellInChain(spellId));
    }
    return spells;
}

void Task_SelfResurrect::Validate(const STAI_TaskConfig& config)
{
    ParseSpells(config);
}

std::unique_ptr<STAI_Task> Task_SelfResurrect::Create(const STAI_TaskConfig& config)
{
    return std::make_unique<Task_SelfResurrect>(ParseSpells(config));
}

EActionRunStatus Task_SelfResurrect::OnEnter(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(Task_SelfResurrect::OnEnter);
    Task_SelfResurrectContext& context = ContextAs<Task_SelfResurrectContext>(baseContext);
    context.spellId = 0;
    context.sinceUseMs = 0;

    Player* bot = context.bot;
    if (sServerFacade.IsAlive(bot))
        return EActionRunStatus::Succeeded;

    // set by the core when the bot died, cleared once used or released
    const uint32 spellId = bot->GetUInt32Value(PLAYER_SELF_RES_SPELL);
    const SpellEntry* spellInfo = spellId ? sSpellTemplate.LookupEntry<SpellEntry>(spellId) : nullptr;
    if (!spellInfo)
        return EActionRunStatus::Failed;
    if (!spells.empty() && std::find(spells.begin(), spells.end(), sSpellMgr.GetFirstSpellInChain(spellId)) == spells.end())
        return EActionRunStatus::Failed;

    // off cooldown and its reagents in the bags (Reincarnation: an Ankh)
    if (!bot->IsSpellReady(spellId))
        return EActionRunStatus::Failed;
    for (uint32 i = 0; i < MAX_SPELL_REAGENTS; ++i)
        if (spellInfo->Reagent[i] > 0 && !bot->HasItemCount(uint32(spellInfo->Reagent[i]), spellInfo->ReagentCount[i]))
            return EActionRunStatus::Failed;

    // as the client's button does
    StartLog(context);
    WorldPacket packet(CMSG_SELF_RES, 0);
    bot->GetSession()->HandleSelfResOpcode(packet);
    context.spellId = spellId;
    return LogStatus(context, sServerFacade.IsAlive(bot) ? EActionRunStatus::Succeeded : EActionRunStatus::Running);
}

EActionRunStatus Task_SelfResurrect::OnTick(STAI_TaskContext& baseContext, uint32 elapsed, bool minimal) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(Task_SelfResurrect::OnTick);
    Task_SelfResurrectContext& context = ContextAs<Task_SelfResurrectContext>(baseContext);
    if (sServerFacade.IsAlive(context.bot))
        return LogStatus(context, EActionRunStatus::Succeeded);

    context.sinceUseMs += elapsed;
    return LogStatus(context, context.sinceUseMs < useTimeoutMs ? EActionRunStatus::Running : EActionRunStatus::Failed);
}

std::vector<uint32> Task_SelfResurrect::GetDebugSpells(const STAI_TaskContext& baseContext) const
{
    const Task_SelfResurrectContext& context = static_cast<const Task_SelfResurrectContext&>(baseContext);
    if (context.spellId)
        return { context.spellId };
    return spells;
}
