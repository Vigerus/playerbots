#include "StateMachine.h"

#include "World/ProfilingSubsystem.h"

#include <algorithm>

StateMachine::StateMachine() :
    m_logger(log4cxx::Logger::getLogger("StateMachine"))
{
}

void StateMachine::RegisterNode(std::unique_ptr<IStateNode> node)
{
    HLSStateType type = node->GetStateType();

    if (nodes.find(type) != nodes.end())
    {
        throw std::runtime_error("StateMachine::RegisterNode: Node for state type already registered");
    }

    IStateNode* registered = nodes.emplace(type, std::move(node)).first->second.get();

    if (type == HLSStateType::Reaction)
    {
        reaction = registered;
    }
    else if (type == HLSStateType::NonCombat)
    {
        initial = registered;
        active = initial;
    }
}

void StateMachine::TransitionTo(HLSStateType type)
{
    if (!IsMainState(type))
    {
        return;
    }

    auto it = nodes.find(type);

    if (it == nodes.end())
    {
        return;
    }

    IStateNode* next = it->second.get();

    // already there: the state keeps running (its tree is not restarted)
    if (active == next)
    {
        return;
    }

    CCLOG_TRACE(m_logger) << "[TRANSITION] " << (active ? active->GetName() : "-") << " >> " << next->GetName();

    // not entered yet: the first Update enters the state it ends up in
    if (bInitialized && active)
        active->OnExit();
    active = next;
    activeSinceMs = WorldTimer::getMSTime();
    if (bInitialized)
        active->OnEnter();
}

bool StateMachine::GetStateType(HLSStateType& type) const
{
    if (!active)
        return false;

    type = active->GetStateType();
    return true;
}

void StateMachine::Update(uint32 elapsed, bool minimal)
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(StateMachine::Update);

    if (!bInitialized)
    {
        if (active)
            active->OnEnter();
        if (reaction)
            reaction->OnEnter();
        activeSinceMs = WorldTimer::getMSTime();
        bInitialized = true;
    }

    // no per tick trace: with TRACE on (root logger) it was a log file write per bot and tick; transitions are logged
    if (active)
    {
        active->Update(elapsed, minimal);
    }

    if (reaction)
    {
        TRACE_CPU_PROFILER_ENTRY_SCOPE(StateMachine::Update::Reaction);
        reaction->Update(elapsed, minimal);
    }
}

void StateMachine::Reset()
{
    if (bInitialized)
    {
        if (active)
            active->OnExit();
        if (reaction)
            reaction->OnExit();
    }
    active = initial;
    bInitialized = false;
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
//   H^<active type>^<ms in active state>^-             (the last field was the preempted state, always - now)
//   N^<type>^<node name>^<active 0|1>^^0.0^-^          (the empty fields were the legacy engine's last action,
//                                                       relevance, status and strategies; Reaction is always active)
//   K^<type>^<depth>^<tree name>^<running 0|1>       tree stack of the state above, bottom (depth 0) to top
//   G^<type>^<depth>^<task name>^<spell id,...>        global task of the tree K^<type>^<depth>, the spells it works with
//   C^<type>^<active 0|1>^<node name>^<task class>^<conditions>^<task info>^<running task index or -1>^<task;task;...>
//       ^<transition;transition;...>^<spell id,...>   (spells of the running task)
//       ^<disabled 0|1,...>   (per task: switched off in the addon, STAI_TaskContext::bDisabled)
//       ^<info;info;...>      (per task: its static debug info, {spell:<id>} shown as the spell)
//       ^<node disabled 0|1>  (switched off in the addon: never selected)
//       ^<override 0|1|2,...> (per condition: 0 checked, 1 forced to pass, 2 forced to fail in the addon)
//       nodes of the top tree; task class / info of the running task (the first one while inactive), every task
//       of the node in the order they run, its transitions "<type> Root/<node>" in the order they are checked
//       conditions: [ANY:]<result><condition>&&<result><condition>..., result + passed, - failed, ? not checked;
//       ANY: one of them has to pass (<Conditions match="any">), else all
//   T^<from type or *>^<to type>^<condition label>
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
        << "^-";

    for (const auto& [type, node] : nodes)
    {
        const bool running = node.get() == active || node.get() == reaction;
        out << "~N^" << GetHLSStateTypeName(type) << "^" << SanitizeDebugField(node->GetName()) << "^" << (running ? 1 : 0)
            << "^^" << 0.0f << "^-^";

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
            out << "^" << (child.disabled ? 1 : 0) << "^";
            for (size_t condition = 0; condition < child.conditionOverrides.size(); ++condition)
                out << (condition ? "," : "") << int(child.conditionOverrides[condition]);
        }
    }

    // transitions are triggered from PlayerbotAI (OnCombatStarted, OnDeath, ...); Reaction runs next to every state
    out << "~T^NonCombat^Combat^OnCombatStarted"
        << "~T^Combat^NonCombat^OnCombatEnded"
        << "~T^*^Dead^OnDeath"
        << "~T^Dead^NonCombat^OnResurrected"
        << "~T^*^Reaction^always (additive)";

    return out.str();
}

void StateMachine::GetDebugView(StateMachineDebugView& view) const
{
    view.active = active ? GetHLSStateTypeName(active->GetStateType()) : "";
    view.msInState = active ? WorldTimer::getMSTimeDiff(activeSinceMs, WorldTimer::getMSTime()) : 0;
    view.states.clear();
    for (const auto& [type, node] : nodes)
    {
        StateMachineDebugView::State& state = view.states.emplace_back();
        state.type = GetHLSStateTypeName(type);
        state.name = node->GetName();
        state.running = node.get() == active || node.get() == reaction;
        state.stack = node->GetDebugTreeStack();
    }
}

bool StateMachine::SetDebugSwitch(const std::string& state, StateTreeSwitch what, const std::string& tree, int node, int index, int value)
{
    for (const auto& [type, stateNode] : nodes)
        if (state == GetHLSStateTypeName(type))
            return stateNode->SetDebugSwitch(what, tree, node, index, value);
    return false;
}

// History format (records separated by '~', fields by '^'), newest first:
//   Y^<entries in the log>
//   A^<ms since start>^<duration ms>^<running 0|1>^<kind: task|action>^<name: the task's debug info|action>
//     ^<source: node name|strategy>^<result: 1 successful, 0 failed, empty none>
//     ^<runs merged into the entry>^<total ms of the runs>^<failed runs>
// running entries report the time so far as duration; age, duration and result are the latest run's (repeated
// runs of the newest entry are merged into it, STAI_ActivityLog)
std::string StateMachine::BuildDebugHistory()
{
    static constexpr size_t maxEntries = 50;

    const uint32 now = WorldTimer::getMSTime();
    const std::deque<STAI_ActivityLog::Entry>& entries = activityLog.GetEntries();

    // the log may keep an hour (the server's debug window), the addon shows the last minute
    auto inAddonRange = [now](const STAI_ActivityLog::Entry& entry)
    {
        return entry.open || WorldTimer::getMSTimeDiff(entry.endMs, now) < STAI_ActivityLog::addonRetentionMs;
    };

    std::ostringstream out;
    out << "Y^" << std::count_if(entries.begin(), entries.end(), inAddonRange);

    size_t sent = 0;
    for (auto it = entries.rbegin(); it != entries.rend() && sent < maxEntries; ++it)
    {
        if (!inAddonRange(*it))
            continue;
        ++sent;

        const uint32 age = WorldTimer::getMSTimeDiff(it->startMs, now);
        const bool running = it->open || WorldTimer::getMSTimeDiff(it->startMs, it->endMs) > age;
        const uint32 duration = running ? age : WorldTimer::getMSTimeDiff(it->startMs, it->endMs);

        out << "~A^" << age << "^" << duration << "^" << (running ? 1 : 0) << "^" << it->kind
            << "^" << SanitizeDebugField(it->name) << "^" << SanitizeDebugField(it->source)
            << "^" << (it->result < 0 ? "" : it->result ? "1" : "0")
            << "^" << it->count << "^" << (it->doneMs + duration) << "^" << it->failures;
    }

    return out.str();
}
