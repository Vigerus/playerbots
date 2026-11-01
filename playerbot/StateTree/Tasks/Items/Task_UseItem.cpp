#include "Task_UseItem.h"

#include "playerbot/PlayerbotAI.h"
#include "playerbot/ServerFacade.h"

#include "Entities/Item.h"
#include "Globals/ObjectMgr.h"
#include "Spells/SpellMgr.h"

#include <algorithm>
#include <stdexcept>

using namespace ai;

namespace
{
    // the item's spell has to go off within this long after the use
    constexpr uint32 useTimeoutMs = 3000;

    // the spell an item casts when used, 0 when it has none
    uint32 GetUseSpell(const ItemPrototype* proto)
    {
        for (const _Spell& spell : proto->Spells)
            if (spell.SpellId > 0 && spell.SpellTrigger == ITEM_SPELLTRIGGER_ON_USE)
                return uint32(spell.SpellId);
        return 0;
    }

    // the item's spell is on cooldown (the item's own or its category's)
    bool IsReady(Player* bot, uint32 spellId, const ItemPrototype* proto)
    {
        return bot->IsSpellReady(spellId, proto);
    }
}

std::vector<uint32> Task_UseItem::ParseItems(const STAI_TaskConfig& config)
{
    std::vector<uint32> items;
    for (const auto& [key, child] : config)
    {
        if (key == "<xmlattr>" || key == "<xmlcomment>")
            continue;
        if (key != "Item")
            throw std::runtime_error("unexpected <" + key + "> (expected <Item id=\"...\"/>)");

        const std::string idText = child.get<std::string>("<xmlattr>.id", "");
        const uint32 itemId = uint32(std::strtoul(idText.c_str(), nullptr, 10));
        const ItemPrototype* proto = itemId ? sObjectMgr.GetItemPrototype(itemId) : nullptr;
        if (!proto)
            throw std::runtime_error("<Item id=\"" + idText + "\">: unknown item");
        if (!GetUseSpell(proto))
            throw std::runtime_error("<Item id=\"" + idText + "\">: the item has no use spell");
        if (std::find(items.begin(), items.end(), itemId) == items.end())
            items.push_back(itemId);
    }
    if (items.empty())
        throw std::runtime_error("needs at least one <Item id=\"...\"/>");
    return items;
}

void Task_UseItem::Validate(const STAI_TaskConfig& config)
{
    ParseItems(config);
}

std::unique_ptr<STAI_TaskBase> Task_UseItem::Create(const STAI_TaskConfig& config)
{
    return std::make_unique<Task_UseItem>(ParseItems(config));
}

EActionRunStatus Task_UseItem::OnEnterState(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(Task_UseItem::OnEnterState);
    Task_UseItemContext& context = ContextAs<Task_UseItemContext>(baseContext);
    context.itemId = 0;
    context.spellId = 0;
    StartLog(context);

    Player* bot = context.bot;
    PlayerbotAI* ai = context.ai;
    if (!bot->IsInWorld() || !sServerFacade.IsAlive(bot) || bot->IsNonMeleeSpellCasted(true))
        return LogStatus(context, EActionRunStatus::Failed);

    // an item of the bags first
    for (uint32 itemId : items)
    {
        const ItemPrototype* proto = sObjectMgr.GetItemPrototype(itemId);
        Item* item = bot->GetItemByEntry(itemId);
        const uint32 spellId = GetUseSpell(proto);
        if (!item || !IsReady(bot, spellId, proto) || bot->GetLevel() < proto->RequiredLevel)
            continue;

        context.startMs = WorldTimer::getMSTime();
        ai->ImbueItem(item);
        context.itemId = itemId;
        context.spellId = spellId;
        return LogStatus(context, EActionRunStatus::Running);
    }

    // the item cheat: the best item the bot's level allows, by its spell
    if (ai->HasCheat(BotCheatMask::item))
    {
        for (uint32 itemId : items)
        {
            const ItemPrototype* proto = sObjectMgr.GetItemPrototype(itemId);
            const uint32 spellId = GetUseSpell(proto);
            if (bot->GetLevel() < proto->RequiredLevel)
                continue;
            if (!IsReady(bot, spellId, proto))
                break;

            context.startMs = WorldTimer::getMSTime();
            if (!ai->CastSpell(spellId, bot))
                break;
            context.itemId = itemId;
            context.spellId = spellId;
            return LogStatus(context, EActionRunStatus::Running);
        }
    }

    return LogStatus(context, EActionRunStatus::Failed);
}

EActionRunStatus Task_UseItem::OnTick(STAI_TaskContext& baseContext, uint32 elapsed, bool minimal) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(Task_UseItem::OnTick);
    Task_UseItemContext& context = ContextAs<Task_UseItemContext>(baseContext);

    // the use packet is handled with the bot's session: the item went off once its cooldown runs
    const ItemPrototype* proto = sObjectMgr.GetItemPrototype(context.itemId);
    const auto& go = context.ai->GetLastSpellGo();
    const bool wentOff = (go.spellId == context.spellId && WorldTimer::getMSTimeDiff(context.startMs, go.ms) < useTimeoutMs) ||
                         (proto && !IsReady(context.bot, context.spellId, proto));
    if (wentOff)
    {
        EndLog(context, true);
        return EActionRunStatus::Succeeded;
    }

    if (WorldTimer::getMSTimeDiff(context.startMs, WorldTimer::getMSTime()) >= useTimeoutMs)
        return LogStatus(context, EActionRunStatus::Failed);
    return LogStatus(context, EActionRunStatus::Running);
}

void Task_UseItem::OnExitState(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(Task_UseItem::OnExitState);
    Task_UseItemContext& context = ContextAs<Task_UseItemContext>(baseContext);
    const ItemPrototype* proto = sObjectMgr.GetItemPrototype(context.itemId);
    EndLog(context, proto && !IsReady(context.bot, context.spellId, proto));
}

std::string Task_UseItem::GetDebugInfo() const
{
    const ItemPrototype* proto = sObjectMgr.GetItemPrototype(items.front());
    std::string info = "Use " + STAI_SpellMarkup(proto ? GetUseSpell(proto) : 0);
    if (items.size() > 1)
        info += " (or " + std::to_string(items.size() - 1) + " more)";
    return info;
}
