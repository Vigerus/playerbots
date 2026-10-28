#include "EncounterProvider_GlobalTask.h"

#include "playerbot/ServerFacade.h"
#include "playerbot/StateTree/Providers/CreatureProvider_GlobalTask.h"
#include "playerbot/StateTree/STAI_TaskFactory.h"

#include "Entities/Player.h"

#include <stdexcept>

using namespace ai;

namespace
{
    // how often the tactics are checked
    constexpr uint32 checkIntervalMs = 250;
}

std::vector<EncounterProvider_GlobalTask::Tactic> EncounterProvider_GlobalTask::ParseTactics(const STAI_TaskConfig& config)
{
    std::vector<Tactic> tactics;
    for (const auto& [key, child] : config)
    {
        if (key == "<xmlattr>" || key == "<xmlcomment>")
            continue;
        if (key != "Tactic")
            throw std::runtime_error("unexpected <" + key + "> (expected <Tactic name=\"...\" map=\"...\" entry=\"...\"/>)");

        Tactic tactic;
        tactic.name = child.get<std::string>("<xmlattr>.name", "");
        if (tactic.name.empty())
            throw std::runtime_error("<Tactic>: needs a name");

        const std::string mapText = child.get<std::string>("<xmlattr>.map", "");
        char* end = nullptr;
        tactic.mapId = uint32(std::strtoul(mapText.c_str(), &end, 10));
        if (mapText.empty() || *end)
            throw std::runtime_error("<Tactic name=\"" + tactic.name + "\" map=\"" + mapText + "\">: needs a map id");

        try
        {
            tactic.entries = CreatureProvider_GlobalTask::ParseEntries(child.get<std::string>("<xmlattr>.entry", ""));
        }
        catch (const std::runtime_error& error)
        {
            throw std::runtime_error("<Tactic name=\"" + tactic.name + "\">: " + error.what());
        }

        const std::string rangeText = child.get<std::string>("<xmlattr>.range", "40");
        tactic.range = std::strtof(rangeText.c_str(), &end);
        if (rangeText.empty() || *end || tactic.range <= 0.0f)
            throw std::runtime_error("<Tactic name=\"" + tactic.name + "\" range=\"" + rangeText + "\">: needs yards (more than 0)");

        tactics.push_back(std::move(tactic));
    }
    if (tactics.empty())
        throw std::runtime_error("needs one or more <Tactic name=\"...\" map=\"...\" entry=\"...\"/>");
    return tactics;
}

void EncounterProvider_GlobalTask::Validate(const STAI_TaskConfig& config)
{
    ParseTactics(config);
}

std::unique_ptr<STAI_Task> EncounterProvider_GlobalTask::Create(const STAI_TaskConfig& config)
{
    return std::make_unique<EncounterProvider_GlobalTask>(ParseTactics(config));
}

void EncounterProvider_GlobalTask::Update(EncounterProvider_GlobalTaskContext& context) const
{
    Player* bot = context.bot;
    std::string tactic;
    if (bot->IsInWorld() && sServerFacade.IsAlive(bot))
        for (const Tactic& candidate : tactics)
            if (bot->GetMapId() == candidate.mapId && !CreatureProvider_GlobalTask::FindCreatures(bot, candidate.entries, candidate.range).empty())
            {
                tactic = candidate.name;
                break;
            }

    if (tactic != context.tactic)
    {
        context.tactic = tactic;
        context.onTacticChanged.Broadcast();
    }
}

EActionRunStatus EncounterProvider_GlobalTask::OnEnter(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(EncounterProvider_GlobalTask::OnEnter);
    EncounterProvider_GlobalTaskContext& context = ContextAs<EncounterProvider_GlobalTaskContext>(baseContext);
    context.tactic.clear();
    context.sinceCheckMs = 0;
    Update(context);
    return EActionRunStatus::Running;
}

EActionRunStatus EncounterProvider_GlobalTask::OnTick(STAI_TaskContext& baseContext, uint32 elapsed, bool minimal) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(EncounterProvider_GlobalTask::OnTick);
    EncounterProvider_GlobalTaskContext& context = ContextAs<EncounterProvider_GlobalTaskContext>(baseContext);
    context.sinceCheckMs += elapsed;
    if (context.sinceCheckMs >= checkIntervalMs)
    {
        context.sinceCheckMs = 0;
        Update(context);
    }
    return EActionRunStatus::Running;
}

const std::vector<STAI_PropertyDef>& EncounterProvider_GlobalTask::GetProperties() const
{
    static const std::vector<STAI_PropertyDef> properties = { STAI_PropertyDef::Of("tactic", &EncounterProvider_GlobalTaskContext::tactic) };
    return properties;
}

const std::vector<STAI_DelegateDef>& EncounterProvider_GlobalTask::GetDelegates() const
{
    static const std::vector<STAI_DelegateDef> delegates = { STAI_DelegateDef::Of("onTacticChanged", &EncounterProvider_GlobalTaskContext::onTacticChanged) };
    return delegates;
}
