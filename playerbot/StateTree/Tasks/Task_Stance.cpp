#include "Task_Stance.h"

#include "playerbot/PlayerbotAI.h"
#include "playerbot/StateTree/Providers/WarriorProvider_GlobalTask.h"

#include "Entities/Player.h"

#include <stdexcept>

using namespace ai;

std::vector<std::string> Task_Stance::ParseStances(const STAI_TaskConfig& config)
{
    std::vector<std::string> stances;
    for (const auto& [key, child] : config)
    {
        if (key == "<xmlattr>" || key == "<xmlcomment>")
            continue;
        if (key != "Stance")
            throw std::runtime_error("unexpected <" + key + "> (expected <Stance value=\"...\"/>)");

        const std::string name = child.get<std::string>("<xmlattr>.value", "");
        if (!STAI_FindWarriorStance(name))
            throw std::runtime_error("<Stance value=\"" + name + "\">: expected battle, defensive or berserker");
        stances.push_back(name);
    }
    if (stances.empty())
        throw std::runtime_error("needs at least one <Stance value=\"...\"/>");
    return stances;
}

void Task_Stance::Validate(const STAI_TaskConfig& config)
{
    ParseStances(config);
}

std::unique_ptr<STAI_Task> Task_Stance::Create(const STAI_TaskConfig& config)
{
    return std::make_unique<Task_Stance>(ParseStances(config));
}

EActionRunStatus Task_Stance::OnEnter(STAI_TaskContext& context) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(Task_Stance::OnEnter);
    Player* bot = context.bot;

    // the first stance the bot knows
    const STAI_WarriorStance* wanted = nullptr;
    for (const std::string& name : stances)
    {
        const STAI_WarriorStance* stance = STAI_FindWarriorStance(name);
        if (stance && bot->HasSpell(stance->spellId))
        {
            wanted = stance;
            break;
        }
    }
    if (!wanted)
        return EActionRunStatus::Failed;
    if (STAI_GetWarriorStance(bot) == wanted)
        return EActionRunStatus::Succeeded;

    // instant, off the global cooldown
    StartLog(context);
    if (!context.ai->CastSpell(wanted->spellId, bot))
        return LogStatus(context, EActionRunStatus::Failed);
    return LogStatus(context, STAI_GetWarriorStance(bot) == wanted ? EActionRunStatus::Succeeded : EActionRunStatus::Failed);
}

std::string Task_Stance::GetDebugInfo() const
{
    std::string info = "Stance";
    for (const std::string& name : stances)
        info += " " + name;
    return info;
}

std::vector<uint32> Task_Stance::GetDebugSpells(const STAI_TaskContext& context) const
{
    std::vector<uint32> ids;
    for (const std::string& name : stances)
        if (const STAI_WarriorStance* stance = STAI_FindWarriorStance(name))
            ids.push_back(stance->spellId);
    return ids;
}
