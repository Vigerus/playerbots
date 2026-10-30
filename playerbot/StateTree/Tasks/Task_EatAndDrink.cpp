#include "Task_EatAndDrink.h"

#include "playerbot/PlayerbotAI.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"

#include "Globals/ObjectMgr.h"
#include "Spells/SpellAuras.h"

#include <array>
#include <cstdlib>
#include <initializer_list>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <vector>

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

    bool SpellAppliesAura(const SpellEntry* spellInfo, AuraType type)
    {
        for (int i = 0; i < MAX_EFFECT_INDEX; ++i)
            if (spellInfo->Effect[i] == SPELL_EFFECT_APPLY_AURA && spellInfo->EffectApplyAuraName[i] == type)
                return true;
        return false;
    }

    // the use spell of the best conjured food (MOD_REGEN) / drink (MOD_POWER_REGEN) the bot's level may use, 0 when
    // there is none; built once from the item templates
    uint32 GetConjuredSpell(uint32 level, bool food)
    {
        struct Conjured { uint32 requiredLevel; uint32 itemLevel; uint32 spellId; };
        static const auto conjured = []
        {
            std::array<std::vector<Conjured>, 2> lists; // [0] drink, [1] food
            for (uint32 itemId = 0; itemId < sItemStorage.GetMaxEntry(); ++itemId)
            {
                const ItemPrototype* proto = sObjectMgr.GetItemPrototype(itemId);
                if (!proto || !proto->IsConjuredConsumable())
                    continue;

                const uint32 spellId = proto->Spells[0].SpellId;
                const SpellEntry* spellInfo = spellId ? sServerFacade.LookupSpellInfo(spellId) : nullptr;
                if (!spellInfo || !(spellInfo->AuraInterruptFlags & AURA_INTERRUPT_FLAG_STANDING_CANCELS))
                    continue;

                if (SpellAppliesAura(spellInfo, SPELL_AURA_MOD_REGEN))
                    lists[1].push_back({ proto->RequiredLevel, proto->ItemLevel, spellId });
                if (SpellAppliesAura(spellInfo, SPELL_AURA_MOD_POWER_REGEN))
                    lists[0].push_back({ proto->RequiredLevel, proto->ItemLevel, spellId });
            }
            return lists;
        }();

        const Conjured* best = nullptr;
        for (const Conjured& entry : conjured[food ? 1 : 0])
            if (entry.requiredLevel <= level && (!best || std::tie(entry.requiredLevel, entry.itemLevel) > std::tie(best->requiredLevel, best->itemLevel)))
                best = &entry;
        return best ? best->spellId : 0;
    }

    // starts eating (food) or drinking: the bots' item cheat casts the use spell of the conjured food / drink of the
    // bot's level (the plain eat / drink cheat spell when there is none), otherwise a food / drink item of the bags
    // is used (was the legacy "food" / "drink" action); false when it could not start (moving, casting, nothing to
    // eat or drink)
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
            uint32 spellId = GetConjuredSpell(bot->GetLevel(), food);
            if (!spellId)
                spellId = food ? 24005 : 24355;
            const SpellEntry* pSpellInfo = sServerFacade.LookupSpellInfo(spellId);
            if (!pSpellInfo)
                return false;

            // cast by the core, not PlayerbotAI::CastSpell: that one stands a sitting bot up and fails, which would end
            // the food when the drink starts; the aura sits the bot down
            ai->Unmount();
            ai->InterruptSpell();
            return bot->CastSpell(bot, pSpellInfo, TRIGGERED_OLD_TRIGGERED) == SPELL_CAST_OK;
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

std::unique_ptr<STAI_TaskBase> Task_EatAndDrink::Create(const STAI_TaskConfig& config)
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

    // a started food / drink is not started again until its aura shows up (OnTick clears the flag then), so one item
    // is used per bite; a refused start (the bot was still moving) is retried
    if (!context.eatStarted && NeedsHealth(bot) && !IsEating(bot) && StartConsuming(context.ai, bot, true))
    {
        context.eatStarted = true;
        context.startMs = WorldTimer::getMSTime();
    }
    if (!context.drinkStarted && NeedsMana(bot) && !IsDrinking(bot) && StartConsuming(context.ai, bot, false))
    {
        context.drinkStarted = true;
        context.startMs = WorldTimer::getMSTime();
    }
}

EActionRunStatus Task_EatAndDrink::OnEnterState(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(Task_EatAndDrink::OnEnterState);
    StartLog(baseContext);
    Task_EatAndDrinkContext& context = ContextAs<Task_EatAndDrinkContext>(baseContext);
    context.startMs = WorldTimer::getMSTime();
    context.eatStarted = false;
    context.drinkStarted = false;
    context.consumed = false;
    Start(context);
    return EActionRunStatus::Running;
}

EActionRunStatus Task_EatAndDrink::OnTick(STAI_TaskContext& baseContext, uint32 elapsed, bool minimal) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(Task_EatAndDrink::OnTick);
    Task_EatAndDrinkContext& context = ContextAs<Task_EatAndDrinkContext>(baseContext);
    Player* bot = context.bot;

    if (!NeedsHealth(bot) && !NeedsMana(bot))
        return LogStatus(baseContext, EActionRunStatus::Succeeded);

    // the meal runs while a food / drink aura restores something still below its target; an aura that ends before
    // its target is followed by the next food / drink
    const bool eating = NeedsHealth(bot) && IsEating(bot);
    const bool drinking = NeedsMana(bot) && IsDrinking(bot);
    if (eating)
        context.eatStarted = false;
    if (drinking)
        context.drinkStarted = false;
    if (eating || drinking)
        context.consumed = true;

    Start(context);

    // running: an aura is up, or a food / drink just started and its aura is not applied yet (or the bot was still
    // moving at the start)
    if (eating || drinking || WorldTimer::getMSTimeDiff(context.startMs, WorldTimer::getMSTime()) < startTimeoutMs)
        return LogStatus(baseContext, EActionRunStatus::Running);

    // nothing left to eat or drink
    return LogStatus(baseContext, context.consumed ? EActionRunStatus::Succeeded : EActionRunStatus::Failed);
}

void Task_EatAndDrink::OnExitState(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(Task_EatAndDrink::OnExitState);
    // left while running: the run did not get done
    EndLog(baseContext, false);
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
