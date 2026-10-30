#include "Task_RunSubtree.h"

#include "playerbot/PlayerbotAI.h"
#include "playerbot/StateTree/STAI_TreeInstance.h"

#include <stdexcept>

using namespace ai;

namespace
{
    std::string GetSubtreeFile(const STAI_TaskConfig& config)
    {
        return STAI_TaskFactory::GetSetting(config, "Subtree", "filename").value_or("");
    }
}

Task_RunSubtreeContext::Task_RunSubtreeContext() = default;
Task_RunSubtreeContext::~Task_RunSubtreeContext() = default;

Task_RunSubtree::Task_RunSubtree(const std::string& file)
    : file(file)
{
    bShowLog = false;
}

void Task_RunSubtree::Validate(const STAI_TaskConfig& config)
{
    // the file itself is checked once every tree is loaded (missing subtrees, cycles)
    if (GetSubtreeFile(config).empty())
        throw std::runtime_error("missing <Subtree filename=\"statetree/STAI_*.xml\"/>");
}

std::unique_ptr<STAI_TaskBase> Task_RunSubtree::Create(const STAI_TaskConfig& config)
{
    return std::make_unique<Task_RunSubtree>(GetSubtreeFile(config));
}

void Task_RunSubtree::LinkReferencedTrees(const std::function<std::shared_ptr<const STAI_TreeAsset>(const std::string& file)>& find)
{
    tree = find(file);
}

const STAI_TaskContextType& Task_RunSubtree::GetContextType() const
{
    return STAI_TaskContextType::Of<Task_RunSubtreeContext>();
}

EActionRunStatus Task_RunSubtree::OnEnterState(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(Task_RunSubtree::OnEnterState);
    Task_RunSubtreeContext& context = ContextAs<Task_RunSubtreeContext>(baseContext);
    if (!context.created)
    {
        context.created = true;
        if (!tree)
            sLog.outError("StateTree: subtree %s is not loaded, the node of %s always fails", file.c_str(), context.bot->GetName());
        else
        {
            // the subtree's contexts share the bot, state and activity log of this node
            const STAI_TaskContext base(context.ai, context.bot, context.state, context.activity);
            context.subtree = std::make_unique<STAI_TreeInstance>(base, tree);
        }
    }

    if (!context.subtree)
        return EActionRunStatus::Failed;

    context.subtree->OnStart();
    return EActionRunStatus::Running;
}

EActionRunStatus Task_RunSubtree::OnTick(STAI_TaskContext& baseContext, uint32 elapsed, bool minimal) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(Task_RunSubtree::OnTick);
    Task_RunSubtreeContext& context = ContextAs<Task_RunSubtreeContext>(baseContext);

    // finished once the subtree finished itself: Succeeded, or Failed through a transition to Failed
    return context.subtree->OnTick(elapsed, minimal);
}

void Task_RunSubtree::OnExitState(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(Task_RunSubtree::OnExitState);
    Task_RunSubtreeContext& context = ContextAs<Task_RunSubtreeContext>(baseContext);
    if (context.subtree && context.subtree->IsRunning())
        context.subtree->OnFinish();
}

bool Task_RunSubtree::HoldsActiveTree(const STAI_TaskContext& baseContext) const
{
    const Task_RunSubtreeContext& context = ContextAs<Task_RunSubtreeContext>(baseContext);
    return context.subtree && context.subtree->IsRunning();
}

STAI_TreeInstance* Task_RunSubtree::GetRunningSubtree(STAI_TaskContext& baseContext) const
{
    Task_RunSubtreeContext& context = ContextAs<Task_RunSubtreeContext>(baseContext);
    return context.subtree && context.subtree->IsRunning() ? context.subtree.get() : nullptr;
}

void Task_RunSubtree::AppendDebugTreeStack(const STAI_TaskContext& baseContext, std::vector<StateNodeDebugTree>& stack) const
{
    const Task_RunSubtreeContext& context = ContextAs<Task_RunSubtreeContext>(baseContext);
    if (context.subtree && context.subtree->IsRunning())
        context.subtree->AppendDebugTreeStack(stack);
}
