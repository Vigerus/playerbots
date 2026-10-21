#include "StateNode_Tree.h"

using namespace ai;

StateNode_Tree::StateNode_Tree(const STAI_TaskContext& context, std::shared_ptr<const STAI_TreeAsset> asset)
    : state(context.state), tree(context, std::move(asset))
{
    // the tasks and their contexts are fixed once the tree instance is built
    reactionChecker = tree.FindReactionChecker();
}

bool StateNode_Tree::ShouldEnterReaction()
{
    return reactionChecker && reactionChecker->ShouldEnterReaction();
}

bool StateNode_Tree::IsReacting()
{
    return reactionChecker && reactionChecker->IsReacting();
}
