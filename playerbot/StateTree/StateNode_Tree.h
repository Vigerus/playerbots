#pragma once

#include "playerbot/StateTree/StateMachine.h"
#include "playerbot/StateTree/STAI_TreeInstance.h"

#include <memory>
#include <vector>

namespace ai
{
    // HLS state running a STAI tree (the initial tree of the state, bottom of the tree stack): OnEnter starts it,
    // Update ticks it, OnExit finishes it. When the tree finished itself (STAI_FinishTreeTask) the next update
    // starts it again.
    class StateNode_Tree : public IStateNode
    {
    public:
        StateNode_Tree(const STAI_TaskContext& context, std::shared_ptr<const STAI_TreeAsset> asset);

        virtual const char* GetName() const override { return tree.GetAsset().name.c_str(); }
        virtual HLSStateType GetStateType() const override { return state; }

        virtual void OnEnter() override { tree.OnStart(); }
        virtual void OnExit() override { tree.OnFinish(); }
        virtual void Update(uint32 elapsed, bool minimal) override { tree.OnTick(elapsed, minimal); }

        virtual std::vector<StateNodeDebugTree> GetDebugTreeStack() const override
        {
            std::vector<StateNodeDebugTree> stack;
            tree.AppendDebugTreeStack(stack);
            return stack;
        }
        virtual bool SetDebugSwitch(StateTreeSwitch what, const std::string& name, int node, int index, int value) override
        {
            return tree.SetDebugSwitch(what, name, node, index, value);
        }

    private:
        HLSStateType state;
        STAI_TreeInstance tree;
    };
}
