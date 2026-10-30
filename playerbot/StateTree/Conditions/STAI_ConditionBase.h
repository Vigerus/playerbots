#pragma once

#include "playerbot/StateTree/STAI_NodeBase.h"

namespace ai
{
    // Condition of a STAI state tree (RedStateTree's FRedSTConditionBase), a <Condition class="..."> of a state's
    // <EnterConditions>, a transition's or a global task's <Conditions>. Shared by every bot like tasks, its context
    // per bot (GetContextType) holds the values of its property references. inverted="true" negates its result.
    class STAI_ConditionBase : public STAI_NodeBase
    {
    public:
        // called by the tree
        bool TestCondition(STAI_NodeContext& context) const { return bInverted != OnTestCondition(context); }
        void EnterState(STAI_NodeContext& context) const { OnEnterState(context); }
        void ExitState(STAI_NodeContext& context) const { OnExitState(context); }

        bool IsInverted() const { return bInverted; }
        void SetInverted(bool inverted) { bInverted = inverted; }

        virtual std::string GetName() const override { return GetNodeClass(); }

        // the condition as the debug views show it: its GetDebugInfo, NOT in front when inverted
        std::string GetText() const
        {
            const std::string info = GetDebugInfo().empty() ? GetName() : GetDebugInfo();
            return bInverted ? "NOT " + info : info;
        }

    protected:
        // the node's hooks (RedStateTree's OnTestCondition / OnEnterState / OnExitState); enter / exit with the
        // state the condition belongs to (its enter conditions and its transitions' conditions)
        virtual bool OnTestCondition(STAI_NodeContext& context) const = 0;
        virtual void OnEnterState(STAI_NodeContext& context) const {}
        virtual void OnExitState(STAI_NodeContext& context) const {}

    private:
        bool bInverted = false;
    };
}
