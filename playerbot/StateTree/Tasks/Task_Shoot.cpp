#include "Task_Shoot.h"

#include "playerbot/PlayerbotAI.h"
#include "playerbot/ServerFacade.h"

#include "Entities/Item.h"
#include "Maps/Map.h"
#include "Spells/Spell.h"

using namespace ai;

namespace
{
    // Shoot: the wand's auto-repeat spell
    constexpr uint32 shootSpellId = 5019;
    // the shooting stopped this long (a cast in between, the target out of sight): started again, else failed
    constexpr uint32 restartAfterMs = 2000;

    bool HasWand(Player* bot)
    {
        const Item* ranged = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_RANGED);
        return ranged && ranged->GetProto()->Class == ITEM_CLASS_WEAPON && ranged->GetProto()->SubClass == ITEM_SUBCLASS_WEAPON_WAND;
    }

    bool IsShooting(Player* bot)
    {
        const Spell* spell = bot->GetCurrentSpell(CURRENT_AUTOREPEAT_SPELL);
        return spell && spell->m_spellInfo->Id == shootSpellId;
    }

    Unit* GetTarget(const Task_ShootContext& context)
    {
        if (context.target.IsEmpty())
            return nullptr;
        Unit* target = context.bot->GetMap()->GetUnit(context.target);
        return target && target->IsInWorld() && sServerFacade.IsAlive(target) ? target : nullptr;
    }
}

bool Task_Shoot::Start(Task_ShootContext& context, Unit* target)
{
    Player* bot = context.bot;
    if (!HasWand(bot) || !bot->HasSpell(shootSpellId))
        return false;
    if (!context.ai->CanCastSpell(shootSpellId, target, 0, true))
        return false;
    if (!context.ai->CastSpell(shootSpellId, target))
        return false;

    context.shooting = target->GetObjectGuid();
    context.sinceShotMs = 0;
    return true;
}

EActionRunStatus Task_Shoot::OnEnterState(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(Task_Shoot::OnEnterState);
    Task_ShootContext& context = ContextAs<Task_ShootContext>(baseContext);
    context.shooting.Clear();
    context.sinceShotMs = 0;
    StartLog(context);

    Unit* target = GetTarget(context);
    if (!target)
        return LogStatus(context, EActionRunStatus::Succeeded);

    // already shooting at it (the node was entered again)
    if (IsShooting(context.bot) && context.bot->GetSelectionGuid() == target->GetObjectGuid())
    {
        context.shooting = target->GetObjectGuid();
        return LogStatus(context, EActionRunStatus::Running);
    }

    return LogStatus(context, Start(context, target) ? EActionRunStatus::Running : EActionRunStatus::Failed);
}

EActionRunStatus Task_Shoot::OnTick(STAI_TaskContext& baseContext, uint32 elapsed, bool minimal) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(Task_Shoot::OnTick);
    Task_ShootContext& context = ContextAs<Task_ShootContext>(baseContext);

    // the target died or another one was chosen: the node selects again
    Unit* target = GetTarget(context);
    if (!target || target->GetObjectGuid() != context.shooting)
    {
        EndLog(context, true);
        return EActionRunStatus::Succeeded;
    }

    if (IsShooting(context.bot))
    {
        context.sinceShotMs = 0;
        return LogStatus(context, EActionRunStatus::Running);
    }

    context.sinceShotMs += elapsed;
    if (context.sinceShotMs < restartAfterMs)
        return LogStatus(context, EActionRunStatus::Running);
    if (context.bot->IsNonMeleeSpellCasted(false, false, true))
        return LogStatus(context, EActionRunStatus::Running);
    return LogStatus(context, Start(context, target) ? EActionRunStatus::Running : EActionRunStatus::Failed);
}

void Task_Shoot::OnExitState(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(Task_Shoot::OnExitState);
    Task_ShootContext& context = ContextAs<Task_ShootContext>(baseContext);
    if (IsShooting(context.bot))
        context.bot->InterruptSpell(CURRENT_AUTOREPEAT_SPELL);
    // left while shooting: the wand did its job until then
    EndLog(context, !context.shooting.IsEmpty());
}

const std::vector<STAI_PropertyDef>& Task_Shoot::GetProperties() const
{
    static const std::vector<STAI_PropertyDef> properties = { STAI_PropertyDef::Of("target", &Task_ShootContext::target) };
    return properties;
}

std::string Task_Shoot::GetDebugInfo() const
{
    return "Shoot " + STAI_SpellMarkup(shootSpellId);
}
