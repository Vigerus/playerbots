#include "TotemProvider_GlobalTask.h"

#include "playerbot/PlayerbotAI.h"
#include "playerbot/PlayerbotTotems.h"
#include "playerbot/ServerFacade.h"

#include "Entities/Player.h"
#include "Entities/Totem.h"
#include "Spells/SpellMgr.h"

#include <algorithm>
#include <stdexcept>

using namespace ai;

namespace
{
    // how often the totems are checked
    constexpr uint32 checkIntervalMs = 250;
}

float TotemProvider_GlobalTask::ParseRange(const STAI_TaskConfig& config)
{
    const std::string value = STAI_TaskFactory::GetSetting(config, "Range").value_or("30");
    char* end = nullptr;
    const float range = std::strtof(value.c_str(), &end);
    if (value.empty() || *end || range <= 0.0f)
        throw std::runtime_error("<Range value=\"" + value + "\">: needs yards (more than 0)");
    return range;
}

void TotemProvider_GlobalTask::Validate(const STAI_TaskConfig& config)
{
    ParseRange(config);
}

std::unique_ptr<STAI_Task> TotemProvider_GlobalTask::Create(const STAI_TaskConfig& config)
{
    return std::make_unique<TotemProvider_GlobalTask>(ParseRange(config));
}

void TotemProvider_GlobalTask::Update(TotemProvider_GlobalTaskContext& context, bool first) const
{
    Player* bot = context.bot;
    PlayerbotTotems& totems = context.ai->GetTotems();
    const PlayerbotTotems::SetKey key = PlayerbotTotems::GetCurrentKey(bot, context.ai->GetMaster());
    const PlayerbotTotems::TotemSet& set = totems.GetSet(bot, key);
    context.mapId = key.first;
    context.stage = key.second;
    context.seenVersion = totems.GetVersion();

    // the spell per school: the highest rank of the selection the bot knows (a respec may have taken a talent totem)
    std::array<uint32, MAX_TOTEM_SLOT> spells;
    for (uint8 school = 0; school < MAX_TOTEM_SLOT; ++school)
        spells[school] = PlayerbotTotems::GetHighestKnownRank(bot, set[school]);

    const bool changed = spells[TOTEM_SLOT_FIRE] != context.fire || spells[TOTEM_SLOT_EARTH] != context.earth ||
        spells[TOTEM_SLOT_WATER] != context.water || spells[TOTEM_SLOT_AIR] != context.air;
    context.fire = spells[TOTEM_SLOT_FIRE];
    context.earth = spells[TOTEM_SLOT_EARTH];
    context.water = spells[TOTEM_SLOT_WATER];
    context.air = spells[TOTEM_SLOT_AIR];

    // the totems that are not up: none summoned in the school, another totem there, dead, or left behind
    std::vector<uint32> needs;
    if (bot->IsInWorld() && sServerFacade.IsAlive(bot))
    {
        for (uint8 school = 0; school < MAX_TOTEM_SLOT; ++school)
        {
            const uint32 spellId = spells[school];
            if (!spellId)
                continue;

            const Totem* totem = bot->GetTotem(TotemSlot(school));
            const bool up = totem && totem->IsAlive() &&
                sSpellMgr.GetFirstSpellInChain(totem->GetUInt32Value(UNIT_CREATED_BY_SPELL)) == sSpellMgr.GetFirstSpellInChain(spellId) &&
                bot->IsWithinDist(totem, range);
            if (!up)
                needs.push_back(spellId);
        }
    }

    bool missing = false;
    for (uint32 spellId : needs)
        missing |= std::find(context.needs.begin(), context.needs.end(), spellId) == context.needs.end();
    context.needs = std::move(needs);

    if (changed || first)
        context.onTotemsChanged.Broadcast();
    if (missing)
        context.onTotemMissing.Broadcast();
}

EActionRunStatus TotemProvider_GlobalTask::OnEnter(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(TotemProvider_GlobalTask::OnEnter);
    TotemProvider_GlobalTaskContext& context = ContextAs<TotemProvider_GlobalTaskContext>(baseContext);
    context.fire = context.earth = context.water = context.air = 0;
    context.needs.clear();
    context.sinceCheckMs = 0;
    Update(context, true);
    return EActionRunStatus::Running;
}

EActionRunStatus TotemProvider_GlobalTask::OnTick(STAI_TaskContext& baseContext, uint32 elapsed, bool minimal) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(TotemProvider_GlobalTask::OnTick);
    TotemProvider_GlobalTaskContext& context = ContextAs<TotemProvider_GlobalTaskContext>(baseContext);
    context.sinceCheckMs += elapsed;
    // a selection made in the addon is taken at once
    if (context.sinceCheckMs >= checkIntervalMs || context.seenVersion != context.ai->GetTotems().GetVersion())
    {
        context.sinceCheckMs = 0;
        Update(context, false);
    }
    return EActionRunStatus::Running;
}

const std::vector<STAI_PropertyDef>& TotemProvider_GlobalTask::GetProperties() const
{
    static const std::vector<STAI_PropertyDef> properties = {
        STAI_PropertyDef::Of("fire", &TotemProvider_GlobalTaskContext::fire),
        STAI_PropertyDef::Of("earth", &TotemProvider_GlobalTaskContext::earth),
        STAI_PropertyDef::Of("water", &TotemProvider_GlobalTaskContext::water),
        STAI_PropertyDef::Of("air", &TotemProvider_GlobalTaskContext::air),
        STAI_PropertyDef::Of("needs", &TotemProvider_GlobalTaskContext::needs),
        STAI_PropertyDef::Of("mapId", &TotemProvider_GlobalTaskContext::mapId),
        STAI_PropertyDef::Of("stage", &TotemProvider_GlobalTaskContext::stage),
    };
    return properties;
}

const std::vector<STAI_DelegateDef>& TotemProvider_GlobalTask::GetDelegates() const
{
    static const std::vector<STAI_DelegateDef> delegates = {
        STAI_DelegateDef::Of("onTotemsChanged", &TotemProvider_GlobalTaskContext::onTotemsChanged),
        STAI_DelegateDef::Of("onTotemMissing", &TotemProvider_GlobalTaskContext::onTotemMissing),
    };
    return delegates;
}

std::vector<uint32> TotemProvider_GlobalTask::GetDebugSpells(const STAI_TaskContext& baseContext) const
{
    const TotemProvider_GlobalTaskContext& context = static_cast<const TotemProvider_GlobalTaskContext&>(baseContext);
    std::vector<uint32> ids;
    for (uint32 spellId : { context.fire, context.earth, context.water, context.air })
        if (spellId)
            ids.push_back(spellId);
    return ids;
}
