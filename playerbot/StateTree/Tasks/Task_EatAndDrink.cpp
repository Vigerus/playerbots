#include "Task_EatAndDrink.h"

#include "playerbot/PlayerbotAI.h"
#include "playerbot/strategy/Action.h"
#include "playerbot/strategy/AiObjectContext.h"

#include <cstdlib>
#include <sstream>
#include <stdexcept>

using namespace ai;

namespace
{
    // the task fails when the bot neither eats nor drinks for this long
    constexpr uint32 idleTimeoutMs = 3000;

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

    bool IsEating(const Player* bot) { return bot->HasAuraType(SPELL_AURA_MOD_REGEN); }
    bool IsDrinking(const Player* bot) { return bot->HasAuraType(SPELL_AURA_MOD_POWER_REGEN); }

    // food / water in the bags, or the bot's cheat
    bool HasSupplies(PlayerbotAI* ai, const std::string& name)
    {
        return ai->HasCheat(BotCheatMask::item) || !ai->GetAiObjectContext()->GetValue<std::list<Item*>>("inventory items", name)->Get().empty();
    }

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

bool Task_EatAndDrink::Consume(Task_EatAndDrinkContext& context) const
{
    Player* bot = context.bot;

    // both at once: the drink does not stop the food and the other way round
    if (NeedsHealth(bot) && !IsEating(bot))
        RunLegacyAction(context.ai, "food");
    if (NeedsMana(bot) && !IsDrinking(bot))
        RunLegacyAction(context.ai, "drink");

    const bool active = (NeedsHealth(bot) && IsEating(bot)) || (NeedsMana(bot) && IsDrinking(bot));
    if (active)
        context.lastActiveMs = WorldTimer::getMSTime();
    return active;
}

void Task_EatAndDrink::OnEnter(STAI_TaskContext& baseContext) const
{
    Task_EatAndDrinkContext& context = ContextAs<Task_EatAndDrinkContext>(baseContext);
    context.lastActiveMs = WorldTimer::getMSTime();
    if (NeedsHealth(context.bot) || NeedsMana(context.bot))
        Consume(context);
}

EActionRunStatus Task_EatAndDrink::OnTick(STAI_TaskContext& baseContext, uint32 elapsed, bool minimal) const
{
    Task_EatAndDrinkContext& context = ContextAs<Task_EatAndDrinkContext>(baseContext);
    Player* bot = context.bot;

    if (!NeedsHealth(bot) && !NeedsMana(bot))
        return EActionRunStatus::Succeeded;

    // again when the food / drink ran out before the target (or the bot was still moving)
    if (Consume(context))
        return EActionRunStatus::Running;

    // nothing to eat or drink: at once when the bags are empty, else after a while (the legacy action did not start)
    const bool supplies = (NeedsHealth(bot) && HasSupplies(context.ai, "food")) || (NeedsMana(bot) && HasSupplies(context.ai, "drink"));
    if (!supplies || WorldTimer::getMSTimeDiff(context.lastActiveMs, WorldTimer::getMSTime()) >= idleTimeoutMs)
        return EActionRunStatus::Failed;
    return EActionRunStatus::Running;
}

void Task_EatAndDrink::OnExit(STAI_TaskContext& baseContext) const
{
    // standing up ends the food and drink auras, the next node starts from a standing bot
    Player* bot = baseContext.bot;
    if (bot->IsSitState())
        bot->SetStandState(UNIT_STAND_STATE_STAND);
}

std::string Task_EatAndDrink::GetDebugInfo(const STAI_TaskContext& baseContext) const
{
    const Player* bot = baseContext.bot;
    std::ostringstream out;
    out << (IsEating(bot) ? "eating" : "not eating") << ", " << (IsDrinking(bot) ? "drinking" : "not drinking");
    return out.str();
}
