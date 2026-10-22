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
}

void Task_RunSubtree::Validate(const STAI_TaskConfig& config)
{
    // the file itself is checked once every tree is loaded (missing subtrees, cycles)
    if (GetSubtreeFile(config).empty())
        throw std::runtime_error("missing <Subtree filename=\"statetree/STAI_*.xml\"/>");
}

std::unique_ptr<STAI_Task> Task_RunSubtree::Create(const STAI_TaskConfig& config)
{
    return std::make_unique<Task_RunSubtree>(GetSubtreeFile(config));
}

const STAI_TaskContextType& Task_RunSubtree::GetContextType() const
{
    return STAI_TaskContextType::Of<Task_RunSubtreeContext>();
}

void Task_RunSubtree::InitContext(STAI_TaskContext& baseContext) const
{
    Task_RunSubtreeContext& context = ContextAs<Task_RunSubtreeContext>(baseContext);

    std::shared_ptr<const STAI_TreeAsset> asset = sSTAITreeLibrary.Find(file);
    if (!asset)
    {
        sLog.outError("StateTree: subtree %s is not loaded, the node of %s always fails", file.c_str(), context.bot->GetName());
        return;
    }

    // the subtree's contexts share the bot and state of this node
    context.subtree = std::make_unique<STAI_TreeInstance>(context, std::move(asset));
}

void Task_RunSubtree::OnEnter(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(Task_RunSubtree::OnEnter);
    Task_RunSubtreeContext& context = ContextAs<Task_RunSubtreeContext>(baseContext);
    if (context.subtree)
        context.subtree->OnStart();
}

EActionRunStatus Task_RunSubtree::OnTick(STAI_TaskContext& baseContext, uint32 elapsed, bool minimal) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(Task_RunSubtree::OnTick);
    Task_RunSubtreeContext& context = ContextAs<Task_RunSubtreeContext>(baseContext);
    if (!context.subtree)
        return EActionRunStatus::Failed;

    // Succeeded once the subtree finished itself, it never fails
    return context.subtree->OnTick(elapsed, minimal) == EActionRunStatus::Succeeded ? EActionRunStatus::Succeeded : EActionRunStatus::Running;
}

void Task_RunSubtree::OnExit(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(Task_RunSubtree::OnExit);
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
