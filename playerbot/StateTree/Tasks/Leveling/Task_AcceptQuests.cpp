#include "Task_AcceptQuests.h"

#include "playerbot/PlayerbotAI.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/StateTree/Providers/Leveling/QuestProvider_GlobalTask.h"

#include "Entities/Creature.h"
#include "Globals/ObjectMgr.h"
#include "Quests/QuestDef.h"
#include "Server/WorldSession.h"

#include <stdexcept>

using namespace ai;

namespace
{
    uint32 ParseLevels(const STAI_TaskConfig& config, const std::string& attribute, uint32 fallback)
    {
        const std::optional<std::string> text = STAI_TaskFactory::GetSetting(config, "Levels", attribute);
        if (!text)
            return fallback;
        char* end = nullptr;
        const long value = std::strtol(text->c_str(), &end, 10);
        if (end == text->c_str() || *end || value < 0)
            throw std::runtime_error("<Levels " + attribute + "=\"" + *text + "\">: expected a number of 0 or more");
        return uint32(value);
    }
}

void Task_AcceptQuests::Validate(const STAI_TaskConfig& config)
{
    ParseLevels(config, "below", 5);
    ParseLevels(config, "above", 2);
}

std::unique_ptr<STAI_TaskBase> Task_AcceptQuests::Create(const STAI_TaskConfig& config)
{
    return std::make_unique<Task_AcceptQuests>(ParseLevels(config, "below", 5), ParseLevels(config, "above", 2));
}

EActionRunStatus Task_AcceptQuests::OnEnterState(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(Task_AcceptQuests::OnEnterState);
    Task_AcceptQuestsContext& context = ContextAs<Task_AcceptQuestsContext>(baseContext);
    Player* bot = context.bot;
    context.result.clear();
    Creature* giver = !context.giver.IsEmpty() && bot->IsInWorld() ? bot->GetMap()->GetCreature(context.giver) : nullptr;
    if (!giver || !sServerFacade.IsAlive(bot))
        return EActionRunStatus::Failed;

    StartLog(context);
    if (!bot->GetNPCIfCanInteractWith(giver->GetObjectGuid(), UNIT_NPC_FLAG_QUESTGIVER))
    {
        context.result = "cannot interact, " + std::to_string(int(bot->GetDistance(giver))) + " yards";
        return LogStatus(context, EActionRunStatus::Failed);
    }

    uint32 accepted = 0;
    const QuestRelationsMapBounds bounds = sObjectMgr.GetCreatureQuestRelationsMapBounds(giver->GetEntry());
    for (auto itr = bounds.first; itr != bounds.second; ++itr)
    {
        const Quest* quest = sObjectMgr.GetQuestTemplate(itr->second);
        if (!QuestProvider_GlobalTask::IsWanted(bot, quest, levelsBelow, levelsAbove))
            continue;

        WorldPacket accept(CMSG_QUESTGIVER_ACCEPT_QUEST, 8 + 4);
        accept << giver->GetObjectGuid() << quest->GetQuestId();
        bot->GetSession()->HandleQuestgiverAcceptQuestOpcode(accept);
        if (bot->GetQuestStatus(quest->GetQuestId()) == QUEST_STATUS_NONE)
            continue;

        context.result += (accepted ? ", " : "") + quest->GetTitle();
        ++accepted;
    }

    if (!accepted)
    {
        context.result = "nothing accepted";
        return LogStatus(context, EActionRunStatus::Failed);
    }
    return LogStatus(context, EActionRunStatus::Succeeded);
}

std::string Task_AcceptQuests::GetActivityInfo(const STAI_TaskContext& baseContext) const
{
    const Task_AcceptQuestsContext& context = static_cast<const Task_AcceptQuestsContext&>(baseContext);
    return context.result.empty() ? GetDebugInfo() : GetDebugInfo() + ": " + context.result;
}

const std::vector<STAI_PropertyDef>& Task_AcceptQuests::GetProperties() const
{
    static const std::vector<STAI_PropertyDef> properties = { STAI_PropertyDef::Of("giver", &Task_AcceptQuestsContext::giver) };
    return properties;
}
