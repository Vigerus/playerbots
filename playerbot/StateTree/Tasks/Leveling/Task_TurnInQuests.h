#pragma once

#include "playerbot/StateTree/STAI_TaskFactory.h"

#include "Entities/ObjectGuid.h"

#include <string>

class Player;
class Quest;

namespace ai
{
    // runtime context of Task_TurnInQuests: the taker (bound) and how the run went
    struct Task_TurnInQuestsContext : public STAI_TaskContext
    {
        ObjectGuid taker;           // property "taker", e.g. bound to QuestProvider_GlobalTask::taker
        std::string result;         // the quests turned in and the rewards, in its History entry
    };

    // Turns in every quest of the log the creature takes that is done, as the client's Complete Quest button does
    // (CMSG_QUESTGIVER_REQUEST_REWARD, then CMSG_QUESTGIVER_CHOOSE_REWARD: the core checks the distance and the
    // objectives). A reward choice goes to the item the bot can use with the highest stat weight of its class and spec,
    // else the one that sells best. The tree's movement goal (range on the taker) brings the bot there:
    //   <Task class="Task_TurnInQuests">
    //     <Binding local="taker" external="Root|QuestProvider_GlobalTask::taker"/>
    //   </Task>
    // Not tickable: Succeeded when it turned a quest in, Failed otherwise; its History entry names the quests.
    class Task_TurnInQuests : public STAI_TaskBase
    {
    public:
        static void Validate(const STAI_TaskConfig& config) {}
        static std::unique_ptr<STAI_TaskBase> Create(const STAI_TaskConfig& config) { return std::make_unique<Task_TurnInQuests>(); }

        // the reward choice index for the bot (0 without a choice)
        static uint32 ChooseReward(Player* bot, const Quest* quest);

        virtual const char* GetNodeClass() const override { return "Task_TurnInQuests"; }
        inline static std::string task_name = "TurnInQuests";
        virtual std::string GetName() const override { return task_name; }

        virtual const STAI_TaskContextType& GetContextType() const override { return STAI_TaskContextType::Of<Task_TurnInQuestsContext>(); }

        virtual EActionRunStatus OnEnterState(STAI_TaskContext& context) const override;

        virtual const std::vector<STAI_PropertyDef>& GetProperties() const override;

        virtual std::string GetDebugInfo() const override { return "Turn in quests"; }
        virtual std::string GetActivityInfo(const STAI_TaskContext& context) const override;
    };
}
