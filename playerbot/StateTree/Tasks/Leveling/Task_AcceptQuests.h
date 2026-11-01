#pragma once

#include "playerbot/StateTree/STAI_TaskFactory.h"

#include "Entities/ObjectGuid.h"

#include <string>

namespace ai
{
    // runtime context of Task_AcceptQuests: the giver (bound) and how the run went
    struct Task_AcceptQuestsContext : public STAI_TaskContext
    {
        ObjectGuid giver;           // property "giver", e.g. bound to QuestProvider_GlobalTask::giver
        std::string result;         // the accepted quests, in its History entry
    };

    // Accepts every quest of the giver the bot wants (QuestProvider_GlobalTask::IsWanted) while the log has room, as the
    // quest window's Accept button does (CMSG_QUESTGIVER_ACCEPT_QUEST: the core checks the distance and CanTakeQuest).
    // The tree's movement goal (range on the giver) brings the bot there:
    //   <Task class="Task_AcceptQuests">
    //     <Levels below="5" above="2"/>      as QuestProvider_GlobalTask's, default 5 below and 2 above
    //     <Binding local="giver" external="Root|QuestProvider_GlobalTask::giver"/>
    //   </Task>
    // Not tickable: Succeeded when it accepted a quest, Failed otherwise; its History entry names the quests.
    class Task_AcceptQuests : public STAI_TaskBase
    {
    public:
        Task_AcceptQuests(uint32 levelsBelow, uint32 levelsAbove) : levelsBelow(levelsBelow), levelsAbove(levelsAbove) {}

        static void Validate(const STAI_TaskConfig& config);
        static std::unique_ptr<STAI_TaskBase> Create(const STAI_TaskConfig& config);

        virtual const char* GetNodeClass() const override { return "Task_AcceptQuests"; }
        inline static std::string task_name = "AcceptQuests";
        virtual std::string GetName() const override { return task_name; }

        virtual const STAI_TaskContextType& GetContextType() const override { return STAI_TaskContextType::Of<Task_AcceptQuestsContext>(); }

        virtual EActionRunStatus OnEnterState(STAI_TaskContext& context) const override;

        virtual const std::vector<STAI_PropertyDef>& GetProperties() const override;

        virtual std::string GetDebugInfo() const override { return "Accept quests"; }
        virtual std::string GetActivityInfo(const STAI_TaskContext& context) const override;

    private:
        uint32 levelsBelow;
        uint32 levelsAbove;
    };
}
