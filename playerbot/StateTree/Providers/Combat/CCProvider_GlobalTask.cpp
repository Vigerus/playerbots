#include "CCProvider_GlobalTask.h"

#include "playerbot/PlayerbotAI.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/StateTree/STAI_TaskFactory.h"

#include "Groups/Group.h"
#include "Maps/Map.h"
#include "Spells/Spell.h"
#include "Spells/SpellAuras.h"
#include "Spells/SpellMgr.h"

#include <algorithm>
#include <sstream>
#include <stdexcept>

using namespace ai;

namespace
{
    // how often the marks are checked
    constexpr uint32 checkIntervalMs = 250;
    // onCCNeeded fires again this often while the same target still needs crowd control
    constexpr uint32 refireIntervalMs = 1000;
}

CCProvider_GlobalTask::Settings CCProvider_GlobalTask::ParseSettings(const STAI_TaskConfig& config)
{
    Settings settings;
    for (const auto& [key, child] : config)
    {
        if (key == "<xmlattr>" || key == "<xmlcomment>")
            continue;
        if (key != "Aura" && key != "Unmarked")
            throw std::runtime_error("unexpected <" + key + "> (expected <Aura spellid=\"...\"/> or <Unmarked spellid=\"...\"/>)");

        const std::string idText = child.get<std::string>("<xmlattr>.spellid", "");
        const uint32 spellId = uint32(std::strtoul(idText.c_str(), nullptr, 10));
        if (!spellId || !sSpellTemplate.LookupEntry<SpellEntry>(spellId))
            throw std::runtime_error("<" + key + " spellid=\"" + idText + "\">: unknown spell");

        const uint32 firstSpellId = sSpellMgr.GetFirstSpellInChain(spellId);
        if (key == "Unmarked")
            settings.unmarkedSpell = firstSpellId;
        else if (std::find(settings.auras.begin(), settings.auras.end(), firstSpellId) == settings.auras.end())
            settings.auras.push_back(firstSpellId);
    }
    return settings;
}

void CCProvider_GlobalTask::Validate(const STAI_TaskConfig& config)
{
    ParseSettings(config);
}

std::unique_ptr<STAI_TaskBase> CCProvider_GlobalTask::Create(const STAI_TaskConfig& config)
{
    return std::make_unique<CCProvider_GlobalTask>(ParseSettings(config));
}

std::vector<Unit*> CCProvider_GlobalTask::GetMarkedUnits(const STAI_TaskContext& context, const std::vector<std::string>& icons)
{
    std::vector<Unit*> units;
    Player* bot = context.bot;
    Group* group = bot->GetGroup();
    if (!group || !bot->IsInWorld())
        return units;

    for (const std::string& icon : icons)
    {
        const int index = STAI_GetRtiIndex(icon);
        if (index < 0)
            continue;

        const ObjectGuid guid = group->GetTargetIcon(index);
        if (guid.IsEmpty())
            continue;

        Unit* unit = bot->GetMap()->GetUnit(guid);
        if (unit && unit->IsInWorld() && sServerFacade.IsAlive(unit))
            units.push_back(unit);
    }
    return units;
}

bool CCProvider_GlobalTask::IsCrowdControlled(Unit* unit, Player* bot) const
{
    if (STAI_HasBreakableCC(unit, bot) || STAI_HasUnBreakableCC(unit, bot))
        return true;

    for (const auto& [spellId, holder] : unit->GetSpellAuraHolderMap())
        if (!holder->IsDeleted() && std::find(auras.begin(), auras.end(), sSpellMgr.GetFirstSpellInChain(spellId)) != auras.end())
            return true;
    return false;
}

ObjectGuid CCProvider_GlobalTask::FindTarget(const CCProvider_GlobalTaskContext& context) const
{
    Player* bot = context.bot;
    if (!bot->IsInWorld() || !sServerFacade.IsAlive(bot))
        return ObjectGuid();

    // the group kills the first living rti target first: that one is never crowd-controlled
    const std::vector<Unit*> killOrder = GetMarkedUnits(context, context.ai->GetTargetIcons().GetRti(context.bot));
    Unit* killFirst = killOrder.empty() ? nullptr : killOrder.front();

    for (Unit* unit : GetMarkedUnits(context, context.ai->GetTargetIcons().GetRtiCcList(context.bot)))
    {
        if (unit == killFirst || unit->IsPlayer())
            continue;
        if (!sServerFacade.IsHostileTo(bot, unit) || !bot->IsWithinDistInMap(unit, sPlayerbotAIConfig.sightDistance))
            continue;
        if (IsCrowdControlled(unit, bot))
            continue;
        return unit->GetObjectGuid();
    }
    return unmarkedSpell ? FindUnmarkedTarget(context, killFirst) : ObjectGuid();
}

ObjectGuid CCProvider_GlobalTask::FindUnmarkedTarget(const CCProvider_GlobalTaskContext& context, Unit* killFirst) const
{
    Player* bot = context.bot;
    const SpellEntry* spellInfo = sSpellTemplate.LookupEntry<SpellEntry>(unmarkedSpell);
    Group* group = bot->GetGroup();
    auto isMarked = [group](Unit* unit)
    {
        for (int index = 0; group && index < TARGET_ICON_COUNT; ++index)
            if (group->GetTargetIcon(index) == unit->GetObjectGuid())
                return true;
        return false;
    };

    // the units fighting the bot: its threat list (a polymorphed one stays on it) and its attackers
    std::vector<Unit*> enemies;
    auto add = [&](Unit* unit)
    {
        if (unit && unit->IsInWorld() && sServerFacade.IsAlive(unit) && !unit->IsPlayer() && sServerFacade.IsHostileTo(bot, unit) &&
            std::find(enemies.begin(), enemies.end(), unit) == enemies.end())
            enemies.push_back(unit);
    };
    for (HostileReference* ref = bot->getHostileRefManager().getFirst(); ref; ref = ref->next())
        add(ref->getSource()->getOwner());
    for (Unit* attacker : bot->getAttackers())
        add(attacker);

    // one at a time: a unit under the spell already is the one
    std::vector<Unit*> free;
    for (Unit* unit : enemies)
    {
        for (const auto& [spellId, holder] : unit->GetSpellAuraHolderMap())
            if (!holder->IsDeleted() && sSpellMgr.GetFirstSpellInChain(spellId) == unmarkedSpell)
                return ObjectGuid();
        if (!IsCrowdControlled(unit, bot))
            free.push_back(unit);
    }
    if (free.size() < 2)
        return ObjectGuid();

    Unit* victim = bot->GetVictim();
    Unit* best = nullptr;
    for (Unit* unit : free)
    {
        if (unit == victim || unit == killFirst || unit->GetObjectGuid() == bot->GetSelectionGuid() || isMarked(unit) ||
            !Spell::CheckTargetCreatureType(unit, spellInfo))
            continue;
        if (!bot->IsWithinDistInMap(unit, sPlayerbotAIConfig.sightDistance))
            continue;
        if (!best || unit->GetHealth() > best->GetHealth())
            best = unit;
    }
    return best ? best->GetObjectGuid() : ObjectGuid();
}

void CCProvider_GlobalTask::Update(CCProvider_GlobalTaskContext& context) const
{
    const ObjectGuid target = FindTarget(context);
    const ObjectGuid previous = context.whoNeeds.empty() ? ObjectGuid() : context.whoNeeds.front();

    context.whoNeeds.clear();
    if (!target.IsEmpty())
        context.whoNeeds.push_back(target);

    // a new target to crowd-control, or the same one still not controlled a while later
    if (target.IsEmpty())
        return;
    if (target != previous || context.sinceFireMs >= refireIntervalMs)
    {
        context.sinceFireMs = 0;
        context.onCCNeeded.Broadcast();
    }
}

EActionRunStatus CCProvider_GlobalTask::OnEnterState(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(CCProvider_GlobalTask::OnEnterState);
    CCProvider_GlobalTaskContext& context = ContextAs<CCProvider_GlobalTaskContext>(baseContext);
    context.whoNeeds.clear();
    context.sinceCheckMs = 0;
    context.sinceFireMs = 0;
    context.iconsVersion = context.ai->GetTargetIcons().GetVersion();
    Update(context);
    return EActionRunStatus::Running;
}

EActionRunStatus CCProvider_GlobalTask::OnTick(STAI_TaskContext& baseContext, uint32 elapsed, bool minimal) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(CCProvider_GlobalTask::OnTick);
    CCProvider_GlobalTaskContext& context = ContextAs<CCProvider_GlobalTaskContext>(baseContext);
    context.sinceCheckMs += elapsed;
    context.sinceFireMs += elapsed;
    // the bot's rti cc icons changed (the rti command, the inspect addon): check now
    const uint32 iconsVersion = context.ai->GetTargetIcons().GetVersion();
    if (context.sinceCheckMs >= checkIntervalMs || iconsVersion != context.iconsVersion)
    {
        context.iconsVersion = iconsVersion;
        context.sinceCheckMs = 0;
        Update(context);
    }
    return EActionRunStatus::Running;
}

const std::vector<STAI_PropertyDef>& CCProvider_GlobalTask::GetProperties() const
{
    static const std::vector<STAI_PropertyDef> properties = { STAI_PropertyDef::Of("whoNeeds", &CCProvider_GlobalTaskContext::whoNeeds) };
    return properties;
}

const std::vector<STAI_DelegateDef>& CCProvider_GlobalTask::GetDelegates() const
{
    static const std::vector<STAI_DelegateDef> delegates = { STAI_DelegateDef::Of("onCCNeeded", &CCProvider_GlobalTaskContext::onCCNeeded) };
    return delegates;
}

