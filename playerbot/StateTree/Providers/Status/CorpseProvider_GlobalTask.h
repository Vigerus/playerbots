#pragma once

#include "playerbot/StateTree/STAI_Navigation.h"
#include "playerbot/StateTree/Tasks/STAI_GlobalTask.h"

namespace ai
{
    // runtime context of CorpseProvider_GlobalTask: where the dead bot's body is
    struct CorpseProvider_GlobalTaskContext : public STAI_TaskContext
    {
        // fired when corpseFar turned true (the ghost appeared at the graveyard):
        //   <Transition type="OnDelegate" source="Root|CorpseProvider_GlobalTask::onCorpseFar" node="Root|Corpse Run"/>
        FSTAI_Delegate onCorpseFar;

        // properties:
        //   released: the bot is a ghost (its spirit was released)
        //   corpseLocation: where its corpse is (any map), for Task_MakePath; unset without a corpse
        //   corpseFar: released and the corpse is on another map or farther than the reclaim radius (less a margin)
        bool released = false;
        STAI_Location corpseLocation;
        bool corpseFar = false;

        uint32 sinceCheckMs = 0;
    };

    // Global task of the Dead trees for the corpse run:
    //   <Global> <Task class="CorpseProvider_GlobalTask"/> </Global>
    //   <Node name="Corpse Run">
    //     <Conditions> <Condition binding="Root|CorpseProvider_GlobalTask::corpseFar"/> </Conditions>
    //     <Task class="Task_MakePath"> <Binding local="location" external="Root|CorpseProvider_GlobalTask::corpseLocation"/> </Task>
    //     <Task class="Task_MoveTo"> ... </Task>
    //   </Node>
    // A body inside a dungeon is reached through its portal, where the core resurrects the ghost; outdoors the
    // Reaction tree reclaims it (UIInteractionProvider_GlobalTask::canReclaimCorpse). Ticked: checks every 500ms.
    class CorpseProvider_GlobalTask : public STAI_DataProviderBase
    {
    public:
        static void Validate(const STAI_TaskConfig& config) {}
        static std::unique_ptr<STAI_TaskBase> Create(const STAI_TaskConfig& config) { return std::make_unique<CorpseProvider_GlobalTask>(); }

        virtual const char* GetNodeClass() const override { return "CorpseProvider_GlobalTask"; }
        inline static std::string task_name = "CorpseProvider";
        virtual std::string GetName() const override { return task_name; }

        virtual const STAI_TaskContextType& GetContextType() const override { return STAI_TaskContextType::Of<CorpseProvider_GlobalTaskContext>(); }

        virtual EActionRunStatus OnEnterState(STAI_TaskContext& context) const override;
        virtual EActionRunStatus OnTick(STAI_TaskContext& context, uint32 elapsed, bool minimal) const override;
        virtual bool ShouldCallTick() const override { return true; }

        virtual const std::vector<STAI_PropertyDef>& GetProperties() const override;
        virtual const std::vector<STAI_DelegateDef>& GetDelegates() const override;

    private:
        static void Update(CorpseProvider_GlobalTaskContext& context);
    };
}
