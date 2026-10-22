#include "Task_EatAndDrink.h"

#include "playerbot/PlayerbotAI.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"

#include "Spells/SpellAuras.h"

#include <cstdlib>
#include <initializer_list>
#include <sstream>
#include <stdexcept>

using namespace ai;

namespace
{
    // the task fails when the bot could not start eating or drinking for this long
    constexpr uint32 startTimeoutMs = 3000;

    // <Health until="..."/> / <Mana until="..."/>, percent
    float ParseUntil(const STAI_TaskConfig& config, const std::string& name)
    {
        const std::string value = STAI_TaskFactory::GetSetting(config, name, "until").value_or("100");
        char* end = nullptr;
        const float percent = std::strtof(value.c_str(), &end);
        if (value.empty() || *end || percent <= 0.0f || percent > 100.0f)
            throw std::runtime_error("<" + name + " until=\"...\">: expected a percentage above 0 and up to 100, got '" + value + "'");
        return percent;
    }

    // a seated aura restoring the resource: food / drink items use MOD_REGEN / MOD_POWER_REGEN, the bots' eat / drink
    // cheat spells (24005 / 24355) OBS_MOD_HEALTH / OBS_MOD_MANA; all of them end when the bot stands up
    bool HasSeatedAura(const Player* bot, std::initializer_list<AuraType> types)
    {
        for (AuraType type : types)
            for (const Aura* aura : bot->GetAurasByType(type))
                if (aura->GetSpellProto()->AuraInterruptFlags & AURA_INTERRUPT_FLAG_STANDING_CANCELS)
                    return true;
        return false;
    }

    bool IsEating(const Player* bot) { return HasSeatedAura(bot, { SPELL_AURA_MOD_REGEN, SPELL_AURA_OBS_MOD_HEALTH }); }
    bool IsDrinking(const Player* bot) { return HasSeatedAura(bot, { SPELL_AURA_MOD_POWER_REGEN, SPELL_AURA_OBS_MOD_MANA }); }

    // starts eating (food) or drinking: the bots' item cheat casts the eat / drink spell, otherwise a food / drink
    // item of the bags is used (was the legacy "food" / "drink" action); false when it could not start (moving,
    // casting, nothing to eat or drink)
    bool StartConsuming(PlayerbotAI* ai, Player* bot, bool food)
    {
        if (sServerFacade.IsInCombat(bot) || bot->IsNonMeleeSpellCasted(true))
            return false;

        if (sServerFacade.isMoving(bot))
        {
            ai->StopMoving();
            return false;
        }

        if (ai->HasCheat(BotCheatMask::item))
        {
            const uint32 spellId = food ? 24005 : 24355;
            const SpellEntry* pSpellInfo = sServerFacade.LookupSpellInfo(spellId);
            if (!pSpellInfo)
                return false;

            ai->Unmount();
            ai->InterruptSpell();
            const bool cast = ai->CastSpell(spellId, bot);
            bot->RemoveSpellCooldown(*pSpellInfo);
            return cast;
        }

        const std::list<Item*> items = ai->InventoryParseItems(food ? "food" : "drink", IterateItemsMask::ITERATE_ITEMS_IN_BAGS);
        if (items.empty())
            return false;

        ai->Unmount();
        ai->ImbueItem(items.front());
        return true;
    }
}

void Task_EatAndDrink::Validate(const STAI_TaskConfig& config)
{
    ParseUntil(config, "Health");
    ParseUntil(config, "Mana");
}

std::unique_ptr<STAI_Task> Task_EatAndDrink::Create(const STAI_TaskConfig& config)
{
    return std::make_unique<Task_EatAndDrink>(ParseUntil(config, "Health"), ParseUntil(config, "Mana"));
}

bool Task_EatAndDrink::NeedsHealth(const Player* bot) const
{
    return bot->GetHealthPercent() < healthUntil;
}

bool Task_EatAndDrink::NeedsMana(const Player* bot) const
{
    return bot->GetPowerType() == POWER_MANA && bot->GetPowerPercent(POWER_MANA) < manaUntil;
}

void Task_EatAndDrink::Start(Task_EatAndDrinkContext& context) const
{
    Player* bot = context.bot;

    // a successful action used an item (or cast the cheat): it is not run again in this meal, even when its aura
    // shows up a moment later; a refused one (the bot was still moving) is retried
    if (!context.eatStarted && NeedsHealth(bot) && !IsEating(bot))
        context.eatStarted = StartConsuming(context.ai, bot, true);
    if (!context.drinkStarted && NeedsMana(bot) && !IsDrinking(bot))
        context.drinkStarted = StartConsuming(context.ai, bot, false);
}

void Task_EatAndDrink::OnEnter(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(Task_EatAndDrink::OnEnter);
    Task_EatAndDrinkContext& context = ContextAs<Task_EatAndDrinkContext>(baseContext);
    context.enterMs = WorldTimer::getMSTime();
    context.eatStarted = false;
    context.drinkStarted = false;
    context.consumed = false;
    Start(context);
}

EActionRunStatus Task_EatAndDrink::OnTick(STAI_TaskContext& baseContext, uint32 elapsed, bool minimal) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(Task_EatAndDrink::OnTick);
    Task_EatAndDrinkContext& context = ContextAs<Task_EatAndDrinkContext>(baseContext);
    Player* bot = context.bot;

    if (!NeedsHealth(bot) && !NeedsMana(bot))
        return EActionRunStatus::Succeeded;

    // the meal runs while a food / drink aura restores something still below its target
    const bool eating = NeedsHealth(bot) && IsEating(bot);
    const bool drinking = NeedsMana(bot) && IsDrinking(bot);
    if (eating || drinking)
    {
        context.consumed = true;
        return EActionRunStatus::Running;
    }

    // the food / drink ran out before the targets: the meal is over, the next selection decides on another one
    if (context.consumed)
        return EActionRunStatus::Succeeded;

    // nothing started yet (the bot was moving, the aura is not applied yet): retry for a moment
    if (WorldTimer::getMSTimeDiff(context.enterMs, WorldTimer::getMSTime()) < startTimeoutMs)
    {
        Start(context);
        return EActionRunStatus::Running;
    }

    return EActionRunStatus::Failed;
}

void Task_EatAndDrink::OnExit(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(Task_EatAndDrink::OnExit);
    // standing up ends the food and drink auras, the next node starts from a standing bot
    Player* bot = baseContext.bot;
    if (bot->IsSitState())
        bot->SetStandState(UNIT_STAND_STATE_STAND);
}

std::string Task_EatAndDrink::GetDebugInfo() const
{
    std::ostringstream out;
    out << "Eat to " << healthUntil << "% health, drink to " << manaUntil << "% mana";
    return out.str();
}
