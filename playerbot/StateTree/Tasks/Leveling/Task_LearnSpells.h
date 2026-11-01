#pragma once

#include "playerbot/StateTree/STAI_TaskFactory.h"

#include "Entities/ObjectGuid.h"

#include <string>

namespace ai
{
    // runtime context of Task_LearnSpells: the trainer (bound) and the run
    struct Task_LearnSpellsContext : public STAI_TaskContext
    {
        ObjectGuid trainer;         // property "trainer", e.g. bound to TrainerProvider_GlobalTask::trainer
        size_t greenBefore = 0;     // the trainer's green spells before buying
        uint32 waitedMs = 0;
        std::string result;         // how the run went, in its History entry
    };

    // Buys every spell the trainer teaches the bot now that its money pays for, cheapest first, as the trainer window's
    // Train button does (CMSG_TRAINER_BUY_SPELL: the core checks the distance, the level and the money). The trainer
    // teaches a bought spell by casting it on the bot, so the task waits until they landed. The tree's movement goal
    // (range on the trainer) brings the bot there:
    //   <Task class="Task_LearnSpells">
    //     <Binding local="trainer" external="Root|TrainerProvider_GlobalTask::trainer"/>
    //   </Task>
    // Succeeded once a bought spell is learned or when there is nothing left to learn, Failed when nothing could be
    // bought (out of reach, no money) or nothing was learned within 3s; its History entry tells which.
    class Task_LearnSpells : public STAI_TaskBase
    {
    public:
        static void Validate(const STAI_TaskConfig& config) {}
        static std::unique_ptr<STAI_TaskBase> Create(const STAI_TaskConfig& config) { return std::make_unique<Task_LearnSpells>(); }

        virtual const char* GetNodeClass() const override { return "Task_LearnSpells"; }
        inline static std::string task_name = "LearnSpells";
        virtual std::string GetName() const override { return task_name; }

        virtual const STAI_TaskContextType& GetContextType() const override { return STAI_TaskContextType::Of<Task_LearnSpellsContext>(); }

        virtual EActionRunStatus OnEnterState(STAI_TaskContext& context) const override;
        virtual EActionRunStatus OnTick(STAI_TaskContext& context, uint32 elapsed, bool minimal) const override;
        virtual bool ShouldCallTick() const override { return true; }

        virtual const std::vector<STAI_PropertyDef>& GetProperties() const override;

        virtual std::string GetDebugInfo() const override { return "Learn spells at the trainer"; }
        virtual std::string GetActivityInfo(const STAI_TaskContext& context) const override;
    };
}
