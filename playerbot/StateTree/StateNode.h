#pragma once

#include "HLS.h"

#include <functional>
#include <vector>
#include <memory>

namespace ai
{
    class StateTreeTask;

    class StateNode : public IStateNode
    {
    protected:
        std::vector<std::function<std::unique_ptr<StateTreeTask>()>> factories; // action factories
        std::unique_ptr<StateTreeTask> runningTask;
        ActionRunContext runningCtx;
    public:
        void AddActionFactory(std::function<std::unique_ptr<StateTreeTask>()> f) { factories.push_back(f); }

        void Update(uint32 elapsed, bool minimal) override;
    };

}