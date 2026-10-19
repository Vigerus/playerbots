#include "STAI_TreeInstance.h"

#include "playerbot/PlayerbotAI.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/Trigger.h"
#include "playerbot/StateTree/STAI_ActivityLog.h"

#include <algorithm>

using namespace ai;

namespace
{
    // a node whose task failed is not selected again for this long (avoids re-entering a failing task every update)
    constexpr uint32 failedRetryDelayMs = 3000;
}

STAI_TreeInstance::STAI_TreeInstance(const STAI_TaskContext& context, std::shared_ptr<const STAI_TreeAsset> asset)
    : context(context), asset(std::move(asset))
{
    for (const STAI_NodeDef& def : this->asset->nodes)
    {
        RuntimeNode node;
        node.def = &def;
        for (const STAI_TaskDef& task : def.tasks)
            node.tasks.push_back({ &task, nullptr, {} });
        node.results.assign(def.conditions.size(), StateNodeConditionResult::NotChecked);

        nodes.push_back(std::move(node));
    }

    for (const STAI_TaskDef& task : this->asset->globalTasks)
        globals.push_back({ &task, nullptr, {} });

    CreateContexts();
    ResolveMembers();
}

STAI_TaskContext* STAI_TreeInstance::GetContext(const STAI_MemberRef& ref) const
{
    return ref.node < 0 ? globals[ref.task].context : nodes[ref.node].tasks[ref.task].context;
}

void STAI_TreeInstance::ResolveConditions(RuntimeConditions& conditions, const STAI_ConditionsDef& def, const std::string& nodeName)
{
    conditions.def = &def;
    for (const STAI_ConditionDef& condition : def.list)
    {
        // triggers are the bot's (class contexts included), bound conditions read the property in its context
        Trigger* trigger = nullptr;
        if (!condition.trigger.empty())
        {
            trigger = context.ai->GetAiObjectContext()->GetTrigger(condition.trigger);
            if (!trigger)
                sLog.outError("StateTree: %s node '%s': unknown trigger '%s' for %s, the condition never passes",
                    asset->file.c_str(), nodeName.c_str(), condition.trigger.c_str(), context.bot->GetName());
        }
        conditions.triggers.push_back(trigger);
        conditions.boundValues.push_back(condition.trigger.empty() ? condition.property->get(*GetContext(condition.binding)) : nullptr);
    }
}

bool STAI_TreeInstance::CheckConditions(const RuntimeConditions& conditions, std::vector<StateNodeConditionResult>* results)
{
    const STAI_ConditionsDef& def = *conditions.def;
    if (def.empty())
        return true;

    // all: stops at the first failure; any: stops at the first pass (the rest is not checked)
    for (size_t i = 0; i < def.size(); ++i)
    {
        const STAI_ConditionDef& condition = def.list[i];
        const bool passed = condition.trigger.empty()
            ? condition.Passes(conditions.boundValues[i])
            : conditions.triggers[i] && conditions.triggers[i]->IsActive() != condition.negate;
        if (results)
            (*results)[i] = passed ? StateNodeConditionResult::Passed : StateNodeConditionResult::Failed;
        if (passed == def.any)
            return passed;
    }
    return !def.any;
}

void STAI_TreeInstance::ResolveMembers()
{
    // bindings: the local and the external value in the bot's contexts
    auto resolveBindings = [this](TaskSlot& slot)
    {
        for (const STAI_BindingDef& binding : slot.def->bindings)
            slot.bindings.push_back({ binding.localProperty->get(*slot.context), binding.externalProperty->get(*GetContext(binding.external)), binding.localProperty });
    };
    for (TaskSlot& global : globals)
        resolveBindings(global);

    for (RuntimeNode& node : nodes)
    {
        for (TaskSlot& task : node.tasks)
            resolveBindings(task);

        ResolveConditions(node.conditions, node.def->conditions, node.def->name);

        node.transitionConditions.resize(node.def->transitions.size());
        for (size_t t = 0; t < node.def->transitions.size(); ++t)
        {
            const STAI_TransitionDef& transition = node.def->transitions[t];
            ResolveConditions(node.transitionConditions[t], transition.conditions, node.def->name);

            // OnDelegate transitions: one remembered flag per source delegate
            if (transition.type != STAI_TransitionDef::Type::OnDelegate)
                continue;

            FSTAI_Delegate* delegate = transition.delegate->get(*GetContext(transition.source));
            auto known = std::find(delegates.begin(), delegates.end(), delegate);
            const size_t index = size_t(known - delegates.begin());
            if (known == delegates.end())
            {
                delegates.push_back(delegate);
                // the delegate lives in a context destroyed with the tree: no unsubscribing needed
                delegate->AddLambda([this, index]() { firedDelegates[index] = 1; });
            }
            node.delegateTransitions.emplace_back(index, t);
        }
    }

    firedDelegates.assign(delegates.size(), 0);
}

void STAI_TreeInstance::ApplyBindings(const TaskSlot& slot)
{
    for (const RuntimeBinding& binding : slot.bindings)
        binding.property->assign(binding.local, binding.external);
}

void STAI_TreeInstance::EnterTask(const TaskSlot& slot)
{
    ApplyBindings(slot);
    slot.def->task->OnEnter(*slot.context);
}

EActionRunStatus STAI_TreeInstance::TickTask(const TaskSlot& slot, uint32 elapsed, bool minimal)
{
    // a task that is not tickable is done once entered
    if (!slot.def->task->IsTickable())
        return EActionRunStatus::Succeeded;

    ApplyBindings(slot);
    return slot.def->task->OnTick(*slot.context, elapsed, minimal);
}

bool STAI_TreeInstance::TakeDelegateTransition(uint32 now)
{
    RuntimeNode& node = nodes[active];
    for (const auto& [index, transitionIndex] : node.delegateTransitions)
    {
        if (!firedDelegates[index])
            continue;

        // taken or not, the delegate is used up: it fires again when it has news
        firedDelegates[index] = 0;
        if (!CheckConditions(node.transitionConditions[transitionIndex]))
            continue;

        const int target = node.def->transitions[transitionIndex].target;
        if (target < 0)
        {
            // Root: selected again from the first node
            ExitActive();
            return true;
        }

        // the target must be enterable now (its conditions, a failed node's retry delay), else the next one is tried
        if (!CanEnter(nodes[target], now))
            continue;

        EnterNode(target);
        return true;
    }
    return false;
}

STAI_TreeInstance::~STAI_TreeInstance()
{
    for (TaskSlot& global : globals)
        if (global.context)
            global.context->~STAI_TaskContext();
    for (RuntimeNode& node : nodes)
        for (TaskSlot& task : node.tasks)
            if (task.context)
                task.context->~STAI_TaskContext();
}

void STAI_TreeInstance::CreateContexts()
{
    // every task of the tree: global tasks first, then the tasks of the nodes
    std::vector<TaskSlot*> slots;
    for (TaskSlot& global : globals)
        slots.push_back(&global);
    for (RuntimeNode& node : nodes)
        for (TaskSlot& task : node.tasks)
            slots.push_back(&task);

    // lay the contexts out like a stack frame: each at the next offset its alignment allows
    std::vector<size_t> offsets;
    size_t size = 0;
    for (const TaskSlot* slot : slots)
    {
        const STAI_TaskContextType& type = slot->def->task->GetContextType();
        MANGOS_ASSERT(type.alignment <= alignof(std::max_align_t));
        size = (size + type.alignment - 1) / type.alignment * type.alignment;
        offsets.push_back(size);
        size += type.size;
    }

    contextStorage.reset(new std::max_align_t[(size + sizeof(std::max_align_t) - 1) / sizeof(std::max_align_t)]);
    std::byte* memory = reinterpret_cast<std::byte*>(contextStorage.get());
    for (size_t i = 0; i < slots.size(); ++i)
        slots[i]->context = slots[i]->def->task->GetContextType().construct(memory + offsets[i], context);

    for (TaskSlot* slot : slots)
        slot->def->task->InitContext(*slot->context);
}

void STAI_TreeInstance::UpdateStrategies(const std::string& name, BotState type)
{
    for (TaskSlot& global : globals)
        global.def->task->UpdateStrategies(*global.context, name, type);
    for (RuntimeNode& node : nodes)
        for (TaskSlot& task : node.tasks)
            task.def->task->UpdateStrategies(*task.context, name, type);
}

void STAI_TreeInstance::ResetStrategies()
{
    for (TaskSlot& global : globals)
        global.def->task->ResetStrategies(*global.context);
    for (RuntimeNode& node : nodes)
        for (TaskSlot& task : node.tasks)
            task.def->task->ResetStrategies(*task.context);
}

void STAI_TreeInstance::ReInit()
{
    for (TaskSlot& global : globals)
        global.def->task->ReInit(*global.context);
    for (RuntimeNode& node : nodes)
        for (TaskSlot& task : node.tasks)
            task.def->task->ReInit(*task.context);
}

bool STAI_TreeInstance::CanEnter(RuntimeNode& node, uint32 now, bool ignoreFailedDelay)
{
    std::fill(node.results.begin(), node.results.end(), StateNodeConditionResult::NotChecked);

    if (!ignoreFailedDelay && node.failed && WorldTimer::getMSTimeDiff(node.failedAtMs, now) < failedRetryDelayMs)
        return false;

    return CheckConditions(node.conditions, &node.results);
}

void STAI_TreeInstance::EnterNode(int index)
{
    ExitActive();
    active = index;

    RuntimeNode& node = nodes[index];
    node.failed = false;
    node.current = 0;

    // the tree logs the node unless one of its tasks logs its own entries (legacy engines)
    bool logged = true;
    for (const TaskSlot& task : node.tasks)
        logged = logged && task.def->task->LogsNodeActivity();
    if (context.activity && logged)
        node.activityId = context.activity->Begin("node", node.def->name, node.tasks.front().def->task->GetTaskClass());

    EnterTask(node.tasks.front());
}

void STAI_TreeInstance::ExitActive()
{
    if (active < 0)
        return;

    // only the running task was entered, the earlier ones are exited already
    RuntimeNode& node = nodes[active];
    if (node.current >= 0)
        node.tasks[node.current].def->task->OnExit(*node.tasks[node.current].context);
    node.current = -1;

    if (node.activityId && context.activity)
        context.activity->End(node.activityId);
    node.activityId = 0;
    active = -1;
}

void STAI_TreeInstance::EnterGlobals()
{
    if (running)
        return;

    running = true;
    for (TaskSlot& global : globals)
        EnterTask(global);
}

void STAI_TreeInstance::ExitGlobals()
{
    if (!running)
        return;

    // opposite to the order they were entered
    for (auto global = globals.rbegin(); global != globals.rend(); ++global)
        global->def->task->OnExit(*global->context);
    running = false;
}

void STAI_TreeInstance::OnStart()
{
    pendingTransition = -1;
    ExitActive();
    ExitGlobals();
    // delegates fired before are stale, the global tasks fire again when they are entered
    std::fill(firedDelegates.begin(), firedDelegates.end(), 0);
    EnterGlobals();
}

void STAI_TreeInstance::OnFinish()
{
    // opposite to the activation order (OnStart, global tasks OnEnter, node OnEnter, subtree OnStart, ...):
    // the running node first, which finishes the trees stacked on it (Task_RunSubtree), then the global tasks,
    // then the tree itself is finished
    pendingTransition = -1;
    ExitActive();
    ExitGlobals();
}

EActionRunStatus STAI_TreeInstance::OnTick(uint32 elapsed, bool minimal)
{
    const uint32 now = WorldTimer::getMSTime();

    if (!running)
        OnStart();

    // scopeless: ticked whichever node runs, the result does not end anything
    for (TaskSlot& global : globals)
        TickTask(global, elapsed, minimal);

    // the running node keeps the results it was entered with, the others show only what this update checked
    for (int i = 0; i < int(nodes.size()); ++i)
        if (i != active)
            std::fill(nodes[i].results.begin(), nodes[i].results.end(), StateNodeConditionResult::NotChecked);

    // the path to the running node only changes when a delegate of one of its OnDelegate transitions fired
    if (active >= 0)
        TakeDelegateTransition(now);

    // no node runs (the tree started, the node finished or a transition went to Root): the target of the finished
    // node's transition when its conditions pass (an explicit jump, the retry delay of a failed node does not
    // apply), else the first node from the top whose conditions pass; conditions are only checked here
    if (active < 0)
    {
        if (pendingTransition >= 0)
        {
            const int target = pendingTransition;
            pendingTransition = -1;
            if (CanEnter(nodes[target], now, true))
                EnterNode(target);
        }

        for (int i = 0; active < 0 && i < int(nodes.size()); ++i)
            if (CanEnter(nodes[i], now))
                EnterNode(i);

        // none can be entered: the selection is tried again next tick
        if (active < 0)
            return EActionRunStatus::Running;
    }

    RuntimeNode& node = nodes[active];
    TaskSlot& task = node.tasks[node.current];
    const EActionRunStatus status = TickTask(task, elapsed, minimal);
    if (status == EActionRunStatus::Running)
        return status;

    // a failed task fails the node: its transition, or the next tick selects from the first node
    if (status == EActionRunStatus::Failed)
    {
        node.failed = true;
        node.failedAtMs = now;
        FinishNode(node, status);
        return EActionRunStatus::Running;
    }

    if (task.def->task->FinishesTree())
    {
        OnFinish();
        return EActionRunStatus::Succeeded;
    }

    // succeeded: the next task of the node, or the node is done and the next tick selects from the first node
    if (node.current + 1 < int(node.tasks.size()))
    {
        task.def->task->OnExit(*task.context);
        EnterTask(node.tasks[++node.current]);
        return EActionRunStatus::Running;
    }

    FinishNode(node, status);
    return EActionRunStatus::Running;
}

void STAI_TreeInstance::FinishNode(RuntimeNode& node, EActionRunStatus status)
{
    // the first matching transition whose conditions pass replaces the selection from the first node on the next tick
    for (size_t t = 0; t < node.def->transitions.size(); ++t)
    {
        const STAI_TransitionDef& transition = node.def->transitions[t];
        if (transition.Matches(status) && CheckConditions(node.transitionConditions[t]))
        {
            pendingTransition = transition.target;
            break;
        }
    }

    ExitActive();
}

IReactionChecker* STAI_TreeInstance::FindReactionChecker() const
{
    for (const TaskSlot& global : globals)
        if (IReactionChecker* checker = global.def->task->GetReactionChecker(*global.context))
            return checker;
    for (const RuntimeNode& node : nodes)
        for (const TaskSlot& task : node.tasks)
            if (IReactionChecker* checker = task.def->task->GetReactionChecker(*task.context))
                return checker;
    return nullptr;
}

Engine* STAI_TreeInstance::GetDebugEngine() const
{
    if (active >= 0)
    {
        const TaskSlot& task = nodes[active].tasks[nodes[active].current];
        if (Engine* engine = task.def->task->GetDebugEngine(*task.context))
            return engine;
    }

    for (const RuntimeNode& node : nodes)
        for (const TaskSlot& task : node.tasks)
            if (Engine* engine = task.def->task->GetDebugEngine(*task.context))
                return engine;

    return nullptr;
}

void STAI_TreeInstance::AppendDebugTreeStack(std::vector<StateNodeDebugTree>& stack) const
{
    StateNodeDebugTree tree;
    tree.name = asset->name;
    tree.running = running;

    for (const TaskSlot& global : globals)
        tree.globalTasks.push_back(global.def->taskClass);

    for (size_t i = 0; i < nodes.size(); ++i)
    {
        const RuntimeNode& node = nodes[i];
        const TaskSlot& shown = node.tasks[node.current >= 0 ? node.current : 0];

        StateNodeDebugChild child;
        child.name = node.def->name;
        child.active = int(i) == active;
        child.taskClass = shown.def->task->GetTaskClass();
        child.info = shown.def->task->GetDebugInfo(*shown.context);
        child.currentTask = node.current;
        for (const STAI_TransitionDef& transition : node.def->transitions)
            child.transitions.push_back(transition.GetText());
        for (const TaskSlot& task : node.tasks)
            child.tasks.push_back(task.def->task->GetTaskClass());

        for (size_t c = 0; c < node.def->conditions.size(); ++c)
            child.conditions.emplace_back(node.def->conditions.list[c].GetText(), node.results[c]);
        child.conditionsAny = node.def->conditions.any;

        tree.nodes.push_back(std::move(child));
    }

    stack.push_back(std::move(tree));

    if (active >= 0)
    {
        const TaskSlot& task = nodes[active].tasks[nodes[active].current];
        task.def->task->AppendDebugTreeStack(*task.context, stack);
    }
}

const char* STAI_TreeInstance::GetActiveNodeName() const
{
    return active >= 0 ? nodes[active].def->name.c_str() : nullptr;
}
