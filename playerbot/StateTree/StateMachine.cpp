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
    activeSinceMs = WorldTimer::getMSTime();
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
    activeSinceMs = WorldTimer::getMSTime();
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

static const char* GetHLSStateTypeName(HLSStateType type)
{
    switch (type)
    {
        case HLSStateType::NonCombat: return "NonCombat";
        case HLSStateType::Combat: return "Combat";
        case HLSStateType::Reaction: return "Reaction";
        case HLSStateType::Dead: return "Dead";
        default: return "Unknown";
    }
}

// field/record separators of the snapshot format must not appear inside values
static std::string SanitizeDebugField(std::string_view value)
{
    std::string result(value);
    for (char& c : result)
    {
        if (c == '~' || c == '^' || c == '|' || c == ',' || c == '\t' || c == '\n' || c == '\r')
            c = ' ';
    }
    return result;
}

// Snapshot format (records separated by '~', fields by '^'):
//   H^<active type>^<ms in active state>^<previous type or ->
//   N^<type>^<node name>^<active 0|1>^<last action>^<last relevance>^<last status>^<strategy,strategy,...>
//   C^<type>^<active 0|1>^<node name>^<task class>^<conditions>^<task info>     (tree nodes of the state above)
//       conditions: <result><trigger>&&<result><trigger>..., result + passed, - failed, ? not checked
//   T^<from type or *>^<to type or <prev>>^<condition label>
std::string StateMachine::BuildDebugSnapshot() const
{
    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(1);

    out << "H^" << (active ? GetHLSStateTypeName(active->GetStateType()) : "-")
        << "^" << (active ? WorldTimer::getMSTimeDiff(activeSinceMs, WorldTimer::getMSTime()) : 0)
        << "^" << (previous && active && active->GetStateType() == HLSStateType::Reaction ? GetHLSStateTypeName(previous->GetStateType()) : "-");

    for (const auto& [type, node] : nodes)
    {
        std::string nodeName = node->GetName();
        if (Engine* engine = node->GetDebugEngine())
            if (!engine->GetBrainName().empty())
                nodeName += "  [" + engine->GetBrainName() + "]";

        out << "~N^" << GetHLSStateTypeName(type) << "^" << SanitizeDebugField(nodeName) << "^" << (node.get() == active ? 1 : 0);

        std::string lastAction;
        float relevance = 0.0f;
        const char* status = "-";
        std::string strategies;

        if (Engine* engine = node->GetDebugEngine())
        {
            if (const ActionRunContext* context = engine->GetLastActionContext())
            {
                lastAction = SanitizeDebugField(context->name);
                relevance = context->relevance;

                switch (engine->GetLastExecutedActionStatus())
                {
                    case EActionRunStatus::Running: status = "Running"; break;
                    case EActionRunStatus::Succeeded: status = "Succeeded"; break;
                    case EActionRunStatus::Failed: status = "Failed"; break;
                }
            }

            for (std::string_view strategy : engine->GetStrategies())
            {
                if (!strategies.empty())
                    strategies += ",";
                strategies += SanitizeDebugField(strategy);
            }
        }

        out << "^" << lastAction << "^" << relevance << "^" << status << "^" << strategies;

        for (const StateNodeDebugChild& child : node->GetDebugChildren())
        {
            std::string conditions;
            for (const auto& [text, result] : child.conditions)
            {
                if (!conditions.empty())
                    conditions += "&&";
                conditions += result == StateNodeConditionResult::Passed ? '+' : result == StateNodeConditionResult::Failed ? '-' : '?';
                conditions += SanitizeDebugField(text);
            }

            out << "~C^" << GetHLSStateTypeName(type) << "^" << (child.active ? 1 : 0)
                << "^" << SanitizeDebugField(child.name) << "^" << SanitizeDebugField(child.taskClass)
                << "^" << conditions << "^" << SanitizeDebugField(child.info);
        }
    }

    // transitions are triggered from PlayerbotAI (OnCombatStarted, OnDeath, ...) and from Update() (reaction preempt)
    out << "~T^NonCombat^Combat^OnCombatStarted"
        << "~T^Combat^NonCombat^OnCombatEnded"
        << "~T^*^Dead^OnDeath"
        << "~T^Dead^NonCombat^OnResurrected"
        << "~T^*^Reaction^ShouldEnterReaction (preempt)"
        << "~T^Reaction^<prev>^!IsReacting (resume)";

    return out.str();
}
