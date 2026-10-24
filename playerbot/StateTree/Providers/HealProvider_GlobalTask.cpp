#include "HealProvider_GlobalTask.h"

#include "playerbot/StateTree/STAI_SameMap.h"

#include "playerbot/ServerFacade.h"
#include "playerbot/StateTree/Providers/TeamProvider_GlobalTask.h"
#include "playerbot/StateTree/STAI_TaskFactory.h"

#include "Entities/Corpse.h"
#include "Entities/Player.h"
#include "Globals/ObjectAccessor.h"
#include "Groups/Group.h"
#include "Maps/Map.h"

#include <algorithm>
#include <stdexcept>

using namespace ai;

namespace
{
    // health is read this often
    constexpr uint32 checkIntervalMs = 200;

    float ParseNumber(const STAI_TaskConfig& config, const std::string& name, float fallback)
    {
        const std::optional<std::string> text = STAI_TaskFactory::GetSetting(config, name);
        if (!text)
            return fallback;
        char* end = nullptr;
        const float value = std::strtof(text->c_str(), &end);
        if (text->empty() || *end || value < 0.0f)
            throw std::runtime_error("<" + name + " value=\"" + *text + "\">: needs a number (0 or more)");
        return value;
    }

    // the bot and the members of its group on its map, dead or alive
    std::vector<Player*> GetMembersOnMap(Player* bot)
    {
        std::vector<Player*> members;
        if (Group* group = bot->GetGroup())
        {
            for (GroupReference* itr = group->GetFirstMember(); itr; itr = itr->next())
            {
                Player* member = itr->getSource();
                if (IsOnSameMap(member, bot))
                    members.push_back(member);
            }
        }
        if (std::find(members.begin(), members.end(), bot) == members.end())
            members.push_back(bot);
        return members;
    }

    // a hostile unit attacks the member (it is the attacker's victim)
    bool IsAttacked(Player* bot, Player* member)
    {
        for (Unit* attacker : member->getAttackers())
            if (attacker && attacker->IsInWorld() && sServerFacade.IsAlive(attacker) && attacker->GetVictim() == member &&
                sServerFacade.IsHostileTo(bot, attacker))
                return true;
        return false;
    }

    // a dead body a resurrection spell can bring back: not released (no ghost), nobody offered a resurrection yet
    bool CanBeResurrected(Player* member)
    {
        return member->GetDeathState() == CORPSE && !member->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST) && !member->isRessurectRequested();
    }

    // the corpse on the bot's map of a released member (a ghost, wherever it is) that a resurrection cast at the
    // corpse brings back; null otherwise
    Corpse* GetResurrectableCorpse(Player* bot, Player* member)
    {
        if (!member->IsInWorld() || !member->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST) || member->isRessurectRequested())
            return nullptr;

        Corpse* corpse = sObjectAccessor.GetCorpseForPlayerGUID(member->GetObjectGuid());
        if (!corpse || corpse->GetType() == CORPSE_BONES || corpse->GetMapId() != bot->GetMapId() || corpse->GetInstanceId() != bot->GetInstanceId())
            return nullptr;

        // the map's own object, in the world
        corpse = bot->GetMap()->GetCorpse(corpse->GetObjectGuid());
        return corpse && corpse->IsInWorld() ? corpse : nullptr;
    }

    bool HasNew(const std::vector<ObjectGuid>& now, const std::vector<ObjectGuid>& before)
    {
        for (ObjectGuid guid : now)
            if (std::find(before.begin(), before.end(), guid) == before.end())
                return true;
        return false;
    }
}

HealProvider_GlobalTask::Settings HealProvider_GlobalTask::ParseSettings(const STAI_TaskConfig& config)
{
    Settings settings;
    settings.critical = ParseNumber(config, "Critical", settings.critical);
    settings.low = ParseNumber(config, "Low", settings.low);
    settings.medium = ParseNumber(config, "Medium", settings.medium);
    settings.almostFull = ParseNumber(config, "AlmostFull", settings.almostFull);
    settings.tankRange = ParseNumber(config, "TankRange", settings.tankRange);
    if (!(settings.critical <= settings.low && settings.low <= settings.medium && settings.medium <= settings.almostFull && settings.almostFull <= 100.0f))
        throw std::runtime_error("the bands need Critical <= Low <= Medium <= AlmostFull <= 100");
    return settings;
}

void HealProvider_GlobalTask::Validate(const STAI_TaskConfig& config)
{
    ParseSettings(config);
}

std::unique_ptr<STAI_Task> HealProvider_GlobalTask::Create(const STAI_TaskConfig& config)
{
    return std::make_unique<HealProvider_GlobalTask>(ParseSettings(config));
}

void HealProvider_GlobalTask::Evaluate(HealProvider_GlobalTaskContext& context) const
{
    Player* bot = context.bot;
    const std::vector<ObjectGuid> beforeAlmostFull = context.almostFull;
    const std::vector<ObjectGuid> beforeAggroed = context.aggroed;
    const std::vector<ObjectGuid> beforeDead = context.dead;
    const size_t beforeCritical = context.critical.size(), beforeLow = context.low.size(), beforeMedium = context.medium.size();
    const bool beforeTankOutOfRange = context.tankOutOfRange;

    context.critical.clear();
    context.low.clear();
    context.medium.clear();
    context.almostFull.clear();
    context.healTarget.Clear();
    context.lowestPct = 100.0f;
    context.aggroed.clear();
    context.selfAttacked = false;
    context.dead.clear();
    context.resurrectTarget.Clear();
    context.mainTank.Clear();
    context.tankOutOfRange = false;

    // a dead bot helps nobody
    if (!bot->IsInWorld() || !sServerFacade.IsAlive(bot))
        return;

    struct Wounded { Player* member; float pct; bool tank; };
    std::vector<Wounded> wounded;
    for (Player* member : GetMembersOnMap(bot))
    {
        if (!sServerFacade.IsAlive(member))
        {
            if (member != bot && CanBeResurrected(member))
                context.dead.push_back(member->GetObjectGuid());
            else if (Corpse* corpse = member != bot ? GetResurrectableCorpse(bot, member) : nullptr)
                context.dead.push_back(corpse->GetObjectGuid());
            continue;
        }

        const bool tank = context.roles.HasRole(member->GetObjectGuid(), BOT_ROLE_TANK);
        if (IsAttacked(bot, member))
        {
            if (member == bot)
                context.selfAttacked = true;
            if (!tank)
                context.aggroed.push_back(member->GetObjectGuid());
        }

        // dueling members are not healed
        if (member->duel && member->duel->opponent)
            continue;

        const float pct = member->GetHealthPercent();
        if (pct < settings.almostFull)
            wounded.push_back({ member, pct, tank });
    }

    // released members on other maps (a ghost at the graveyard) whose corpse lies on the bot's map
    if (Group* group = bot->GetGroup())
    {
        for (GroupReference* itr = group->GetFirstMember(); itr; itr = itr->next())
        {
            Player* member = itr->getSource();
            if (member && member != bot && member->IsInWorld() && !IsOnSameMap(member, bot))
                if (Corpse* corpse = GetResurrectableCorpse(bot, member))
                    context.dead.push_back(corpse->GetObjectGuid());
        }
    }

    std::stable_sort(wounded.begin(), wounded.end(), [](const Wounded& a, const Wounded& b)
    {
        return a.pct != b.pct ? a.pct < b.pct : a.tank && !b.tank;
    });
    for (const Wounded& w : wounded)
    {
        const ObjectGuid guid = w.member->GetObjectGuid();
        if (w.pct < settings.critical)
            context.critical.push_back(guid);
        if (w.pct < settings.low)
            context.low.push_back(guid);
        if (w.pct < settings.medium)
            context.medium.push_back(guid);
        context.almostFull.push_back(guid);
    }
    if (!wounded.empty())
    {
        context.healTarget = wounded.front().member->GetObjectGuid();
        context.lowestPct = wounded.front().pct;
    }

    if (!context.dead.empty())
        context.resurrectTarget = context.dead.front();

    // the bot takes care of itself first
    auto self = std::find(context.aggroed.begin(), context.aggroed.end(), bot->GetObjectGuid());
    if (self != context.aggroed.end())
        std::rotate(context.aggroed.begin(), self, self + 1);

    context.mainTank = TeamProvider_GlobalTask::FindMainTank(bot, context.roles);
    if (Player* tank = context.mainTank.IsEmpty() ? nullptr : bot->GetMap()->GetPlayer(context.mainTank))
        context.tankOutOfRange = sServerFacade.GetDistance2d(bot, tank) > settings.tankRange || !bot->IsWithinLOSInMap(tank);

    // a member wounded for the first time, or more members in a lower band than before
    if (HasNew(context.almostFull, beforeAlmostFull) || context.critical.size() > beforeCritical || context.low.size() > beforeLow ||
        context.medium.size() > beforeMedium)
        context.onNeedsHeal.Broadcast();
    if (HasNew(context.aggroed, beforeAggroed))
        context.onAggro.Broadcast();
    if (HasNew(context.dead, beforeDead))
        context.onDead.Broadcast();
    if (context.tankOutOfRange && !beforeTankOutOfRange)
        context.onTankOutOfRange.Broadcast();
}

EActionRunStatus HealProvider_GlobalTask::OnEnter(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(HealProvider_GlobalTask::OnEnter);
    HealProvider_GlobalTaskContext& context = ContextAs<HealProvider_GlobalTaskContext>(baseContext);
    context.critical.clear();
    context.low.clear();
    context.medium.clear();
    context.almostFull.clear();
    context.aggroed.clear();
    context.dead.clear();
    context.tankOutOfRange = false;
    context.sinceCheckMs = 0;
    context.roles.Subscribe(context.bot);
    context.roles.Update(context.bot, 0);
    Evaluate(context);
    return EActionRunStatus::Running;
}

EActionRunStatus HealProvider_GlobalTask::OnTick(STAI_TaskContext& baseContext, uint32 elapsed, bool minimal) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(HealProvider_GlobalTask::OnTick);
    HealProvider_GlobalTaskContext& context = ContextAs<HealProvider_GlobalTaskContext>(baseContext);
    context.roles.Update(context.bot, elapsed);
    context.sinceCheckMs += elapsed;
    if (context.sinceCheckMs >= checkIntervalMs)
    {
        context.sinceCheckMs = 0;
        Evaluate(context);
    }
    return EActionRunStatus::Running;
}

void HealProvider_GlobalTask::OnExit(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(HealProvider_GlobalTask::OnExit);
    HealProvider_GlobalTaskContext& context = ContextAs<HealProvider_GlobalTaskContext>(baseContext);
    context.roles.Unsubscribe();
}

const std::vector<STAI_PropertyDef>& HealProvider_GlobalTask::GetProperties() const
{
    static const std::vector<STAI_PropertyDef> properties =
    {
        STAI_PropertyDef::Of("critical", &HealProvider_GlobalTaskContext::critical),
        STAI_PropertyDef::Of("low", &HealProvider_GlobalTaskContext::low),
        STAI_PropertyDef::Of("medium", &HealProvider_GlobalTaskContext::medium),
        STAI_PropertyDef::Of("almostFull", &HealProvider_GlobalTaskContext::almostFull),
        STAI_PropertyDef::Of("healTarget", &HealProvider_GlobalTaskContext::healTarget),
        STAI_PropertyDef::Of("lowestPct", &HealProvider_GlobalTaskContext::lowestPct),
        STAI_PropertyDef::Of("aggroed", &HealProvider_GlobalTaskContext::aggroed),
        STAI_PropertyDef::Of("selfAttacked", &HealProvider_GlobalTaskContext::selfAttacked),
        STAI_PropertyDef::Of("dead", &HealProvider_GlobalTaskContext::dead),
        STAI_PropertyDef::Of("resurrectTarget", &HealProvider_GlobalTaskContext::resurrectTarget),
        STAI_PropertyDef::Of("mainTank", &HealProvider_GlobalTaskContext::mainTank),
        STAI_PropertyDef::Of("tankOutOfRange", &HealProvider_GlobalTaskContext::tankOutOfRange),
    };
    return properties;
}

const std::vector<STAI_DelegateDef>& HealProvider_GlobalTask::GetDelegates() const
{
    static const std::vector<STAI_DelegateDef> delegates =
    {
        STAI_DelegateDef::Of("onNeedsHeal", &HealProvider_GlobalTaskContext::onNeedsHeal),
        STAI_DelegateDef::Of("onAggro", &HealProvider_GlobalTaskContext::onAggro),
        STAI_DelegateDef::Of("onDead", &HealProvider_GlobalTaskContext::onDead),
        STAI_DelegateDef::Of("onTankOutOfRange", &HealProvider_GlobalTaskContext::onTankOutOfRange),
    };
    return delegates;
}
