#pragma once

#include "playerbot/StateTree/StateMachine.h"
#include "playerbot/StateTree/STAI_Tree.h"

#include <memory>
#include <vector>

namespace ai
{
    class Trigger;

    // HLS state running a STAI tree. Nodes are checked top to bottom: the first node whose conditions pass
    // is entered; an earlier node interrupts the running one as soon as its conditions pass; the running
    // node stays active until its task returns Succeeded or Failed (a failed node is skipped for a while).
    class StateNode_Tree : public IStateNode, public IReactionChecker
    {
    public:
        StateNode_Tree(const STAI_TaskContext& context, std::shared_ptr<const STAI_TreeAsset> asset);

        virtual const char* GetName() const override { return asset->name.c_str(); }
        virtual HLSStateType GetStateType() const override { return context.state; }

        // temporary, for compatibility: forwarded to every task
        virtual void UpdateStrategies(const std::string& name, BotState type) override;
        virtual void ResetStrategies() override;
        virtual void ReInit() override;

        virtual void OnEnter() override {}
        virtual void OnExit() override;
        virtual void Update(uint32 elapsed, bool minimal) override;

        virtual bool ShouldEnterReaction() override;
        virtual bool IsReacting() override;

        virtual Engine* GetDebugEngine() const override;
        virtual std::vector<StateNodeDebugChild> GetDebugChildren() const override;

    private:
        struct RuntimeNode
        {
            const STAI_NodeDef* def = nullptr;
            std::unique_ptr<STAI_Task> task;
            std::vector<Trigger*> triggers;                       // per condition, nullptr = unknown trigger
            std::vector<StateNodeConditionResult> results;        // per condition, last update
            bool failed = false;
            uint32 failedAtMs = 0;
            uint64 activityId = 0;
        };

        bool CanEnter(RuntimeNode& node, uint32 now);
        void ExitActive();
        IReactionChecker* FindReactionChecker() const;

        STAI_TaskContext context;
        std::shared_ptr<const STAI_TreeAsset> asset;
        std::vector<RuntimeNode> nodes;
        int active = -1;
    };
}
