#include "StateNode_Tree.h"

#include "playerbot/PlayerbotAI.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/Trigger.h"

using namespace ai;

namespace
{
    // a node whose task failed is not entered again for this long (avoids re-entering a failing task every update)
    constexpr uint32 failedRetryDelayMs = 3000;
}

StateNode_Tree::StateNode_Tree(const STAI_TaskContext& context, std::shared_ptr<const STAI_TreeAsset> asset)
    : context(context), asset(std::move(asset))
{
    for (const STAI_NodeDef& def : this->asset->nodes)
    {
        RuntimeNode node;
        node.def = &def;
        if (const STAI_TaskFactory::TaskClass* taskClass = sSTAITaskFactory.FindTask(def.taskClass))
            node.task = taskClass->create(context, def.taskConfig);
        if (!node.task)
            continue;

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
}

void StateNode_Tree::UpdateStrategies(const std::string& name, BotState type)
{
    for (RuntimeNode& node : nodes)
        node.task->UpdateStrategies(name, type);
}

void StateNode_Tree::ResetStrategies()
{
    for (RuntimeNode& node : nodes)
        node.task->ResetStrategies();
}

void StateNode_Tree::ReInit()
{
    for (RuntimeNode& node : nodes)
        node.task->ReInit();
}

bool StateNode_Tree::CanEnter(RuntimeNode& node, uint32 now)
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

void StateNode_Tree::ExitActive()
{
    if (active < 0)
        return;

    nodes[active].task->OnExit();
    active = -1;
}

void StateNode_Tree::OnExit()
{
    ExitActive();
}

void StateNode_Tree::Update(uint32 elapsed, bool minimal)
{
    const uint32 now = WorldTimer::getMSTime();

    // the running node keeps the results it was entered with, the others show only what this update checked
    for (int i = 0; i < int(nodes.size()); ++i)
        if (i != active)
            std::fill(nodes[i].results.begin(), nodes[i].results.end(), StateNodeConditionResult::NotChecked);

    // earlier nodes have priority: they interrupt the running node when their conditions pass
    const int limit = active >= 0 ? active : int(nodes.size());
    for (int i = 0; i < limit; ++i)
    {
        if (!CanEnter(nodes[i], now))
            continue;

        ExitActive();
        active = i;
        nodes[i].failed = false;
        nodes[i].task->OnEnter();
        break;
    }

    if (active < 0)
        return;

    RuntimeNode& node = nodes[active];
    const EActionRunStatus status = node.task->Update(elapsed, minimal);
    if (status == EActionRunStatus::Running)
        return;

    if (status == EActionRunStatus::Failed)
    {
        node.failed = true;
        node.failedAtMs = now;
    }

    ExitActive();
}

IReactionChecker* StateNode_Tree::FindReactionChecker() const
{
    for (const RuntimeNode& node : nodes)
        if (IReactionChecker* checker = node.task->GetReactionChecker())
            return checker;
    return nullptr;
}

bool StateNode_Tree::ShouldEnterReaction()
{
    IReactionChecker* checker = FindReactionChecker();
    return checker && checker->ShouldEnterReaction();
}

bool StateNode_Tree::IsReacting()
{
    IReactionChecker* checker = FindReactionChecker();
    return checker && checker->IsReacting();
}

Engine* StateNode_Tree::GetDebugEngine() const
{
    if (active >= 0)
        if (Engine* engine = nodes[active].task->GetDebugEngine())
            return engine;

    for (const RuntimeNode& node : nodes)
        if (Engine* engine = node.task->GetDebugEngine())
            return engine;

    return nullptr;
}

std::vector<StateNodeDebugChild> StateNode_Tree::GetDebugChildren() const
{
    std::vector<StateNodeDebugChild> children;
    for (size_t i = 0; i < nodes.size(); ++i)
    {
        const RuntimeNode& node = nodes[i];

        StateNodeDebugChild child;
        child.name = node.def->name;
        child.taskClass = node.task->GetTaskClass();
        child.active = int(i) == active;
        child.info = node.task->GetDebugInfo();

        for (size_t c = 0; c < node.def->conditions.size(); ++c)
            child.conditions.emplace_back(node.def->conditions[c].GetText(), node.results[c]);

        children.push_back(std::move(child));
    }
    return children;
}
