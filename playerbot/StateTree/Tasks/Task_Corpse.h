#pragma once

#include "playerbot/StateTree/STAI_TaskFactory.h"

namespace ai
{
    // Releases the dead bot's spirit, like the client's "Release Spirit" button (CMSG_REPOP_REQUEST): the ghost
    // appears at the graveyard (outside the dungeon for a death inside one), the corpse stays:
    //   <Task class="Task_ReleaseSpirit"/>
    // Not tickable: Succeeded in OnEnter once the bot is a ghost (also when it already was), Failed while it cannot
    // release yet (alive, still dying).
    class Task_ReleaseSpirit : public STAI_Task
    {
    public:
        static void Validate(const STAI_TaskConfig& config) {}
        static std::unique_ptr<STAI_Task> Create(const STAI_TaskConfig& config) { return std::make_unique<Task_ReleaseSpirit>(); }

        virtual const char* GetTaskClass() const override { return "Task_ReleaseSpirit"; }
        inline static std::string task_name = "ReleaseSpirit";
        virtual std::string GetName() const override { return task_name; }

        virtual EActionRunStatus OnEnter(STAI_TaskContext& context) const override;

        virtual std::string GetDebugInfo() const override { return "Release spirit"; }
    };

    // Comes back at the body, like the client's "Resurrect" button of a ghost at its corpse (CMSG_RECLAIM_CORPSE):
    //   <Node name="Reclaim Corpse">
    //     <Conditions> <Condition binding="Root|UIInteractionProvider_GlobalTask::canReclaimCorpse"/> </Conditions>
    //     <Task class="Task_ReclaimCorpse"/>
    //   </Node>
    // Not tickable: Succeeded in OnEnter once the bot is alive (also when it already was), Failed while it cannot
    // (not a ghost, too far from the corpse, the reclaim delay is not over).
    class Task_ReclaimCorpse : public STAI_Task
    {
    public:
        static void Validate(const STAI_TaskConfig& config) {}
        static std::unique_ptr<STAI_Task> Create(const STAI_TaskConfig& config) { return std::make_unique<Task_ReclaimCorpse>(); }

        virtual const char* GetTaskClass() const override { return "Task_ReclaimCorpse"; }
        inline static std::string task_name = "ReclaimCorpse";
        virtual std::string GetName() const override { return task_name; }

        virtual EActionRunStatus OnEnter(STAI_TaskContext& context) const override;

        virtual std::string GetDebugInfo() const override { return "Reclaim corpse"; }
    };
}
