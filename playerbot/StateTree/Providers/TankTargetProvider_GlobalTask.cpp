#include "TankTargetProvider_GlobalTask.h"

#include "playerbot/StateTree/STAI_SameMap.h"

#include "playerbot/PlayerbotAI.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/StateTree/Providers/STAI_TargetIcons.h"
#include "playerbot/StateTree/Providers/TeamProvider_GlobalTask.h"

#include "Entities/Player.h"
#include "Groups/Group.h"
#include "Maps/Map.h"

#include <algorithm>

using namespace ai;

namespace
{
    // how often the target is chosen
    constexpr uint32 chooseIntervalMs = 250;

    // the units of the group's marks for the icons, alive, in their order
    std::vector<Unit*> GetMarked(Player* bot, const std::vector<std::string>& icons)
    {
        std::vector<Unit*> units;
        Group* group = bot->GetGroup();
        if (!group)
            return units;
        for (const std::string& icon : icons)
        {
            const int index = STAI_GetRtiIndex(icon);
            const ObjectGuid guid = index < 0 ? ObjectGuid() : group->GetTargetIcon(index);
            Unit* unit = guid.IsEmpty() ? nullptr : bot->GetMap()->GetUnit(guid);
            if (unit && unit->IsInWorld() && sServerFacade.IsAlive(unit))
                units.push_back(unit);
        }
        return units;
    }

    struct Candidate
    {
        Unit* unit;
        Unit* victim;       // the member it attacks, null for none / not a member
        float distance;
    };
}

void TankTargetProvider_GlobalTask::Choose(TankTargetProvider_GlobalTaskContext& context)
{
    Player* bot = context.bot;
    const ObjectGuid before = context.target;
    const std::vector<ObjectGuid> looseBefore = context.loose;
    context.target.Clear();
    context.taunt.Clear();
    context.loose.clear();
    context.mainTank = false;
    if (!bot->IsInWorld() || !sServerFacade.IsAlive(bot))
    {
        if (!before.IsEmpty())
            context.onTargetChanged.Broadcast();
        return;
    }

    // the main tank: the first living tank of the group; the bot when it is that one (or alone)
    const ObjectGuid otherTank = TeamProvider_GlobalTask::FindMainTank(bot, context.roles);
    const std::vector<ObjectGuid>& tanks = context.roles.WithRole(BOT_ROLE_TANK);
    const auto botTank = std::find(tanks.begin(), tanks.end(), bot->GetObjectGuid());
    const auto firstOther = std::find(tanks.begin(), tanks.end(), otherTank);
    context.mainTank = otherTank.IsEmpty() || (botTank != tanks.end() && botTank < firstOther);
    Player* mainTank = context.mainTank ? bot : bot->GetMap()->GetPlayer(otherTank);

    // the units fighting the group that the bot may hold
    const std::vector<Unit*> ccMarks = GetMarked(bot, context.ai->GetTargetIcons().GetRtiCcList(bot));
    std::vector<Candidate> candidates;
    for (const auto& [guid, victims] : context.ai->GetTeamAttackers())
    {
        Unit* unit = context.ai->GetUnit(guid);
        if (!unit || unit->IsPlayer() || !sServerFacade.IsAlive(unit) || !IsOnSameMap(unit, bot))
            continue;
        if (!sServerFacade.IsHostileTo(bot, unit) || !bot->IsWithinDistInMap(unit, sPlayerbotAIConfig.sightDistance))
            continue;
        if (STAI_HasBreakableCC(unit, bot) || STAI_HasUnBreakableCC(unit, bot) || std::find(ccMarks.begin(), ccMarks.end(), unit) != ccMarks.end())
            continue;
        Unit* victim = unit->GetVictim();
        if (victim && (!victim->IsPlayer() || !bot->IsInGroup(static_cast<Player*>(victim), true)) && victim != bot)
            victim = nullptr;
        candidates.push_back({ unit, victim, bot->GetDistance(unit) });
    }
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) { return a.distance < b.distance; });

    // loose: attacking a member that is no tank, the healers' attackers first
    std::vector<Unit*> loose;
    for (int pass = 0; pass < 2; ++pass)
        for (const Candidate& candidate : candidates)
        {
            Unit* victim = candidate.victim;
            if (!victim || victim == bot || context.roles.HasRole(victim->GetObjectGuid(), BOT_ROLE_TANK))
                continue;
            if ((pass == 0) == context.roles.HasRole(victim->GetObjectGuid(), BOT_ROLE_HEALER))
                loose.push_back(candidate.unit);
        }
    for (Unit* unit : loose)
        context.loose.push_back(unit->GetObjectGuid());

    auto find = [&](ObjectGuid guid) -> const Candidate*
    {
        for (const Candidate& candidate : candidates)
            if (candidate.unit->GetObjectGuid() == guid)
                return &candidate;
        return nullptr;
    };
    const Candidate* current = before.IsEmpty() ? nullptr : find(before);
    Unit* chosen = nullptr;

    if (context.mainTank)
    {
        if (!loose.empty())
            chosen = loose.front();
        if (!chosen)
            for (Unit* marked : GetMarked(bot, context.ai->GetTargetIcons().GetRti(bot)))
                if (find(marked->GetObjectGuid()))
                {
                    chosen = marked;
                    break;
                }
        if (!chosen && current)
            chosen = current->unit;
        if (!chosen)
            for (const Candidate& candidate : candidates)
                if (candidate.victim != bot)
                {
                    chosen = candidate.unit;
                    break;
                }
        if (!chosen && !candidates.empty())
            chosen = candidates.front().unit;
    }
    else
    {
        // an off tank leaves the main tank's units alone
        for (Unit* unit : loose)
            if (unit->GetVictim() != mainTank)
            {
                chosen = unit;
                break;
            }
        if (!chosen && current && current->victim != mainTank)
            chosen = current->unit;
        if (!chosen)
            for (const Candidate& candidate : candidates)
                if (candidate.victim == bot)
                {
                    chosen = candidate.unit;
                    break;
                }
        if (!chosen && mainTank && mainTank->GetVictim() && find(mainTank->GetVictim()->GetObjectGuid()))
            chosen = mainTank->GetVictim();
    }

    if (chosen)
    {
        context.target = chosen->GetObjectGuid();
        // taunt only off members that are no tank (never off the main tank the off tank assists)
        Unit* victim = chosen->GetVictim();
        if (victim && victim != bot && victim->IsPlayer() && bot->IsInGroup(static_cast<Player*>(victim), true) &&
            !context.roles.HasRole(victim->GetObjectGuid(), BOT_ROLE_TANK))
            context.taunt = context.target;
    }

    if (context.target != before)
        context.onTargetChanged.Broadcast();
    for (ObjectGuid guid : context.loose)
        if (std::find(looseBefore.begin(), looseBefore.end(), guid) == looseBefore.end())
        {
            context.onLoose.Broadcast();
            break;
        }
}

EActionRunStatus TankTargetProvider_GlobalTask::OnEnter(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(TankTargetProvider_GlobalTask::OnEnter);
    TankTargetProvider_GlobalTaskContext& context = ContextAs<TankTargetProvider_GlobalTaskContext>(baseContext);
    context.target.Clear();
    context.taunt.Clear();
    context.loose.clear();
    context.sinceCheckMs = 0;
    context.roles.Subscribe(context.bot);
    context.roles.Update(context.bot, 0);
    Choose(context);
    return EActionRunStatus::Running;
}

EActionRunStatus TankTargetProvider_GlobalTask::OnTick(STAI_TaskContext& baseContext, uint32 elapsed, bool minimal) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(TankTargetProvider_GlobalTask::OnTick);
    TankTargetProvider_GlobalTaskContext& context = ContextAs<TankTargetProvider_GlobalTaskContext>(baseContext);
    context.roles.Update(context.bot, elapsed);
    context.sinceCheckMs += elapsed;
    if (context.sinceCheckMs >= chooseIntervalMs)
    {
        context.sinceCheckMs = 0;
        Choose(context);
    }
    return EActionRunStatus::Running;
}

void TankTargetProvider_GlobalTask::OnExit(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(TankTargetProvider_GlobalTask::OnExit);
    TankTargetProvider_GlobalTaskContext& context = ContextAs<TankTargetProvider_GlobalTaskContext>(baseContext);
    context.roles.Unsubscribe();
    context.target.Clear();
    context.taunt.Clear();
    context.loose.clear();
}

const std::vector<STAI_PropertyDef>& TankTargetProvider_GlobalTask::GetProperties() const
{
    static const std::vector<STAI_PropertyDef> properties =
    {
        STAI_PropertyDef::Of("target", &TankTargetProvider_GlobalTaskContext::target),
        STAI_PropertyDef::Of("taunt", &TankTargetProvider_GlobalTaskContext::taunt),
        STAI_PropertyDef::Of("loose", &TankTargetProvider_GlobalTaskContext::loose),
        STAI_PropertyDef::Of("mainTank", &TankTargetProvider_GlobalTaskContext::mainTank),
    };
    return properties;
}

const std::vector<STAI_DelegateDef>& TankTargetProvider_GlobalTask::GetDelegates() const
{
    static const std::vector<STAI_DelegateDef> delegates =
    {
        STAI_DelegateDef::Of("onTargetChanged", &TankTargetProvider_GlobalTaskContext::onTargetChanged),
        STAI_DelegateDef::Of("onLoose", &TankTargetProvider_GlobalTaskContext::onLoose),
    };
    return delegates;
}
