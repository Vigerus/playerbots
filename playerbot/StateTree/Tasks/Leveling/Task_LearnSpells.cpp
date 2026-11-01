#include "Task_LearnSpells.h"

#include "playerbot/PlayerbotAI.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/StateTree/Providers/Leveling/TrainerProvider_GlobalTask.h"

#include "Entities/Creature.h"
#include "Server/WorldSession.h"

#include <sstream>

using namespace ai;

namespace
{
    constexpr uint32 learnTimeoutMs = 3000;     // the trainer's casts teaching the bought spells

    Creature* GetTrainer(const Task_LearnSpellsContext& context)
    {
        Player* bot = context.bot;
        return !context.trainer.IsEmpty() && bot->IsInWorld() ? bot->GetMap()->GetCreature(context.trainer) : nullptr;
    }
}

EActionRunStatus Task_LearnSpells::OnEnterState(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(Task_LearnSpells::OnEnterState);
    Task_LearnSpellsContext& context = ContextAs<Task_LearnSpellsContext>(baseContext);
    Player* bot = context.bot;
    Creature* trainer = GetTrainer(context);
    context.waitedMs = 0;
    context.result.clear();
    if (!trainer || !sServerFacade.IsAlive(bot))
        return EActionRunStatus::Failed;

    StartLog(context);
    std::ostringstream out;
    const std::vector<TrainerProvider_GlobalTask::Learnable> spells = TrainerProvider_GlobalTask::FindLearnable(bot, trainer->GetCreatureInfo());
    context.greenBefore = spells.size();
    if (!bot->GetNPCIfCanInteractWith(trainer->GetObjectGuid(), UNIT_NPC_FLAG_TRAINER))
    {
        out << "cannot interact, " << bot->GetDistance(trainer) << " yards";
        context.result = out.str();
        return LogStatus(context, EActionRunStatus::Failed);
    }

    const uint32 moneyBefore = bot->GetMoney();
    uint32 bought = 0;
    for (const TrainerProvider_GlobalTask::Learnable& spell : spells)
    {
        if (spell.cost > bot->GetMoney())
            break;

        WorldPacket buy(CMSG_TRAINER_BUY_SPELL, 8 + 4);
        buy << trainer->GetObjectGuid() << spell.spellId;
        bot->GetSession()->HandleTrainerBuySpellOpcode(buy);
        ++bought;
    }

    out << "bought " << bought << " of " << spells.size() << ", money " << moneyBefore << " -> " << bot->GetMoney();
    context.result = out.str();
    // nothing left to learn is done too: the Train subtree may finish
    if (spells.empty())
        return LogStatus(context, EActionRunStatus::Succeeded);
    if (bot->GetMoney() == moneyBefore)
        return LogStatus(context, EActionRunStatus::Failed);
    return LogStatus(context, EActionRunStatus::Running);
}

EActionRunStatus Task_LearnSpells::OnTick(STAI_TaskContext& baseContext, uint32 elapsed, bool minimal) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(Task_LearnSpells::OnTick);
    Task_LearnSpellsContext& context = ContextAs<Task_LearnSpellsContext>(baseContext);
    Creature* trainer = GetTrainer(context);
    if (!trainer)
        return LogStatus(context, EActionRunStatus::Failed);

    // a learned spell is no longer green
    const size_t green = TrainerProvider_GlobalTask::FindLearnable(context.bot, trainer->GetCreatureInfo()).size();
    if (green < context.greenBefore)
    {
        context.result += ", learned " + std::to_string(context.greenBefore - green);
        return LogStatus(context, EActionRunStatus::Succeeded);
    }

    context.waitedMs += elapsed;
    if (context.waitedMs < learnTimeoutMs)
        return EActionRunStatus::Running;

    context.result += ", nothing learned in 3s";
    return LogStatus(context, EActionRunStatus::Failed);
}

std::string Task_LearnSpells::GetActivityInfo(const STAI_TaskContext& baseContext) const
{
    const Task_LearnSpellsContext& context = static_cast<const Task_LearnSpellsContext&>(baseContext);
    return context.result.empty() ? GetDebugInfo() : GetDebugInfo() + ": " + context.result;
}

const std::vector<STAI_PropertyDef>& Task_LearnSpells::GetProperties() const
{
    static const std::vector<STAI_PropertyDef> properties = { STAI_PropertyDef::Of("trainer", &Task_LearnSpellsContext::trainer) };
    return properties;
}
