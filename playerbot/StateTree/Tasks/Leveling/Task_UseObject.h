#pragma once

#include "playerbot/StateTree/STAI_TaskFactory.h"

#include "Entities/ObjectGuid.h"

#include <string>

namespace ai
{
    // runtime context of Task_UseObject: the game object (bound) and the run
    struct Task_UseObjectContext : public STAI_TaskContext
    {
        ObjectGuid target;          // property "target", e.g. bound to QuestProvider_GlobalTask::objectTarget
        uint32 waitedMs = 0;
        bool cast = false;          // opened by a spell (a lock), not used directly
        std::string result;
    };

    // Uses a quest game object as the client does: a locked one by the bot's opening spell for its lock (cast at it),
    // else CMSG_GAMEOBJ_USE; a chest's loot window is then emptied like a corpse's (Task_Loot). The tree's movement goal
    // (range, interact) brings the bot there:
    //   <Task class="Task_UseObject">
    //     <Binding local="target" external="Root|QuestProvider_GlobalTask::objectTarget"/>
    //   </Task>
    // Succeeded once the loot is taken (a chest) or the object is used (a goober), Failed when it cannot be used or no
    // loot opened within 8s.
    class Task_UseObject : public STAI_TaskBase
    {
    public:
        static void Validate(const STAI_TaskConfig& config) {}
        static std::unique_ptr<STAI_TaskBase> Create(const STAI_TaskConfig& config) { return std::make_unique<Task_UseObject>(); }

        virtual const char* GetNodeClass() const override { return "Task_UseObject"; }
        inline static std::string task_name = "UseObject";
        virtual std::string GetName() const override { return task_name; }

        virtual const STAI_TaskContextType& GetContextType() const override { return STAI_TaskContextType::Of<Task_UseObjectContext>(); }

        virtual EActionRunStatus OnEnterState(STAI_TaskContext& context) const override;
        virtual EActionRunStatus OnTick(STAI_TaskContext& context, uint32 elapsed, bool minimal) const override;
        virtual bool ShouldCallTick() const override { return true; }

        virtual const std::vector<STAI_PropertyDef>& GetProperties() const override;

        virtual std::string GetDebugInfo() const override { return "Use the object"; }
        virtual std::string GetActivityInfo(const STAI_TaskContext& context) const override;
    };
}
