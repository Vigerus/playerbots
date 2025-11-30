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
        void RegisterNode(std::unique_ptr<IStateNode> node, bool bInitial = false)
        {
            HLSStateType type = node->GetStateType();

            if (nodes.find(type) == nodes.end())
            {
                auto inserted = nodes.emplace(type, std::move(node));

                if (bInitial)
                {
                    active = inserted.first->second.get();
                }
            }
            else
            {
                throw std::runtime_error("StateMachine::RegisterNode: Node for state type already registered");
            }
        }

        void TransitionTo(HLSStateType type)
        {
            if (active)
                active->OnExit();
            active = nodes[type].get();
            if (active)
                active->OnEnter();
        }

        void Update(uint32 elapsed, bool minimal)
        {
            if (!active)
            { 
                return;
            }

            if (!bInitialized)
            {
                active->OnEnter();
                bInitialized = true;
            }

            active->Update(elapsed, minimal);
        }
        IStateNode* GetActive() const { return active; }

    private:
        bool bInitialized = false;
        std::unique_ptr<Engine> mp_engine;
    };

}