#pragma once

#include "playerbot/StateTree/STAI_NodeBase.h"

#include <algorithm>

namespace ai
{
    // Utility consideration of a STAI state (RedStateTree's FRedSTConsiderationBase), a <Consideration class="...">
    // of a state's <Considerations>: its score (0..1) times the scores of the state's other considerations and the
    // state's weight is the state's utility, used by the utility selections of its parent.
    class STAI_ConsiderationBase : public STAI_NodeBase
    {
    public:
        // called by the tree, clamped to 0..1
        float GetScore(STAI_NodeContext& context) const { return std::clamp(CalculateScore(context), 0.0f, 1.0f); }

        virtual std::string GetName() const override { return GetNodeClass(); }

    protected:
        virtual float CalculateScore(STAI_NodeContext& context) const = 0;
    };
}
