#include "STAI_TreeInstance.h"

#include "playerbot/PlayerbotAI.h"
#include "playerbot/StateTree/Tasks/STAI_DebugSpells.h"
#include "playerbot/StateTree/STAI_ActivityLog.h"
#include "Util/Util.h"
#include "World/ProfilingSubsystem.h"

#include <algorithm>

using namespace ai;

namespace
{
    // tasks finishing at once hand over in the same update (next state, the selection), at most this many per update
    constexpr int maxStepsPerUpdate = 8;
}

STAI_TreeInstance::STAI_TreeInstance(const STAI_TaskContext& context, std::shared_ptr<const STAI_TreeAsset> asset)
    : context(context), asset(std::move(asset))
{
    for (const STAI_TaskDef& task : this->asset->globalTasks)
    {
        TaskSlot slot;
        slot.def = &task;
        globals.push_back(std::move(slot));
    }

    for (const STAI_StateDef& def : this->asset->states)
    {
        RuntimeState state;
        state.def = &def;
        for (const STAI_TaskDef& task : def.tasks)
        {
            TaskSlot slot;
            slot.def = &task;
            state.tasks.push_back(std::move(slot));
        }
        state.results.assign(def.enterConditions.size(), StateNodeConditionResult::NotChecked);
        state.conditionOverrides.assign(def.enterConditions.size(), 0);
        state.transitionDelegates.assign(def.transitions.size(), -1);
        states.push_back(std::move(state));
    }

    CreateContexts();
    ResolveMembers();
}

STAI_TreeInstance::~STAI_TreeInstance()
{
    for (STAI_TaskContext* nodeContext : contexts)
        if (nodeContext)
            nodeContext->~STAI_TaskContext();
}

void STAI_TreeInstance::CreateContexts()
{
    // lay the contexts out like a stack frame: each at the next offset its alignment allows
    std::vector<size_t> offsets;
    size_t size = 0;
    for (const STAI_NodeEntry& entry : asset->nodes)
    {
        const STAI_TaskContextType& type = entry.node->GetContextType();
        MANGOS_ASSERT(type.alignment <= alignof(std::max_align_t));
        size = (size + type.alignment - 1) / type.alignment * type.alignment;
        offsets.push_back(size);
        size += type.size;
    }

    contextStorage.reset(new std::max_align_t[std::max<size_t>(1, (size + sizeof(std::max_align_t) - 1) / sizeof(std::max_align_t))]);
    std::byte* memory = reinterpret_cast<std::byte*>(contextStorage.get());
    contexts.assign(asset->nodes.size(), nullptr);
    for (size_t i = 0; i < asset->nodes.size(); ++i)
    {
        STAI_TaskContext* nodeContext = asset->nodes[i].node->GetContextType().construct(memory + offsets[i], context);
        nodeContext->tree = &asset->name;
        nodeContext->instance = this;
        nodeContext->ownerState = asset->nodes[i].state;
        contexts[i] = nodeContext;
    }

    for (TaskSlot& global : globals)
        global.context = contexts[global.def->node];
    for (RuntimeState& state : states)
        for (TaskSlot& task : state.tasks)
            task.context = contexts[task.def->node];
}

void STAI_TreeInstance::ResolveMembers()
{
    // bindings: the local and the external value in the bot's contexts
    auto resolveBindings = [this](TaskSlot& slot)
    {
        for (const STAI_BindingDef& binding : slot.def->bindings)
            slot.bindings.push_back({ binding.localProperty->get(*slot.context), binding.externalProperty->get(*contexts[binding.external.node]), binding.localProperty });
    };
    for (TaskSlot& global : globals)
        resolveBindings(global);
    for (RuntimeState& state : states)
        for (TaskSlot& task : state.tasks)
            resolveBindings(task);

    // properties read by path: the value in the bot's context of the referenced task
    for (size_t i = 0; i < asset->nodes.size(); ++i)
    {
        const std::vector<STAI_PropertyRef>& refs = asset->nodes[i].node->GetPropertyRefs();
        contexts[i]->refs.clear();
        for (const STAI_PropertyRef& ref : refs)
            contexts[i]->refs.push_back(ref.property->get(*contexts[ref.ref.node]));
    }

    // OnDelegate transitions: one remembered flag per source delegate
    for (RuntimeState& state : states)
    {
        for (size_t t = 0; t < state.def->transitions.size(); ++t)
        {
            const STAI_TransitionDef& transition = state.def->transitions[t];
            if (transition.trigger != ESTAI_TransitionTrigger::OnDelegate)
                continue;

            FSTAI_Delegate* delegate = transition.delegate->get(*contexts[transition.source.node]);
            auto known = std::find(delegates.begin(), delegates.end(), delegate);
            const size_t index = size_t(known - delegates.begin());
            if (known == delegates.end())
            {
                delegates.push_back(delegate);
                // the delegate lives in a context destroyed with the tree: no unsubscribing needed
                delegate->AddLambda([this, index]() { firedDelegates[index] = 1; });
            }
            state.transitionDelegates[t] = int(index);
        }
    }

    firedDelegates.assign(delegates.size(), 0);
}

bool STAI_TreeInstance::TestExpr(const STAI_ConditionExpr& expr) const
{
    switch (expr.op)
    {
        case STAI_ConditionExpr::Op::Leaf:
            return expr.condition->TestCondition(*contexts[expr.node]);
        case STAI_ConditionExpr::Op::All:
            for (const STAI_ConditionExpr& child : expr.children)
                if (!TestExpr(child))
                    return false;
            return true;
        default:
            for (const STAI_ConditionExpr& child : expr.children)
                if (TestExpr(child))
                    return true;
            return false;
    }
}

bool STAI_TreeInstance::TestConditions(const STAI_ConditionsDef& conditions, std::vector<StateNodeConditionResult>* results,
    const std::vector<uint8>* overrides) const
{
    if (conditions.empty())
        return true;

    // all: stops at the first failure; any: stops at the first pass (the rest is not checked)
    for (size_t i = 0; i < conditions.size(); ++i)
    {
        const uint8 forced = overrides ? (*overrides)[i] : 0;
        const bool passed = forced ? forced == 1 : TestExpr(conditions.list[i]);
        if (results)
            (*results)[i] = passed ? StateNodeConditionResult::Passed : StateNodeConditionResult::Failed;
        if (passed == conditions.any)
            return passed;
    }
    return !conditions.any;
}

void STAI_TreeInstance::ForEachCondition(const STAI_ConditionsDef& conditions, const std::function<void(const STAI_ConditionBase&, STAI_TaskContext&)>& call) const
{
    std::function<void(const STAI_ConditionExpr&)> visit = [&](const STAI_ConditionExpr& expr)
    {
        if (expr.op == STAI_ConditionExpr::Op::Leaf)
            call(*expr.condition, *contexts[expr.node]);
        for (const STAI_ConditionExpr& child : expr.children)
            visit(child);
    };
    for (const STAI_ConditionExpr& expr : conditions.list)
        visit(expr);
}

float STAI_TreeInstance::GetUtility(int state) const
{
    const STAI_StateDef& def = *states[state].def;
    float utility = def.weight;
    for (const STAI_ConsiderationDef& consideration : def.considerations)
        utility *= consideration.consideration->GetScore(*contexts[consideration.node]);
    return utility;
}

bool STAI_TreeInstance::CanSelect(int index, uint32 now, bool ignoreRetryDelay)
{
    RuntimeState& state = states[index];
    const STAI_StateDef& def = *state.def;
    std::fill(state.results.begin(), state.results.end(), StateNodeConditionResult::NotChecked);

    // switched off in the file or in the addon
    if (!def.enabled || state.disabled)
        return false;

    if (!ignoreRetryDelay && state.failed && def.retryDelayMs && WorldTimer::getMSTimeDiff(state.failedAtMs, now) < def.retryDelayMs)
        return false;

    // a leaf whose tasks are all switched off: nothing to run
    if (def.children.empty() && def.type != ESTAI_StateType::Linked &&
        std::none_of(state.tasks.begin(), state.tasks.end(), [](const TaskSlot& slot) { return !slot.context->bDisabled; }))
        return false;

    return TestConditions(def.enterConditions, &state.results, &state.conditionOverrides);
}

bool STAI_TreeInstance::SelectState(int index, std::vector<int>& path, uint32 now, bool ignoreRetryDelay)
{
    if (!CanSelect(index, now, ignoreRetryDelay))
        return false;

    const size_t mark = path.size();
    path.push_back(index);

    const STAI_StateDef& def = *states[index].def;
    // Linked: the subtree state runs below it, its children selected as usual
    if (def.type == ESTAI_StateType::Linked)
    {
        if (SelectState(def.linked, path, now, true))
            return true;
        path.resize(mark);
        return false;
    }

    if (def.children.empty() || SelectChildren(index, path, now))
        return true;

    path.resize(mark);
    return false;
}

bool STAI_TreeInstance::SelectChildren(int index, std::vector<int>& path, uint32 now)
{
    const STAI_StateDef& def = *states[index].def;

    // Subtree states are only entered through Linked states and transitions
    std::vector<int> candidates;
    for (int child : def.children)
        if (states[child].def->type != ESTAI_StateType::Subtree)
            candidates.push_back(child);

    auto tryInOrder = [&](const std::vector<int>& order)
    {
        for (int child : order)
            if (SelectState(child, path, now, false))
                return true;
        return false;
    };

    switch (def.selection)
    {
        case ESTAI_StateSelection::TrySelectChildrenAtRandom:
        {
            for (size_t i = candidates.size(); i > 1; --i)
                std::swap(candidates[i - 1], candidates[urand(0, uint32(i - 1))]);
            return tryInOrder(candidates);
        }
        case ESTAI_StateSelection::TrySelectChildrenWithHighestUtility:
        {
            // states without utility are not selected
            std::vector<std::pair<float, int>> scored;
            for (int child : candidates)
                if (const float utility = GetUtility(child); utility > 0.0f)
                    scored.emplace_back(utility, child);
            std::stable_sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
            for (const auto& [utility, child] : scored)
                if (SelectState(child, path, now, false))
                    return true;
            return false;
        }
        case ESTAI_StateSelection::TrySelectChildrenAtRandomWeightedByUtility:
        {
            std::vector<std::pair<float, int>> scored;
            for (int child : candidates)
                if (const float utility = GetUtility(child); utility > 0.0f)
                    scored.emplace_back(utility, child);
            while (!scored.empty())
            {
                float total = 0.0f;
                for (const auto& entry : scored)
                    total += entry.first;
                float roll = frand(0.0f, total);
                size_t pick = 0;
                while (pick + 1 < scored.size() && roll > scored[pick].first)
                    roll -= scored[pick++].first;
                if (SelectState(scored[pick].second, path, now, false))
                    return true;
                scored.erase(scored.begin() + pick);
            }
            return false;
        }
        case ESTAI_StateSelection::TryFollowTransitions:
        {
            // the first of the state's own transitions to a state below it whose conditions pass and whose path can be entered
            for (const STAI_TransitionDef& transition : def.transitions)
            {
                if (transition.target != STAI_TransitionDef::Target::State || transition.state <= index || transition.state >= def.subtreeEnd)
                    continue;
                if (transition.trigger == ESTAI_TransitionTrigger::OnDelegate || transition.trigger == ESTAI_TransitionTrigger::OnEvent)
                    continue;
                if (!TestConditions(transition.conditions))
                    continue;

                std::vector<int> chain;
                for (int state = states[transition.state].def->parent; state != index && state >= 0; state = states[state].def->parent)
                    chain.insert(chain.begin(), state);

                const size_t mark = path.size();
                bool entered = true;
                for (int state : chain)
                {
                    if (!CanSelect(state, now, false))
                    {
                        entered = false;
                        break;
                    }
                    path.push_back(state);
                }
                if (entered && SelectState(transition.state, path, now, false))
                    return true;
                path.resize(mark);
            }
            return false;
        }
        default:
            return tryInOrder(candidates);
    }
}

bool STAI_TreeInstance::SelectFromRoot(std::vector<int>& path, uint32 now)
{
    for (int state : asset->topStates)
        if (states[state].def->type != ESTAI_StateType::Subtree && SelectState(state, path, now, false))
            return true;
    return false;
}

bool STAI_TreeInstance::SelectTarget(int target, std::vector<int>& path, uint32 now, bool ignoreRetryDelay)
{
    // the target's parents, top first
    std::vector<int> chain;
    for (int state = states[target].def->parent; state >= 0; state = states[state].def->parent)
        chain.insert(chain.begin(), state);

    // the active path is kept down to the deepest active parent (in a running linked subtree: down through its
    // Linked state), the parents below it must be enterable
    path.clear();
    size_t next = 0;
    for (size_t i = chain.size(); i-- > 0; )
    {
        auto found = std::find(active.begin(), active.end(), chain[i]);
        if (found == active.end())
            continue;
        path.assign(active.begin(), found + 1);
        next = i + 1;
        break;
    }

    for (size_t i = next; i < chain.size(); ++i)
    {
        if (!CanSelect(chain[i], now, ignoreRetryDelay))
            return false;
        path.push_back(chain[i]);
    }
    return SelectState(target, path, now, ignoreRetryDelay);
}

void STAI_TreeInstance::ChangeActive(const std::vector<int>& path, size_t reenterFrom, uint32 now)
{
    size_t keep = 0;
    while (keep < active.size() && keep < path.size() && keep < reenterFrom && active[keep] == path[keep])
        ++keep;

    while (active.size() > keep)
    {
        ExitStateAt(active.back());
        active.pop_back();
    }

    for (size_t i = keep; i < path.size(); ++i)
    {
        active.push_back(path[i]);
        EnterStateAt(path[i], now);
    }

    if (delayed.state >= 0 && !states[delayed.state].active)
        delayed = {};
}

void STAI_TreeInstance::EnterStateAt(int index, uint32 now)
{
    RuntimeState& state = states[index];
    state.active = true;
    state.enteredAtMs = now;
    state.failed = false;

    auto enter = [](const STAI_ConditionBase& condition, STAI_TaskContext& nodeContext) { condition.EnterState(nodeContext); };
    ForEachCondition(state.def->enterConditions, enter);
    for (const STAI_TransitionDef& transition : state.def->transitions)
        ForEachCondition(transition.conditions, enter);

    for (TaskSlot& slot : state.tasks)
    {
        if (slot.context->bDisabled)
            continue;
        slot.enterStatus = EnterTask(slot);
        slot.entered = true;
    }
}

void STAI_TreeInstance::ExitStateAt(int index)
{
    RuntimeState& state = states[index];

    // opposite to the order they were entered
    for (auto slot = state.tasks.rbegin(); slot != state.tasks.rend(); ++slot)
        ExitTask(*slot);

    auto exit = [](const STAI_ConditionBase& condition, STAI_TaskContext& nodeContext) { condition.ExitState(nodeContext); };
    for (const STAI_TransitionDef& transition : state.def->transitions)
        ForEachCondition(transition.conditions, exit);
    ForEachCondition(state.def->enterConditions, exit);

    state.active = false;
}

void STAI_TreeInstance::ExitActive()
{
    while (!active.empty())
    {
        ExitStateAt(active.back());
        active.pop_back();
    }
    delayed = {};
}

STAI_TreeInstance::TransitionRef STAI_TreeInstance::FindTransition(int depth, const std::function<bool(const STAI_TransitionDef&)>& matches) const
{
    // from the leaf up, the first of the highest priority
    TransitionRef best;
    ESTAI_TransitionPriority bestPriority = ESTAI_TransitionPriority::Low;
    for (int d = std::min(depth, int(active.size()) - 1); d >= 0; --d)
    {
        const STAI_StateDef& def = *states[active[d]].def;
        for (size_t t = 0; t < def.transitions.size(); ++t)
        {
            const STAI_TransitionDef& transition = def.transitions[t];
            if ((best.state >= 0 && transition.priority <= bestPriority) || !matches(transition) || !TestConditions(transition.conditions))
                continue;
            best = { active[d], int(t) };
            bestPriority = transition.priority;
        }
    }
    return best;
}

bool STAI_TreeInstance::TakeTriggeredTransition(uint32 now)
{
    // every triggered transition of the active states whose conditions pass, from the leaf up
    std::vector<std::pair<ESTAI_TransitionPriority, TransitionRef>> triggered;
    std::vector<int> used;
    for (int d = int(active.size()) - 1; d >= 0; --d)
    {
        const RuntimeState& state = states[active[d]];
        for (size_t t = 0; t < state.def->transitions.size(); ++t)
        {
            const STAI_TransitionDef& transition = state.def->transitions[t];
            switch (transition.trigger)
            {
                case ESTAI_TransitionTrigger::OnDelegate:
                {
                    const int delegate = state.transitionDelegates[t];
                    if (!firedDelegates[delegate])
                        continue;
                    // taken or not, the delegate is used up: it fires again when it has news
                    used.push_back(delegate);
                    break;
                }
                case ESTAI_TransitionTrigger::OnEvent:
                    if (std::find(events.begin(), events.end(), transition.event) == events.end())
                        continue;
                    break;
                case ESTAI_TransitionTrigger::OnTick:
                    break;
                default:
                    continue;
            }

            if (TestConditions(transition.conditions))
                triggered.emplace_back(transition.priority, TransitionRef{ active[d], int(t) });
        }
    }
    for (int delegate : used)
        firedDelegates[delegate] = 0;

    // the highest priority first, the deepest state first among equal ones
    std::stable_sort(triggered.begin(), triggered.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    for (const auto& [priority, transition] : triggered)
        if (TakeTransition(transition, now, false))
            return true;
    return false;
}

bool STAI_TreeInstance::TakeTransition(const TransitionRef& transition, uint32 now, bool ignoreRetryDelay)
{
    const STAI_TransitionDef& def = states[transition.state].def->transitions[transition.transition];
    if (!def.delayMs)
        return ApplyTransition(transition, now, ignoreRetryDelay);

    // a delayed transition: taken once its time came, while its state stays active
    if (!(delayed == transition))
    {
        delayed = transition;
        delayedSinceMs = now;
    }
    return true;
}

bool STAI_TreeInstance::ApplyTransition(const TransitionRef& transition, uint32 now, bool ignoreRetryDelay)
{
    const STAI_TransitionDef& def = states[transition.state].def->transitions[transition.transition];
    switch (def.target)
    {
        case STAI_TransitionDef::Target::Root:
            // the selection starts from Root
            ExitActive();
            return true;
        case STAI_TransitionDef::Target::Succeeded:
            CompleteFrom(transition.state, EActionRunStatus::Succeeded, now);
            return true;
        case STAI_TransitionDef::Target::Failed:
            CompleteFrom(transition.state, EActionRunStatus::Failed, now);
            return true;
        default:
        {
            std::vector<int> path;
            if (!SelectTarget(def.state, path, now, ignoreRetryDelay))
                return false;
            const size_t target = size_t(std::find(path.begin(), path.end(), def.state) - path.begin());
            ChangeActive(path, target, now);
            return true;
        }
    }
}

void STAI_TreeInstance::CompleteFrom(int state, EActionRunStatus status, uint32 now)
{
    // the Linked state running the subtree the transition's state is in
    const int position = int(std::find(active.begin(), active.end(), state) - active.begin());
    int linked = -1;
    for (int d = std::min(position, int(active.size())) - 1; d >= 0 && linked < 0; --d)
        if (states[active[d]].def->type == ESTAI_StateType::Linked)
            linked = d;

    if (linked < 0)
    {
        finishRequest = status;
        return;
    }

    // the linked state completed: its completion transitions and those above it
    const TransitionRef next = FindTransition(linked, [status](const STAI_TransitionDef& transition) { return transition.Matches(status); });
    if (next.state >= 0 && ApplyTransition(next, now, true))
        return;
    ExitActive();
}

void STAI_TreeInstance::HandleCompletion(EActionRunStatus status, uint32 now)
{
    // RedStateTree's StateCompleted on every task of the active states
    for (int index : active)
        for (TaskSlot& slot : states[index].tasks)
            if (slot.entered)
                slot.def->task->StateCompleted(*slot.context, status);

    // failed states are not selected again for their retry delay
    if (status == EActionRunStatus::Failed)
    {
        for (int index : active)
        {
            states[index].failed = true;
            states[index].failedAtMs = now;
        }
    }

    const TransitionRef next = FindTransition(int(active.size()) - 1, [status](const STAI_TransitionDef& transition) { return transition.Matches(status); });
    if (next.state >= 0)
    {
        const STAI_TransitionDef& def = states[next.state].def->transitions[next.transition];
        // the target is entered ignoring its retry delay; when it cannot be entered the selection starts from Root
        if (def.delayMs ? TakeTransition(next, now, true) : ApplyTransition(next, now, true))
            return;
    }
    ExitActive();
}

void STAI_TreeInstance::ApplyBindings(const TaskSlot& slot)
{
    for (const RuntimeBinding& binding : slot.bindings)
        binding.property->assign(binding.local, binding.external);
}

EActionRunStatus STAI_TreeInstance::EnterTask(TaskSlot& slot)
{
    ApplyBindings(slot);
    return slot.def->task->EnterState(*slot.context);
}

EActionRunStatus STAI_TreeInstance::TickTask(TaskSlot& slot, uint32 elapsed, bool minimal)
{
    ApplyBindings(slot);
    return slot.def->task->TickState(*slot.context, elapsed, minimal);
}

void STAI_TreeInstance::ExitTask(TaskSlot& slot)
{
    if (!slot.entered)
        return;
    slot.def->task->ExitState(*slot.context);
    slot.entered = false;
    slot.enterStatus = EActionRunStatus::Running;
}

void STAI_TreeInstance::CheckGlobalConditions(const TaskSlot& slot) const
{
    slot.context->bActive = TestConditions(slot.def->conditions);
}

void STAI_TreeInstance::EnterGlobals()
{
    if (running)
        return;

    running = true;
    for (TaskSlot& global : globals)
    {
        CheckGlobalConditions(global);
        const EActionRunStatus status = EnterTask(global);
        global.entered = true;
        if (status != EActionRunStatus::Running && finishRequest == EActionRunStatus::Running)
            finishRequest = status;
    }
}

void STAI_TreeInstance::ExitGlobals()
{
    if (!running)
        return;

    // opposite to the order they were entered
    for (auto global = globals.rbegin(); global != globals.rend(); ++global)
        ExitTask(*global);
    running = false;
}

void STAI_TreeInstance::OnStart()
{
    ExitActive();
    ExitGlobals();
    finishRequest = EActionRunStatus::Running;
    // delegates and events from before are stale, the global tasks fire again when they are entered
    std::fill(firedDelegates.begin(), firedDelegates.end(), 0);
    events.clear();
    incomingEvents.clear();
    EnterGlobals();
}

void STAI_TreeInstance::OnFinish()
{
    // opposite to the activation order (OnStart, global tasks, states top first, subtree OnStart, ...): the active
    // states from the leaf up, which finishes the trees stacked on them (Task_RunSubtree), then the global tasks
    ExitActive();
    ExitGlobals();
}

void STAI_TreeInstance::SendEvent(const std::string& tag)
{
    if (std::find(incomingEvents.begin(), incomingEvents.end(), tag) == incomingEvents.end())
        incomingEvents.push_back(tag);
}

uint32 STAI_TreeInstance::GetTimeInState(int state, uint32 now) const
{
    return IsStateActive(state) ? WorldTimer::getMSTimeDiff(states[state].enteredAtMs, now) : 0;
}

EActionRunStatus STAI_TreeInstance::OnTick(uint32 elapsed, bool minimal)
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(STAI_TreeInstance::OnTick);
    const uint32 now = WorldTimer::getMSTime();
    ++updateId;

    if (!running)
        OnStart();

    // the events sent since the last update
    events.swap(incomingEvents);
    incomingEvents.clear();

    auto finish = [this]()
    {
        const EActionRunStatus status = finishRequest;
        OnFinish();
        finishRequest = EActionRunStatus::Running;
        return status;
    };

    // global tasks: ticked whichever state runs, they finish the tree only when their completion says so
    for (TaskSlot& global : globals)
    {
        if (!global.def->task->ShouldCallTick())
            continue;
        CheckGlobalConditions(global);
        const EActionRunStatus status = TickTask(global, elapsed, minimal);
        if (status != EActionRunStatus::Running && finishRequest == EActionRunStatus::Running)
            finishRequest = status;
    }
    if (finishRequest != EActionRunStatus::Running)
        return finish();

    // an active state switched off in the addon: left, the selection starts from Root
    if (std::any_of(active.begin(), active.end(), [this](int state) { return states[state].disabled; }))
        ExitActive();

    // a delayed transition whose time came, then what the delegates, events and conditions trigger
    if (delayed.state >= 0 && WorldTimer::getMSTimeDiff(delayedSinceMs, now) >= states[delayed.state].def->transitions[delayed.transition].delayMs)
    {
        const TransitionRef transition = delayed;
        delayed = {};
        if (states[transition.state].active)
            ApplyTransition(transition, now, false);
    }
    else if (!active.empty())
        TakeTriggeredTransition(now);
    if (finishRequest != EActionRunStatus::Running)
        return finish();

    // tasks that finish at once hand over in the same update: a completion is followed by its transition or the
    // selection right away (bounded, so states completing at once cannot spin)
    for (int step = 0; step < maxStepsPerUpdate; ++step)
    {
        // no state is active: the first top level state whose path can be entered
        if (active.empty())
        {
            std::vector<int> path;
            if (SelectFromRoot(path, now))
                ChangeActive(path, 0, now);
            // none can be entered: the selection is tried again next update
            if (active.empty())
                return EActionRunStatus::Running;
        }

        // the tasks of the active states, top level first: each is processed once per update (a task entered in this
        // update gets its enter's result, then a tick with no elapsed time)
        EActionRunStatus completion = EActionRunStatus::Running;
        bool finishTree = false;
        for (int index : active)
        {
            RuntimeState& state = states[index];
            bool anyEntered = false;
            for (TaskSlot& slot : state.tasks)
            {
                if (!slot.entered)
                    continue;
                // switched off in the addon while it runs: left
                if (slot.context->bDisabled)
                {
                    ExitTask(slot);
                    continue;
                }
                anyEntered = true;
                if (slot.processedUpdate == updateId && slot.enterStatus == EActionRunStatus::Running)
                    continue;

                const bool firstTick = slot.processedUpdate != updateId;
                slot.processedUpdate = updateId;
                EActionRunStatus status = slot.enterStatus;
                slot.enterStatus = EActionRunStatus::Running;
                if (status == EActionRunStatus::Running && slot.def->task->ShouldCallTick() && firstTick)
                    status = TickTask(slot, step == 0 ? elapsed : 0, minimal);

                if (status == EActionRunStatus::Succeeded && slot.context->bFinishTree)
                    finishTree = true;
                if (status == EActionRunStatus::Failed)
                    completion = EActionRunStatus::Failed;
                else if (status == EActionRunStatus::Succeeded && completion == EActionRunStatus::Running)
                    completion = EActionRunStatus::Succeeded;
            }

            // a leaf whose tasks were all switched off is done
            if (!anyEntered && !state.tasks.empty() && state.def->children.empty() && completion == EActionRunStatus::Running)
                completion = EActionRunStatus::Succeeded;
        }

        if (finishTree)
        {
            finishRequest = EActionRunStatus::Succeeded;
            return finish();
        }
        if (completion == EActionRunStatus::Running)
            return EActionRunStatus::Running;

        HandleCompletion(completion, now);
        if (finishRequest != EActionRunStatus::Running)
            return finish();
        // a delayed completion transition waits with the completed states
        if (delayed.state >= 0)
            return EActionRunStatus::Running;
    }

    return EActionRunStatus::Running;
}

void STAI_TreeInstance::AppendDebugTreeStack(std::vector<StateNodeDebugTree>& stack) const
{
    StateNodeDebugTree tree;
    tree.name = asset->name;
    tree.running = running;

    for (const TaskSlot& global : globals)
    {
        tree.globalTasks.push_back(global.def->task->GetName());
        const ISTAI_DebugSpells* spells = dynamic_cast<const ISTAI_DebugSpells*>(global.def->task.get());
        tree.globalSpells.push_back(spells ? spells->GetDebugSpells(*global.context) : std::vector<uint32>());
    }

    for (size_t i = 0; i < states.size(); ++i)
    {
        const RuntimeState& state = states[i];

        StateNodeDebugChild child;
        STAI_DescribeState(*asset, int(i), child);
        child.active = state.active;
        child.disabled = state.disabled || !state.def->enabled;
        for (size_t c = 0; c < state.results.size() && c < child.conditions.size(); ++c)
            child.conditions[c].second = state.results[c];
        child.conditionOverrides = state.conditionOverrides;

        // the first running task, else the first one
        int runningTask = -1;
        for (size_t t = 0; t < state.tasks.size() && runningTask < 0; ++t)
            if (state.tasks[t].entered && state.tasks[t].context->bTaskActive)
                runningTask = int(t);
        child.currentTask = state.active ? runningTask : -1;

        if (!state.tasks.empty())
        {
            const TaskSlot& shown = state.tasks[runningTask >= 0 ? runningTask : 0];
            child.taskClass = shown.def->task->GetName();
            if (const ISTAI_DebugSpells* spells = dynamic_cast<const ISTAI_DebugSpells*>(shown.def->task.get()))
                child.spells = spells->GetDebugSpells(*shown.context);
            // the running task shows what its run does (Task_CastSpell: the rank it casts and why), the others their
            // configuration
            child.info = child.currentTask >= 0 ? shown.def->task->GetActivityInfo(*shown.context) : shown.def->task->GetDebugInfo();
        }

        for (size_t t = 0; t < state.tasks.size(); ++t)
        {
            const TaskSlot& task = state.tasks[t];
            child.tasks.push_back(task.def->task->GetName());
            child.tasksDisabled.push_back(task.context->bDisabled);
            child.taskInfos.push_back(child.currentTask == int(t) ? task.def->task->GetActivityInfo(*task.context) : task.def->task->GetDebugInfo());
            // a subtree that runs is on the stack above, else its file is shown down to the leaves
            if (const STAI_TreeAsset* subtree = task.def->task->GetLinkedTree())
                if (!task.def->task->HoldsActiveTree(*task.context))
                {
                    child.subtrees.push_back(subtree->GetDebugTree());
                    child.subtrees.back().task = int(t);
                }
        }

        tree.nodes.push_back(std::move(child));
    }

    stack.push_back(std::move(tree));

    for (int index : active)
        for (const TaskSlot& slot : states[index].tasks)
            if (slot.entered)
                slot.def->task->AppendDebugTreeStack(*slot.context, stack);
}

bool STAI_TreeInstance::SetDebugSwitch(StateTreeSwitch what, const std::string& tree, int node, int index, int value)
{
    // the snapshot shows the top tree of the stack: subtrees running on this tree first
    for (int state : active)
        for (TaskSlot& slot : states[state].tasks)
            if (slot.entered)
                if (STAI_TreeInstance* subtree = slot.def->task->GetRunningSubtree(*slot.context))
                    if (subtree->SetDebugSwitch(what, tree, node, index, value))
                        return true;

    if (asset->name != tree || node < 0 || node >= int(states.size()))
        return false;

    RuntimeState& state = states[node];
    switch (what)
    {
        case StateTreeSwitch::Task:
            if (index < 0 || index >= int(state.tasks.size()))
                return false;
            state.tasks[index].context->bDisabled = value != 0;
            return true;
        case StateTreeSwitch::Node:
            // an active state is left on the next update
            state.disabled = value != 0;
            return true;
        case StateTreeSwitch::Condition:
            if (index < 0 || index >= int(state.conditionOverrides.size()) || value < 0 || value > 2)
                return false;
            state.conditionOverrides[index] = uint8(value);
            return true;
    }
    return false;
}

const char* STAI_TreeInstance::GetActiveNodeName() const
{
    return active.empty() ? nullptr : states[active.back()].def->name.c_str();
}
