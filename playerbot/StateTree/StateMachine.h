#pragma once

#include "HLS.h"

namespace ai
{
    class Engine;

    class StateMachine
    {
        IStateNode* active = nullptr;
        std::map<HLSStateType, std::unique_ptr<IStateNode>> nodes;
    public:
        void RegisterNode(std::unique_ptr<IStateNode> node, bool bInitial = false);

        void TransitionTo(HLSStateType type);

        void Update(uint32 elapsed, bool minimal);

        IStateNode* GetActive() const { return active; }

    private:
        bool bInitialized = false;
        std::unique_ptr<Engine> mp_engine;
    };

}