#include "CreatureProvider_GlobalTask.h"

#include "playerbot/ServerFacade.h"
#include "playerbot/StateTree/STAI_TaskFactory.h"

#include "Entities/Creature.h"
#include "Entities/Player.h"
#include "Globals/ObjectMgr.h"
#include "Grids/CellImpl.h"
#include "Grids/GridNotifiers.h"
#include "Grids/GridNotifiersImpl.h"
#include "Maps/Map.h"

#include <algorithm>
#include <sstream>
#include <stdexcept>

using namespace ai;

namespace
{
    // how often the creatures are looked for
    constexpr uint32 checkIntervalMs = 250;
    constexpr float defaultRange = 40.0f;
}

std::vector<uint32> CreatureProvider_GlobalTask::ParseEntries(const std::string& text)
{
    std::vector<uint32> entries;
    std::stringstream stream(text);
    std::string item;
    while (std::getline(stream, item, ','))
    {
        const uint32 entry = uint32(std::strtoul(item.c_str(), nullptr, 10));
        if (!entry || !ObjectMgr::GetCreatureTemplate(entry))
            throw std::runtime_error("entry \"" + item + "\": unknown creature");
        if (std::find(entries.begin(), entries.end(), entry) == entries.end())
            entries.push_back(entry);
    }
    if (entries.empty())
        throw std::runtime_error("entry=\"" + text + "\": needs one or more creature entries");
    return entries;
}

float CreatureProvider_GlobalTask::ParseRange(const STAI_TaskConfig& config)
{
    const std::string value = STAI_TaskFactory::GetSetting(config, "Range").value_or("40");
    char* end = nullptr;
    const float range = std::strtof(value.c_str(), &end);
    if (value.empty() || *end || range <= 0.0f)
        throw std::runtime_error("<Range value=\"" + value + "\">: needs yards (more than 0)");
    return range;
}

std::vector<uint32> CreatureProvider_GlobalTask::ParseCreatures(const STAI_TaskConfig& config)
{
    std::vector<uint32> entries;
    for (const auto& [key, child] : config)
    {
        if (key == "<xmlattr>" || key == "<xmlcomment>" || key == "Range")
            continue;
        if (key != "Creature")
            throw std::runtime_error("unexpected <" + key + "> (expected <Creature entry=\"...\"/> or <Range value=\"...\"/>)");

        for (uint32 entry : ParseEntries(child.get<std::string>("<xmlattr>.entry", "")))
            if (std::find(entries.begin(), entries.end(), entry) == entries.end())
                entries.push_back(entry);
    }
    if (entries.empty())
        throw std::runtime_error("needs one or more <Creature entry=\"...\"/>");
    return entries;
}

void CreatureProvider_GlobalTask::Validate(const STAI_TaskConfig& config)
{
    ParseCreatures(config);
    ParseRange(config);
}

std::unique_ptr<STAI_Task> CreatureProvider_GlobalTask::Create(const STAI_TaskConfig& config)
{
    return std::make_unique<CreatureProvider_GlobalTask>(ParseCreatures(config), ParseRange(config));
}

std::vector<Creature*> CreatureProvider_GlobalTask::FindCreatures(Player* bot, const std::vector<uint32>& entries, float range)
{
    std::vector<Creature*> creatures;
    if (!bot->IsInWorld())
        return creatures;

    CreatureList found;
    MaNGOS::AllCreaturesMatchingOneEntryInRange check(bot, entries, range);
    MaNGOS::CreatureListSearcher<MaNGOS::AllCreaturesMatchingOneEntryInRange> searcher(found, check);
    Cell::VisitGridObjects(bot, searcher, range);

    for (Creature* creature : found)
        if (creature->IsInWorld() && sServerFacade.IsAlive(creature) && sServerFacade.IsHostileTo(bot, creature))
            creatures.push_back(creature);

    std::sort(creatures.begin(), creatures.end(), [bot](Creature* a, Creature* b) { return bot->GetDistance(a) < bot->GetDistance(b); });
    return creatures;
}

void CreatureProvider_GlobalTask::Update(CreatureProvider_GlobalTaskContext& context) const
{
    const std::vector<ObjectGuid> previous = std::move(context.units);
    context.units.clear();
    if (sServerFacade.IsAlive(context.bot))
        for (Creature* creature : FindCreatures(context.bot, entries, range))
            context.units.push_back(creature->GetObjectGuid());

    context.closest = context.units.empty() ? ObjectGuid() : context.units.front();
    context.count = uint32(context.units.size());

    const bool appeared = std::any_of(context.units.begin(), context.units.end(), [&previous](const ObjectGuid& guid)
    {
        return std::find(previous.begin(), previous.end(), guid) == previous.end();
    });
    if (appeared)
        context.onAppeared.Broadcast();
    else if (context.units.empty() && !previous.empty())
        context.onGone.Broadcast();
}

EActionRunStatus CreatureProvider_GlobalTask::OnEnter(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(CreatureProvider_GlobalTask::OnEnter);
    CreatureProvider_GlobalTaskContext& context = ContextAs<CreatureProvider_GlobalTaskContext>(baseContext);
    context.units.clear();
    context.closest = ObjectGuid();
    context.count = 0;
    context.sinceCheckMs = 0;
    Update(context);
    return EActionRunStatus::Running;
}

EActionRunStatus CreatureProvider_GlobalTask::OnTick(STAI_TaskContext& baseContext, uint32 elapsed, bool minimal) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(CreatureProvider_GlobalTask::OnTick);
    CreatureProvider_GlobalTaskContext& context = ContextAs<CreatureProvider_GlobalTaskContext>(baseContext);
    context.sinceCheckMs += elapsed;
    if (context.sinceCheckMs >= checkIntervalMs)
    {
        context.sinceCheckMs = 0;
        Update(context);
    }
    return EActionRunStatus::Running;
}

const std::vector<STAI_PropertyDef>& CreatureProvider_GlobalTask::GetProperties() const
{
    static const std::vector<STAI_PropertyDef> properties = {
        STAI_PropertyDef::Of("units", &CreatureProvider_GlobalTaskContext::units),
        STAI_PropertyDef::Of("closest", &CreatureProvider_GlobalTaskContext::closest),
        STAI_PropertyDef::Of("count", &CreatureProvider_GlobalTaskContext::count),
    };
    return properties;
}

const std::vector<STAI_DelegateDef>& CreatureProvider_GlobalTask::GetDelegates() const
{
    static const std::vector<STAI_DelegateDef> delegates = {
        STAI_DelegateDef::Of("onAppeared", &CreatureProvider_GlobalTaskContext::onAppeared),
        STAI_DelegateDef::Of("onGone", &CreatureProvider_GlobalTaskContext::onGone),
    };
    return delegates;
}
