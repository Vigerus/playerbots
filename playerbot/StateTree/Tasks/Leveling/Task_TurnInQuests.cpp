#include "Task_TurnInQuests.h"

#include "playerbot/ItemQualifier.h"
#include "playerbot/PlayerbotAI.h"
#include "playerbot/RandomItemMgr.h"
#include "playerbot/ServerFacade.h"

#include "Entities/Creature.h"
#include "Globals/ObjectMgr.h"
#include "Quests/QuestDef.h"
#include "Server/WorldSession.h"

#include <tuple>

using namespace ai;

uint32 Task_TurnInQuests::ChooseReward(Player* bot, const Quest* quest)
{
    // usable first, then stat weight, then the vendor price
    uint32 best = 0;
    std::tuple<bool, uint32, uint32> bestScore{ false, 0, 0 };
    for (uint32 i = 0; i < quest->GetRewChoiceItemsCount() && i < QUEST_REWARD_CHOICES_COUNT; ++i)
    {
        const ItemPrototype* proto = sObjectMgr.GetItemPrototype(quest->RewChoiceItemId[i]);
        if (!proto)
            continue;

        const bool usable = proto->InventoryType != INVTYPE_NON_EQUIP && bot->CanUseItem(proto, false) == EQUIP_ERR_OK;
        ItemQualifier qualifier(proto->ItemId);
        const uint32 weight = usable ? sRandomItemMgr.ItemStatWeight(bot, qualifier) : 0;
        const std::tuple<bool, uint32, uint32> score{ usable, weight, proto->SellPrice };
        if (i == 0 || bestScore < score)
        {
            best = i;
            bestScore = score;
        }
    }
    return best;
}

EActionRunStatus Task_TurnInQuests::OnEnterState(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(Task_TurnInQuests::OnEnterState);
    Task_TurnInQuestsContext& context = ContextAs<Task_TurnInQuestsContext>(baseContext);
    Player* bot = context.bot;
    context.result.clear();
    Creature* taker = !context.taker.IsEmpty() && bot->IsInWorld() ? bot->GetMap()->GetCreature(context.taker) : nullptr;
    if (!taker || !sServerFacade.IsAlive(bot))
        return EActionRunStatus::Failed;

    StartLog(context);
    if (!bot->GetNPCIfCanInteractWith(taker->GetObjectGuid(), UNIT_NPC_FLAG_QUESTGIVER))
    {
        context.result = "cannot interact, " + std::to_string(int(bot->GetDistance(taker))) + " yards";
        return LogStatus(context, EActionRunStatus::Failed);
    }

    // the log first: turning in changes it
    std::vector<uint32> questIds;
    for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
        if (const uint32 questId = bot->GetQuestSlotQuestId(slot))
            if (taker->HasInvolvedQuest(questId))
                questIds.push_back(questId);

    WorldSession* session = bot->GetSession();
    uint32 turnedIn = 0;
    for (uint32 questId : questIds)
    {
        const Quest* quest = sObjectMgr.GetQuestTemplate(questId);
        if (!quest)
            continue;

        // completes a quest without objectives (talk to)
        WorldPacket request(CMSG_QUESTGIVER_REQUEST_REWARD, 8 + 4);
        request << taker->GetObjectGuid() << questId;
        session->HandleQuestgiverRequestRewardOpcode(request);
        if (bot->GetQuestStatus(questId) != QUEST_STATUS_COMPLETE)
            continue;

        const uint32 reward = ChooseReward(bot, quest);
        WorldPacket choose(CMSG_QUESTGIVER_CHOOSE_REWARD, 8 + 4 + 4);
        choose << taker->GetObjectGuid() << questId << reward;
        session->HandleQuestgiverChooseRewardOpcode(choose);
        if (!bot->GetQuestRewardStatus(questId))
        {
            context.result += (context.result.empty() ? "" : ", ") + quest->GetTitle() + " (not rewarded)";
            continue;
        }

        context.result += (context.result.empty() ? "" : ", ") + quest->GetTitle();
        if (quest->GetRewChoiceItemsCount())
            if (const ItemPrototype* proto = sObjectMgr.GetItemPrototype(quest->RewChoiceItemId[reward]))
                context.result += " -> " + std::string(proto->Name1);
        ++turnedIn;
    }

    if (!turnedIn)
    {
        if (context.result.empty())
            context.result = "nothing to turn in";
        return LogStatus(context, EActionRunStatus::Failed);
    }
    return LogStatus(context, EActionRunStatus::Succeeded);
}

std::string Task_TurnInQuests::GetActivityInfo(const STAI_TaskContext& baseContext) const
{
    const Task_TurnInQuestsContext& context = static_cast<const Task_TurnInQuestsContext&>(baseContext);
    return context.result.empty() ? GetDebugInfo() : GetDebugInfo() + ": " + context.result;
}

const std::vector<STAI_PropertyDef>& Task_TurnInQuests::GetProperties() const
{
    static const std::vector<STAI_PropertyDef> properties = { STAI_PropertyDef::Of("taker", &Task_TurnInQuestsContext::taker) };
    return properties;
}
