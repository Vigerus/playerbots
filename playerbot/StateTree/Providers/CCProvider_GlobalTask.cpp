#include "CCProvider_GlobalTask.h"

#include "playerbot/PlayerbotAI.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/StateTree/STAI_TaskFactory.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/values/PossibleAttackTargetsValue.h"
#include "playerbot/strategy/values/RtiTargetValue.h"

#include "Groups/Group.h"
#include "Maps/Map.h"
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

std::vector<uint32> CCProvider_GlobalTask::ParseAuras(const STAI_TaskConfig& config)
{
    std::vector<uint32> auras;
    for (const auto& [key, child] : config)
    {
        if (key == "<xmlattr>" || key == "<xmlcomment>")
            continue;
        if (key != "Aura")
            throw std::runtime_error("unexpected <" + key + "> (expected <Aura spellid=\"...\"/>)");

        const std::string idText = child.get<std::string>("<xmlattr>.spellid", "");
        const uint32 spellId = uint32(std::strtoul(idText.c_str(), nullptr, 10));
        if (!spellId || !sSpellTemplate.LookupEntry<SpellEntry>(spellId))
            throw std::runtime_error("<Aura spellid=\"" + idText + "\">: unknown spell");

        const uint32 firstSpellId = sSpellMgr.GetFirstSpellInChain(spellId);
        if (std::find(auras.begin(), auras.end(), firstSpellId) == auras.end())
            auras.push_back(firstSpellId);
    }
    return auras;
}

void CCProvider_GlobalTask::Validate(const STAI_TaskConfig& config)
{
    ParseAuras(config);
}

std::unique_ptr<STAI_Task> CCProvider_GlobalTask::Create(const STAI_TaskConfig& config)
{
    return std::make_unique<CCProvider_GlobalTask>(ParseAuras(config));
}

std::vector<Unit*> CCProvider_GlobalTask::GetMarkedUnits(const STAI_TaskContext& context, const std::string& rtiValue)
{
    std::vector<Unit*> units;
    Player* bot = context.bot;
    Group* group = bot->GetGroup();
    if (!group || !bot->IsInWorld())
        return units;

    const std::vector<std::string> icons = context.ai->GetAiObjectContext()->GetValue<std::vector<std::string>>(rtiValue)->Get();
    for (const std::string& icon : icons)
    {
        const int index = RtiTargetValue::GetRtiIndex(icon);
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
    if (PossibleAttackTargetsValue::HasBreakableCC(unit, bot) || PossibleAttackTargetsValue::HasUnBreakableCC(unit, bot))
        return true;

    for (const auto& [spellId, holder] : unit->GetSpellAuraHolderMap())
        if (!holder->IsDeleted() && std::find(auras.begin(), auras.end(), sSpellMgr.GetFirstSpellInChain(spellId)) != auras.end())
            return true;
    return false;
}

ObjectGuid CCProvider_GlobalTask::FindTarget(const STAI_TaskContext& context) const
{
    Player* bot = context.bot;
    if (!bot->IsInWorld() || !sServerFacade.IsAlive(bot))
        return ObjectGuid();

    // the group kills the first living rti target first: that one is never crowd-controlled
    const std::vector<Unit*> killOrder = GetMarkedUnits(context, "rti");
    Unit* killFirst = killOrder.empty() ? nullptr : killOrder.front();

    for (Unit* unit : GetMarkedUnits(context, "rti cc"))
    {
        if (unit == killFirst || unit->IsPlayer())
            continue;
        if (!sServerFacade.IsHostileTo(bot, unit) || !bot->IsWithinDistInMap(unit, sPlayerbotAIConfig.sightDistance))
            continue;
        if (IsCrowdControlled(unit, bot))
            continue;
        return unit->GetObjectGuid();
    }
    return ObjectGuid();
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

void CCProvider_GlobalTask::OnEnter(STAI_TaskContext& baseContext) const
{
    CCProvider_GlobalTaskContext& context = ContextAs<CCProvider_GlobalTaskContext>(baseContext);
    context.whoNeeds.clear();
    context.sinceCheckMs = 0;
    context.sinceFireMs = 0;
    Update(context);
}

EActionRunStatus CCProvider_GlobalTask::OnTick(STAI_TaskContext& baseContext, uint32 elapsed, bool minimal) const
{
    CCProvider_GlobalTaskContext& context = ContextAs<CCProvider_GlobalTaskContext>(baseContext);
    context.sinceCheckMs += elapsed;
    context.sinceFireMs += elapsed;
    if (context.sinceCheckMs >= checkIntervalMs)
    {
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

std::string CCProvider_GlobalTask::GetDebugInfo(const STAI_TaskContext& baseContext) const
{
    const CCProvider_GlobalTaskContext& context = ContextAs<CCProvider_GlobalTaskContext>(baseContext);
    if (context.whoNeeds.empty())
        return "nothing to crowd-control";

    std::ostringstream out;
    if (Unit* unit = context.bot->IsInWorld() ? context.bot->GetMap()->GetUnit(context.whoNeeds.front()) : nullptr)
        out << "crowd-control " << unit->GetName();
    else
        out << "crowd-control target";
    return out.str();
}
