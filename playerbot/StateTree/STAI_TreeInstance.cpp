#include "STAI_TreeInstance.h"

#include "playerbot/PlayerbotAI.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/Trigger.h"
#include "playerbot/StateTree/STAI_ActivityLog.h"

using namespace ai;

namespace
{
    // a node whose task failed is not entered again for this long (avoids re-entering a failing task every update)
    constexpr uint32 failedRetryDelayMs = 3000;
}

STAI_TreeInstance::STAI_TreeInstance(const STAI_TaskContext& context, std::shared_ptr<const STAI_TreeAsset> asset)
    : context(context), asset(std::move(asset))
{
    for (const STAI_NodeDef& def : this->asset->nodes)
    {
        RuntimeNode node;
        node.def = &def;

        // conditions are the bot's triggers (class contexts included), so they can only be resolved per bot
        for (const STAI_ConditionDef& condition : def.conditions)
        {
            Trigger* trigger = context.ai->GetAiObjectContext()->GetTrigger(condition.trigger);
            if (!trigger)
                sLog.outError("StateTree: %s node '%s': unknown trigger '%s' for %s, the condition never passes",
                    this->asset->file.c_str(), def.name.c_str(), condition.trigger.c_str(), context.bot->GetName());
            node.triggers.push_back(trigger);
        }
        node.results.assign(def.conditions.size(), StateNodeConditionResult::NotChecked);

        nodes.push_back(std::move(node));
    }

    for (const STAI_NodeDef& def : this->asset->globalTasks)
        globals.push_back({ &def, nullptr });

    CreateContexts();
}

STAI_TreeInstance::~STAI_TreeInstance()
{
    for (GlobalTask& global : globals)
        if (global.context)
            global.context->~STAI_TaskContext();
    for (RuntimeNode& node : nodes)
        if (node.context)
            node.context->~STAI_TaskContext();
}

void STAI_TreeInstance::CreateContexts()
{
    // every task of the tree with the slot of its context: global tasks first, then the nodes
    std::vector<std::pair<const STAI_Task*, STAI_TaskContext**>> slots;
    for (GlobalTask& global : globals)
        slots.emplace_back(global.def->task.get(), &global.context);
    for (RuntimeNode& node : nodes)
        slots.emplace_back(node.def->task.get(), &node.context);

    // lay the contexts out like a stack frame: each at the next offset its alignment allows
    std::vector<size_t> offsets;
    size_t size = 0;
    for (const auto& [task, slot] : slots)
    {
        const STAI_TaskContextType& type = task->GetContextType();
        MANGOS_ASSERT(type.alignment <= alignof(std::max_align_t));
        size = (size + type.alignment - 1) / type.alignment * type.alignment;
        offsets.push_back(size);
        size += type.size;
    }

    contextStorage.reset(new std::max_align_t[(size + sizeof(std::max_align_t) - 1) / sizeof(std::max_align_t)]);
    std::byte* memory = reinterpret_cast<std::byte*>(contextStorage.get());
    for (size_t i = 0; i < slots.size(); ++i)
        *slots[i].second = slots[i].first->GetContextType().construct(memory + offsets[i], context);

    for (const auto& [task, slot] : slots)
        task->InitContext(**slot);
}

void STAI_TreeInstance::UpdateStrategies(const std::string& name, BotState type)
{
    for (GlobalTask& global : globals)
        global.def->task->UpdateStrategies(*global.context, name, type);
    for (RuntimeNode& node : nodes)
        node.def->task->UpdateStrategies(*node.context, name, type);
}

void STAI_TreeInstance::ResetStrategies()
{
    for (GlobalTask& global : globals)
        global.def->task->ResetStrategies(*global.context);
    for (RuntimeNode& node : nodes)
        node.def->task->ResetStrategies(*node.context);
}

void STAI_TreeInstance::ReInit()
{
    for (GlobalTask& global : globals)
        global.def->task->ReInit(*global.context);
    for (RuntimeNode& node : nodes)
        node.def->task->ReInit(*node.context);
}

bool STAI_TreeInstance::CanEnter(RuntimeNode& node, uint32 now)
{
    std::fill(node.results.begin(), node.results.end(), StateNodeConditionResult::NotChecked);

    if (node.failed && WorldTimer::getMSTimeDiff(node.failedAtMs, now) < failedRetryDelayMs)
        return false;

    // all conditions must pass; the ones after the first failure are not checked
    for (size_t i = 0; i < node.triggers.size(); ++i)
    {
        Trigger* trigger = node.triggers[i];
        const bool passed = trigger && trigger->IsActive() != node.def->conditions[i].negate;
        node.results[i] = passed ? StateNodeConditionResult::Passed : StateNodeConditionResult::Failed;
        if (!passed)
            return false;
    }

    return true;
}

void STAI_TreeInstance::ExitActive()
{
    if (active < 0)
        return;

    RuntimeNode& node = nodes[active];
    node.def->task->OnExit(*node.context);
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
    for (GlobalTask& global : globals)
        global.def->task->OnEnter(*global.context);
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
    ExitActive();
    ExitGlobals();
    EnterGlobals();
}

void STAI_TreeInstance::OnFinish()
{
    // opposite to the activation order (OnStart, global tasks OnEnter, node OnEnter, subtree OnStart, ...):
    // the running node first, which finishes the trees stacked on it (Task_RunSubtree), then the global tasks,
    // then the tree itself is finished
    ExitActive();
    ExitGlobals();
}

EActionRunStatus STAI_TreeInstance::OnTick(uint32 elapsed, bool minimal)
{
    const uint32 now = WorldTimer::getMSTime();

    if (!running)
        OnStart();

    // scopeless: ticked whichever node runs, the result does not end anything
    for (GlobalTask& global : globals)
        global.def->task->OnTick(*global.context, elapsed, minimal);

    // the running node keeps the results it was entered with, the others show only what this update checked
    for (int i = 0; i < int(nodes.size()); ++i)
        if (i != active)
            std::fill(nodes[i].results.begin(), nodes[i].results.end(), StateNodeConditionResult::NotChecked);

    // earlier nodes have priority: they interrupt the running node when their conditions pass; a tree below the
    // top of the stack (its running node runs a subtree) only ticks that node
    const bool holdsActiveTree = active >= 0 && nodes[active].def->task->HoldsActiveTree(*nodes[active].context);
    const int limit = holdsActiveTree ? 0 : active >= 0 ? active : int(nodes.size());
    for (int i = 0; i < limit; ++i)
    {
        if (!CanEnter(nodes[i], now))
            continue;

        ExitActive();
        active = i;
        RuntimeNode& node = nodes[i];
        node.failed = false;
        if (context.activity && node.def->task->LogsNodeActivity())
            node.activityId = context.activity->Begin("node", node.def->name, node.def->task->GetTaskClass());
        node.def->task->OnEnter(*node.context);
        break;
    }

    // every node evaluated, none can be entered: loops over to the first node next tick
    if (active < 0)
        return EActionRunStatus::Running;

    RuntimeNode& node = nodes[active];
    const EActionRunStatus status = node.def->task->OnTick(*node.context, elapsed, minimal);
    if (status == EActionRunStatus::Running)
        return status;

    if (status == EActionRunStatus::Failed)
    {
        node.failed = true;
        node.failedAtMs = now;
    }

    const bool finishTree = status == EActionRunStatus::Succeeded && node.def->task->FinishesTree();
    ExitActive();

    if (finishTree)
    {
        OnFinish();
        return EActionRunStatus::Succeeded;
    }

    // the node finished: loops over to the first node next tick
    return EActionRunStatus::Running;
}

IReactionChecker* STAI_TreeInstance::FindReactionChecker() const
{
    for (const GlobalTask& global : globals)
        if (IReactionChecker* checker = global.def->task->GetReactionChecker(*global.context))
            return checker;
    for (const RuntimeNode& node : nodes)
        if (IReactionChecker* checker = node.def->task->GetReactionChecker(*node.context))
            return checker;
    return nullptr;
}

Engine* STAI_TreeInstance::GetDebugEngine() const
{
    if (active >= 0)
        if (Engine* engine = nodes[active].def->task->GetDebugEngine(*nodes[active].context))
            return engine;

    for (const RuntimeNode& node : nodes)
        if (Engine* engine = node.def->task->GetDebugEngine(*node.context))
            return engine;

    return nullptr;
}

void STAI_TreeInstance::AppendDebugTreeStack(std::vector<StateNodeDebugTree>& stack) const
{
    StateNodeDebugTree tree;
    tree.name = asset->name;
    tree.running = running;

    for (const GlobalTask& global : globals)
        tree.globalTasks.push_back(global.def->name);

    for (size_t i = 0; i < nodes.size(); ++i)
    {
        const RuntimeNode& node = nodes[i];

        StateNodeDebugChild child;
        child.name = node.def->name;
        child.taskClass = node.def->task->GetTaskClass();
        child.active = int(i) == active;
        child.info = node.def->task->GetDebugInfo(*node.context);

        for (size_t c = 0; c < node.def->conditions.size(); ++c)
            child.conditions.emplace_back(node.def->conditions[c].GetText(), node.results[c]);

        tree.nodes.push_back(std::move(child));
    }

    stack.push_back(std::move(tree));

    if (active >= 0)
        nodes[active].def->task->AppendDebugTreeStack(*nodes[active].context, stack);
}

const char* STAI_TreeInstance::GetActiveNodeName() const
{
    return active >= 0 ? nodes[active].def->name.c_str() : nullptr;
}
