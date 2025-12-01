#include "StateMachine.h"

StateMachine::StateMachine() :
    m_logger(log4cxx::Logger::getLogger("StateMachine"))
{
}

void StateMachine::RegisterNode(std::unique_ptr<IStateNode> node, bool bInitial)
{
    HLSStateType type = node->GetStateType();

    if (nodes.find(type) == nodes.end())
    {
        auto inserted = nodes.emplace(type, std::move(node));

        if (bInitial)
        {
            initial = inserted.first->second.get();
            active = initial;
        }
    }
    else
    {
        throw std::runtime_error("StateMachine::RegisterNode: Node for state type already registered");
    }
}

void StateMachine::TransitionTo(HLSStateType type)
{
    auto it = nodes.find(type);

    if (it == nodes.end())
    {
        return;
    }

    CCLOG_TRACE(m_logger) << "[TRANSITION] " << active->GetName() << " >> " << it->second->GetName();

    if (active)
        active->OnExit();
    active = it->second.get();
    if (active)
        active->OnEnter();
}

IStateNode* StateMachine::FindNode(HLSStateType type) const
{
    auto it = nodes.find(type);
    return it == nodes.end() ? nullptr : it->second.get();
}

void StateMachine::PreemptTo(HLSStateType next)
{
    if (active)// && active->IsInterruptible())
    {
        // call Exit with interrupted flag
        active->OnExit();
    }
    
    previous = active;

    TransitionTo(next);
}

void StateMachine::ResumePrevious()
{
    if (!previous) { return; }

    if (active)
    {
        active->OnExit();
    }

    active = previous;
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

    if (IStateNode* reactionNode = FindNode(HLSStateType::Reaction))
    {
        if (IReactionChecker* checker = dynamic_cast<IReactionChecker*>(reactionNode))
        {
            if (active != reactionNode)
            {
                if (checker->ShouldEnterReaction())
                {
                    PreemptTo(HLSStateType::Reaction);
                }
            }
            else
            {
                if (!checker->IsReacting())
                {
                    ResumePrevious();
                }
            }
        }
    }

    CCLOG_TRACE(m_logger) << "[UPDATE] " << active->GetName();

    active->Update(elapsed, minimal);
}

void StateMachine::Reset()
{
    if (active)
    {
        active->OnExit();
    }
    active = initial;
    bInitialized = false;
}

void StateMachine::ReInitCurrentEngine()
{
    if (active)
    {
        active->ReInit();
    }
}

void StateMachine::UpdateStrategies(const std::string& name, BotState botState)
{
    for (auto& [type, node] : nodes)
    {
        node->UpdateStrategies(name, botState);
    }
}

void StateMachine::ResetStrategies()
{
    for (auto& [type, node] : nodes)
    {
        node->ResetStrategies();
    }
}