#pragma once

#include "playerbot/StateTree/STAI_Tree.h"

#include <cstddef>
#include <memory>
#include <vector>

namespace ai
{
    class Trigger;

    // One bot running a shared STAI tree: the bot's contexts of the nodes (constructed in one buffer, one after
    // another like a stack frame) and its resolved conditions; the tree and its tasks stay shared.
    // Nodes are checked top to bottom: the first node whose conditions pass is entered; an earlier node interrupts
    // the running one as soon as its conditions pass; the running node stays active until its task returns
    // Succeeded or Failed (a failed node is skipped for a while). The tasks of a node run one after another: a
    // succeeded task is exited and the next one entered (ticked from the next tick on), the node succeeds with its
    // last task; a failed task fails the node. A finished node is followed by its first matching transition: the
    // target node is entered on the next tick when its conditions pass (no other node is selected in that tick);
    // without a matching transition (or when the target cannot be entered) the next tick starts again at the first
    // node. An interrupted node takes no transition. Once every node was evaluated (the node finished,
    // or none can be entered) the tree loops over: the next tick starts again at its first node.
    // A tree runs until a STAI_FinishTreeTask node of it succeeds; that is the only way it finishes on its own.
    // Trees form a stack (Task_RunSubtree): only the top tree selects nodes, the trees below it just tick their
    // running node, the Task_RunSubtree leading up to the top tree.
    // Global tasks (<Global> of the tree) are scopeless: they belong to no node and run the whole time the tree
    // runs, whichever node is active.
    //   OnStart   the tree starts running: enters the global tasks, no node is active yet
    //   OnTick    ticks the global tasks (their result is ignored), then selects / ticks the node: Succeeded when
    //             the tree finished (STAI_FinishTreeTask), otherwise Running
    //   OnFinish  the tree stops, opposite to the activation order: the running node gets OnExit (a running subtree
    //             of it is finished the same way first), then the global tasks get OnExit (last entered first),
    //             then the tree is finished. Also when the bot leaves the state (combat starts, death, reaction
    //             preempt): every tree on the stack is unwound from the top tree down to the initial tree.
    class STAI_TreeInstance
    {
    public:
        STAI_TreeInstance(const STAI_TaskContext& context, std::shared_ptr<const STAI_TreeAsset> asset);
        ~STAI_TreeInstance();

        STAI_TreeInstance(const STAI_TreeInstance&) = delete;
        STAI_TreeInstance& operator=(const STAI_TreeInstance&) = delete;

        const STAI_TreeAsset& GetAsset() const { return *asset; }

        void OnStart();
        EActionRunStatus OnTick(uint32 elapsed, bool minimal);
        void OnFinish();
        bool IsRunning() const { return running; }

        // temporary, for compatibility: forwarded to every task
        void UpdateStrategies(const std::string& name, BotState type);
        void ResetStrategies();
        void ReInit();

        IReactionChecker* FindReactionChecker() const;

        Engine* GetDebugEngine() const;
        // adds this tree and the trees stacked on its running node (bottom to top)
        void AppendDebugTreeStack(std::vector<StateNodeDebugTree>& stack) const;
        const char* GetActiveNodeName() const;

    private:
        // a task of the tree with the bot's context of it (in contextStorage)
        struct TaskSlot
        {
            const STAI_TaskDef* def = nullptr;
            STAI_TaskContext* context = nullptr;
        };

        struct RuntimeNode
        {
            const STAI_NodeDef* def = nullptr;
            std::vector<TaskSlot> tasks;                          // per task of the node, in order
            int current = -1;                                     // task running while the node is active
            std::vector<Trigger*> triggers;                       // per condition, nullptr = unknown trigger
            std::vector<StateNodeConditionResult> results;        // per condition, last update
            bool failed = false;
            uint32 failedAtMs = 0;
            uint64 activityId = 0;
        };

        void CreateContexts();
        bool CanEnter(RuntimeNode& node, uint32 now, bool ignoreFailedDelay = false);
        void EnterNode(int index);
        void FinishNode(RuntimeNode& node, EActionRunStatus status);
        void ExitActive();
        void EnterGlobals();
        void ExitGlobals();

        STAI_TaskContext context;
        std::shared_ptr<const STAI_TreeAsset> asset;
        std::vector<RuntimeNode> nodes;
        std::vector<TaskSlot> globals;
        bool running = false;                                    // between OnStart and OnFinish, the global tasks are entered
        std::unique_ptr<std::max_align_t[]> contextStorage;      // the contexts of all nodes, one after another
        int active = -1;
        int pendingTransition = -1;                              // node to enter on the next tick
    };
}
