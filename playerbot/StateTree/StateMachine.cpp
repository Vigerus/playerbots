#include "StateMachine.h"

void StateMachine::RegisterNode(std::unique_ptr<IStateNode> node, bool bInitial)
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

void StateMachine::TransitionTo(HLSStateType type)
{
    if (active)
        active->OnExit();
    active = nodes[type].get();
    if (active)
        active->OnEnter();
}

void StateMachine::Update(uint32 elapsed, bool minimal)
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