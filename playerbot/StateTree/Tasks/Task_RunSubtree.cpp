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
    Task_RunSubtreeContext& context = ContextAs<Task_RunSubtreeContext>(baseContext);
    if (context.subtree)
        context.subtree->OnStart();
}

EActionRunStatus Task_RunSubtree::OnTick(STAI_TaskContext& baseContext, uint32 elapsed, bool minimal) const
{
    Task_RunSubtreeContext& context = ContextAs<Task_RunSubtreeContext>(baseContext);
    if (!context.subtree)
        return EActionRunStatus::Failed;

    // Succeeded once the subtree finished itself, it never fails
    return context.subtree->OnTick(elapsed, minimal) == EActionRunStatus::Succeeded ? EActionRunStatus::Succeeded : EActionRunStatus::Running;
}

void Task_RunSubtree::OnExit(STAI_TaskContext& baseContext) const
{
    Task_RunSubtreeContext& context = ContextAs<Task_RunSubtreeContext>(baseContext);
    if (context.subtree && context.subtree->IsRunning())
        context.subtree->OnFinish();
}

bool Task_RunSubtree::HoldsActiveTree(const STAI_TaskContext& baseContext) const
{
    const Task_RunSubtreeContext& context = ContextAs<Task_RunSubtreeContext>(baseContext);
    return context.subtree && context.subtree->IsRunning();
}

void Task_RunSubtree::UpdateStrategies(STAI_TaskContext& baseContext, const std::string& name, BotState type) const
{
    Task_RunSubtreeContext& context = ContextAs<Task_RunSubtreeContext>(baseContext);
    if (context.subtree)
        context.subtree->UpdateStrategies(name, type);
}

void Task_RunSubtree::ResetStrategies(STAI_TaskContext& baseContext) const
{
    Task_RunSubtreeContext& context = ContextAs<Task_RunSubtreeContext>(baseContext);
    if (context.subtree)
        context.subtree->ResetStrategies();
}

void Task_RunSubtree::ReInit(STAI_TaskContext& baseContext) const
{
    Task_RunSubtreeContext& context = ContextAs<Task_RunSubtreeContext>(baseContext);
    if (context.subtree)
        context.subtree->ReInit();
}

IReactionChecker* Task_RunSubtree::GetReactionChecker(STAI_TaskContext& baseContext) const
{
    Task_RunSubtreeContext& context = ContextAs<Task_RunSubtreeContext>(baseContext);
    return context.subtree ? context.subtree->FindReactionChecker() : nullptr;
}

Engine* Task_RunSubtree::GetDebugEngine(const STAI_TaskContext& baseContext) const
{
    const Task_RunSubtreeContext& context = ContextAs<Task_RunSubtreeContext>(baseContext);
    return context.subtree ? context.subtree->GetDebugEngine() : nullptr;
}

std::string Task_RunSubtree::GetDebugInfo(const STAI_TaskContext& baseContext) const
{
    const Task_RunSubtreeContext& context = ContextAs<Task_RunSubtreeContext>(baseContext);
    if (!context.subtree)
        return file + " (not loaded)";

    const char* activeNode = context.subtree->GetActiveNodeName();
    return context.subtree->GetAsset().name + (activeNode ? std::string(" > ") + activeNode : "");
}

std::vector<StateNodeDebugChild> Task_RunSubtree::GetDebugChildren(const STAI_TaskContext& baseContext) const
{
    const Task_RunSubtreeContext& context = ContextAs<Task_RunSubtreeContext>(baseContext);
    return context.subtree ? context.subtree->GetDebugChildren() : std::vector<StateNodeDebugChild>();
}
