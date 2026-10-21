#include "StateMachine.h"

#include "World/ProfilingSubsystem.h"

#include <algorithm>

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

        if (type == HLSStateType::Reaction)
        {
            reactionNode = inserted.first->second.get();
            reactionChecker = dynamic_cast<IReactionChecker*>(reactionNode);
        }

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

    IStateNode* next = it->second.get();

    // a reaction preempted the state: the transition (death, resurrection, combat start / end) changes the state
    // the reaction resumes to, the reaction itself finishes first
    if (active && previous && active != next && active->GetStateType() == HLSStateType::Reaction && next->GetStateType() != HLSStateType::Reaction)
    {
        CCLOG_TRACE(m_logger) << "[TRANSITION] " << active->GetName() << " resumes to " << next->GetName();
        previous = next;
        return;
    }

    // already there: the state keeps running (its tree is not restarted)
    if (active == next)
    {
        return;
    }

    CCLOG_TRACE(m_logger) << "[TRANSITION] " << (active ? active->GetName() : "-") << " >> " << next->GetName();

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
    // TransitionTo exits the preempted state
    IStateNode* preempted = active;
    previous = nullptr;
    TransitionTo(next);
    previous = preempted;
}

void StateMachine::ResumePrevious()
{
    if (!previous) { return; }

    if (active)
    {
        active->OnExit();
    }

    active = previous;
    previous = nullptr;
    activeSinceMs = WorldTimer::getMSTime();
}

bool StateMachine::GetStateType(HLSStateType& type) const
{
    if (!active)
        return false;

    type = (previous && active->GetStateType() == HLSStateType::Reaction) ? previous->GetStateType() : active->GetStateType();
    return true;
}

void StateMachine::Update(uint32 elapsed, bool minimal)
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(StateMachine::Update);

    if (!active)
    {
        return;
    }

    if (!bInitialized)
    {
        active->OnEnter();
        bInitialized = true;
    }

    if (reactionChecker)
    {
        TRACE_CPU_PROFILER_ENTRY_SCOPE(StateMachine::Update::Reaction);
        if (active != reactionNode)
        {
            if (reactionChecker->ShouldEnterReaction())
            {
                PreemptTo(HLSStateType::Reaction);
            }
        }
        else
        {
            if (!reactionChecker->IsReacting())
            {
                ResumePrevious();
            }
        }
    }

    // no per tick trace: with TRACE on (root logger) it was a log file write per bot and tick; transitions are logged
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

// transition text for the snapshot: '|' is a separator there, the path is sent as Root/<node>
static std::string SanitizeTransition(const std::string& text)
{
    std::string result = text;
    std::replace(result.begin(), result.end(), '|', '/');
    return SanitizeDebugField(result);
}

// Snapshot format (records separated by '~', fields by '^'):
//   H^<active type>^<ms in active state>^<previous type or ->
//   N^<type>^<node name>^<active 0|1>^<last action>^<last relevance>^<last status>^<strategy,strategy,...>
//   K^<type>^<depth>^<tree name>^<running 0|1>       tree stack of the state above, bottom (depth 0) to top
//   G^<type>^<depth>^<task name>^<spell id,...>        global task of the tree K^<type>^<depth>, the spells it works with
//   C^<type>^<active 0|1>^<node name>^<task class>^<conditions>^<task info>^<running task index or -1>^<task;task;...>
//       ^<transition;transition;...>^<spell id,...>   (spells of the running task)
//       ^<disabled 0|1,...>   (per task: switched off in the addon, STAI_TaskContext::bDisabled)
//       ^<info;info;...>      (per task: its static debug info, {spell:<id>} shown as the spell)
//       nodes of the top tree; task class / info of the running task (the first one while inactive), every task
//       of the node in the order they run, its transitions "<type> Root/<node>" in the order they are checked
//       conditions: [ANY:]<result><trigger>&&<result><trigger>..., result + passed, - failed, ? not checked;
//       ANY: one of them has to pass (<Conditions match="any">), else all
//   T^<from type or *>^<to type or <prev>>^<condition label>
// a task's debug info inside the ';' separated list of a node's tasks
static std::string SanitizeTaskInfo(const std::string& info)
{
    std::string text = SanitizeDebugField(info);
    std::replace(text.begin(), text.end(), ';', ',');
    return text;
}

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
                if (engine->IsPassiveStrategy(std::string(strategy)))
                    strategies += " (passive)";
            }
        }

        out << "^" << lastAction << "^" << relevance << "^" << status << "^" << strategies;

        const std::vector<StateNodeDebugTree> stack = node->GetDebugTreeStack();
        for (size_t depth = 0; depth < stack.size(); ++depth)
        {
            out << "~K^" << GetHLSStateTypeName(type) << "^" << depth << "^" << SanitizeDebugField(stack[depth].name)
                << "^" << (stack[depth].running ? 1 : 0);
            for (size_t global = 0; global < stack[depth].globalTasks.size(); ++global)
            {
                out << "~G^" << GetHLSStateTypeName(type) << "^" << depth << "^" << SanitizeDebugField(stack[depth].globalTasks[global]) << "^";
                if (global < stack[depth].globalSpells.size())
                    for (size_t spell = 0; spell < stack[depth].globalSpells[global].size(); ++spell)
                        out << (spell ? "," : "") << stack[depth].globalSpells[global][spell];
            }
        }

        static const std::vector<StateNodeDebugChild> noNodes;
        for (const StateNodeDebugChild& child : stack.empty() ? noNodes : stack.back().nodes)
        {
            std::string conditions = child.conditionsAny && !child.conditions.empty() ? "ANY:" : "";
            for (const auto& [text, result] : child.conditions)
            {
                if (!conditions.empty() && conditions != "ANY:")
                    conditions += "&&";
                conditions += result == StateNodeConditionResult::Passed ? '+' : result == StateNodeConditionResult::Failed ? '-' : '?';
                conditions += SanitizeDebugField(text);
            }

            out << "~C^" << GetHLSStateTypeName(type) << "^" << (child.active ? 1 : 0)
                << "^" << SanitizeDebugField(child.name) << "^" << SanitizeDebugField(child.taskClass)
                << "^" << conditions << "^" << SanitizeDebugField(child.info) << "^" << child.currentTask << "^";
            for (size_t task = 0; task < child.tasks.size(); ++task)
                out << (task ? ";" : "") << SanitizeDebugField(child.tasks[task]);
            out << "^";
            for (size_t transition = 0; transition < child.transitions.size(); ++transition)
                out << (transition ? ";" : "") << SanitizeTransition(child.transitions[transition]);
            out << "^";
            for (size_t spell = 0; spell < child.spells.size(); ++spell)
                out << (spell ? "," : "") << child.spells[spell];
            out << "^";
            for (size_t task = 0; task < child.tasksDisabled.size(); ++task)
                out << (task ? "," : "") << (child.tasksDisabled[task] ? 1 : 0);
            out << "^";
            for (size_t task = 0; task < child.taskInfos.size(); ++task)
                out << (task ? ";" : "") << SanitizeTaskInfo(child.taskInfos[task]);
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

bool StateMachine::SetTaskDisabled(const std::string& state, const std::string& tree, int node, int task, bool disabled)
{
    for (const auto& [type, stateNode] : nodes)
        if (state == GetHLSStateTypeName(type))
            return stateNode->SetTaskDisabled(tree, node, task, disabled);
    return false;
}

Engine* StateMachine::FindDebugEngine(const std::string& state) const
{
    for (const auto& [type, stateNode] : nodes)
        if (state == GetHLSStateTypeName(type))
            return stateNode->GetDebugEngine();
    return nullptr;
}

// History format (records separated by '~', fields by '^'), newest first:
//   Y^<entries in the log>
//   A^<ms since start>^<duration ms>^<running 0|1>^<kind: task|action>^<name: the task's debug info|action>
//     ^<source: node name|strategy>^<result: 1 successful, 0 failed, empty none>
// running entries report the time so far as duration
std::string StateMachine::BuildDebugHistory()
{
    static constexpr size_t maxEntries = 50;

    const uint32 now = WorldTimer::getMSTime();
    const std::deque<STAI_ActivityLog::Entry>& entries = activityLog.GetEntries();

    std::ostringstream out;
    out << "Y^" << entries.size();

    size_t sent = 0;
    for (auto it = entries.rbegin(); it != entries.rend() && sent < maxEntries; ++it, ++sent)
    {
        const uint32 age = WorldTimer::getMSTimeDiff(it->startMs, now);
        const bool running = it->open || WorldTimer::getMSTimeDiff(it->startMs, it->endMs) > age;
        const uint32 duration = running ? age : WorldTimer::getMSTimeDiff(it->startMs, it->endMs);

        out << "~A^" << age << "^" << duration << "^" << (running ? 1 : 0) << "^" << it->kind
            << "^" << SanitizeDebugField(it->name) << "^" << SanitizeDebugField(it->source)
            << "^" << (it->result < 0 ? "" : it->result ? "1" : "0");
    }

    return out.str();
}
