#include "WarriorProvider_GlobalTask.h"

#include "playerbot/ServerFacade.h"

#include "Entities/Player.h"
#include "Maps/Map.h"

using namespace ai;

namespace
{
    // how often rage, stance and the reactive abilities are checked
    constexpr uint32 checkIntervalMs = 100;

    const STAI_WarriorStance stances[] =
    {
        { "battle", 2457, FORM_BATTLESTANCE },
        { "defensive", 71, FORM_DEFENSIVESTANCE },
        { "berserker", 2458, FORM_BERSERKERSTANCE },
    };
}

const STAI_WarriorStance* ai::STAI_FindWarriorStance(const std::string& name)
{
    for (const STAI_WarriorStance& stance : stances)
        if (name == stance.name)
            return &stance;
    return nullptr;
}

const STAI_WarriorStance* ai::STAI_GetWarriorStance(const Player* player)
{
    const uint8 form = uint8(player->GetShapeshiftForm());
    for (const STAI_WarriorStance& stance : stances)
        if (form == stance.form)
            return &stance;
    return nullptr;
}

void WarriorProvider_GlobalTask::Update(WarriorProvider_GlobalTaskContext& context)
{
    Player* bot = context.bot;
    const bool hadRevenge = context.revenge;
    const bool hadOverpower = context.overpower;

    context.rage = bot->GetPower(POWER_RAGE) / 10;
    const STAI_WarriorStance* stance = STAI_GetWarriorStance(bot);
    context.stance = stance ? stance->name : "";
    context.knowsDefensive = bot->HasSpell(STAI_FindWarriorStance("defensive")->spellId);
    context.knowsBerserker = bot->HasSpell(STAI_FindWarriorStance("berserker")->spellId);
    context.revenge = sServerFacade.IsAlive(bot) && bot->HasAuraState(AURA_STATE_DEFENSE);

    // the combo point of a dodge, on a target still alive
    context.overpower = false;
    context.overpowerTarget.Clear();
    if (bot->IsInWorld() && bot->GetComboPoints() > 0)
    {
        Unit* target = bot->GetMap()->GetUnit(bot->GetComboTargetGuid());
        if (target && target->IsAlive())
        {
            context.overpower = true;
            context.overpowerTarget = target->GetObjectGuid();
        }
    }

    if ((context.revenge && !hadRevenge) || (context.overpower && !hadOverpower))
        context.onReactive.Broadcast();
}

EActionRunStatus WarriorProvider_GlobalTask::OnEnterState(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(WarriorProvider_GlobalTask::OnEnterState);
    WarriorProvider_GlobalTaskContext& context = ContextAs<WarriorProvider_GlobalTaskContext>(baseContext);
    context.revenge = false;
    context.overpower = false;
    context.sinceCheckMs = 0;
    Update(context);
    return EActionRunStatus::Running;
}

EActionRunStatus WarriorProvider_GlobalTask::OnTick(STAI_TaskContext& baseContext, uint32 elapsed, bool minimal) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(WarriorProvider_GlobalTask::OnTick);
    WarriorProvider_GlobalTaskContext& context = ContextAs<WarriorProvider_GlobalTaskContext>(baseContext);
    context.sinceCheckMs += elapsed;
    if (context.sinceCheckMs >= checkIntervalMs)
    {
        context.sinceCheckMs = 0;
        Update(context);
    }
    return EActionRunStatus::Running;
}

const std::vector<STAI_PropertyDef>& WarriorProvider_GlobalTask::GetProperties() const
{
    static const std::vector<STAI_PropertyDef> properties =
    {
        STAI_PropertyDef::Of("rage", &WarriorProvider_GlobalTaskContext::rage),
        STAI_PropertyDef::Of("stance", &WarriorProvider_GlobalTaskContext::stance),
        STAI_PropertyDef::Of("revenge", &WarriorProvider_GlobalTaskContext::revenge),
        STAI_PropertyDef::Of("overpower", &WarriorProvider_GlobalTaskContext::overpower),
        STAI_PropertyDef::Of("overpowerTarget", &WarriorProvider_GlobalTaskContext::overpowerTarget),
        STAI_PropertyDef::Of("knowsDefensive", &WarriorProvider_GlobalTaskContext::knowsDefensive),
        STAI_PropertyDef::Of("knowsBerserker", &WarriorProvider_GlobalTaskContext::knowsBerserker),
    };
    return properties;
}

const std::vector<STAI_DelegateDef>& WarriorProvider_GlobalTask::GetDelegates() const
{
    static const std::vector<STAI_DelegateDef> delegates = { STAI_DelegateDef::Of("onReactive", &WarriorProvider_GlobalTaskContext::onReactive) };
    return delegates;
}
