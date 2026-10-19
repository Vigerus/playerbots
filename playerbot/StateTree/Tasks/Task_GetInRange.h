#pragma once

#include "playerbot/StateTree/STAI_TaskFactory.h"

#include "Entities/ObjectGuid.h"

#include <string>

namespace ai
{
    // runtime context of Task_GetInRange
    struct Task_GetInRangeContext : public STAI_TaskContext
    {
        ObjectGuid target;          // property "target", e.g. bound to TargetProvider_GlobalTask::target
        uint32 enterMs = 0;
        bool chasing = false;
        float chaseDistance = 0.0f; // the distance the running chase keeps
    };

    // Moves the bot into range and line of sight of its target, then stops it (so a cast is not interrupted):
    //   <Task class="Task_GetInRange">
    //     <Range value="spell"/>    yards or a range name (default spell: the bot's spell range)
    //     <Binding local="target" external="Root|TargetProvider_GlobalTask::target"/>
    //   </Task>
    // Succeeded once in range and sight (right away when it already is), also when there is no living target (the
    // node finishes, the tree selects again); Failed after 10s without getting there.
    class Task_GetInRange : public STAI_Task
    {
    public:
        explicit Task_GetInRange(const std::string& range) : range(range) {}

        static void Validate(const STAI_TaskConfig& config);
        static std::unique_ptr<STAI_Task> Create(const STAI_TaskConfig& config);

        virtual const char* GetTaskClass() const override { return "Task_GetInRange"; }

        virtual const STAI_TaskContextType& GetContextType() const override { return STAI_TaskContextType::Of<Task_GetInRangeContext>(); }

        virtual void OnEnter(STAI_TaskContext& context) const override;
        virtual EActionRunStatus OnTick(STAI_TaskContext& context, uint32 elapsed, bool minimal) const override;
        virtual void OnExit(STAI_TaskContext& context) const override;
        virtual bool IsTickable() const override { return true; }

        virtual const std::vector<STAI_PropertyDef>& GetProperties() const override;

        virtual std::string GetDebugInfo(const STAI_TaskContext& context) const override;

    private:
        float GetRange(const STAI_TaskContext& context) const;
        static void Stop(Task_GetInRangeContext& context);

        std::string range;          // yards or a range name
    };
}
