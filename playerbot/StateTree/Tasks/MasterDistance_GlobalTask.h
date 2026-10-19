#pragma once

#include "playerbot/StateTree/Tasks/STAI_GlobalTask.h"

#include <string>
#include <vector>

namespace ai
{
    // runtime context of MasterDistance_GlobalTask
    struct MasterDistance_GlobalTaskContext : public STAI_TaskContext
    {
        // fired on every bot update while the master is further away than the range:
        //   <Transition type="OnDelegate" source="Root|MasterDistance_GlobalTask::onMasterFar" node="Root|Follow player"/>
        FSTAI_Delegate onMasterFar;
        bool masterFar = false;     // property "masterFar"
    };

    // Global task that watches the 2D distance to the master (same check as the "distance from master" trigger):
    //   <Global>
    //     <Task class="MasterDistance_GlobalTask">
    //       <Range value="follow"/>     yards or a range name (default follow)
    //     </Task>
    //   </Global>
    // Tickable: fires onMasterFar every update while the master is further away, so the running node leaves for
    // Follow (whose conditions decide whether the bot follows: follow / stay strategies).
    class MasterDistance_GlobalTask : public STAI_GlobalTask
    {
    public:
        explicit MasterDistance_GlobalTask(const std::string& range) : range(range) {}

        static void Validate(const STAI_TaskConfig& config);
        static std::unique_ptr<STAI_Task> Create(const STAI_TaskConfig& config);

        virtual const char* GetTaskClass() const override { return "MasterDistance_GlobalTask"; }

        virtual const STAI_TaskContextType& GetContextType() const override { return STAI_TaskContextType::Of<MasterDistance_GlobalTaskContext>(); }

        virtual void OnEnter(STAI_TaskContext& context) const override;
        virtual EActionRunStatus OnTick(STAI_TaskContext& context, uint32 elapsed, bool minimal) const override;
        virtual bool IsTickable() const override { return true; }

        virtual const std::vector<STAI_PropertyDef>& GetProperties() const override;
        virtual const std::vector<STAI_DelegateDef>& GetDelegates() const override;

        virtual std::string GetDebugInfo(const STAI_TaskContext& context) const override;

    private:
        std::string range;      // yards or a range name
    };
}
