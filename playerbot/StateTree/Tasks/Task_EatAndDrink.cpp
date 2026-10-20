#include "Task_EatAndDrink.h"

#include "playerbot/PlayerbotAI.h"
#include "playerbot/strategy/Action.h"
#include "playerbot/strategy/AiObjectContext.h"

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

    // the legacy action without its own thresholds (isUseful)
    bool RunLegacyAction(PlayerbotAI* ai, const std::string& name)
    {
        Action* action = ai->GetAiObjectContext()->GetAction(name);
        if (!action || !action->isPossible())
            return false;

        Event event;
        return action->Execute(event);
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
        context.eatStarted = RunLegacyAction(context.ai, "food");
    if (!context.drinkStarted && NeedsMana(bot) && !IsDrinking(bot))
        context.drinkStarted = RunLegacyAction(context.ai, "drink");
}

void Task_EatAndDrink::OnEnter(STAI_TaskContext& baseContext) const
{
    Task_EatAndDrinkContext& context = ContextAs<Task_EatAndDrinkContext>(baseContext);
    context.enterMs = WorldTimer::getMSTime();
    context.eatStarted = false;
    context.drinkStarted = false;
    context.consumed = false;
    Start(context);
}

EActionRunStatus Task_EatAndDrink::OnTick(STAI_TaskContext& baseContext, uint32 elapsed, bool minimal) const
{
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
