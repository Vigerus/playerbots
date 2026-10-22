#include "StateNode_Tree.h"

using namespace ai;

StateNode_Tree::StateNode_Tree(const STAI_TaskContext& context, std::shared_ptr<const STAI_TreeAsset> asset)
    : state(context.state), tree(context, std::move(asset))
{
}
