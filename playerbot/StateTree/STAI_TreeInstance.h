#pragma once

#include "playerbot/StateTree/STAI_Tree.h"

#include <cstddef>
#include <memory>
#include <vector>

namespace ai
{
    // One bot running a shared STAI tree: the bot's contexts of the nodes (constructed in one buffer, one after
    // another like a stack frame) and its resolved conditions, bindings and delegates; the tree and its tasks stay
    // shared.
    // There is always a single active path from the tree to the running node. A node is selected only while none
    // runs (the tree started, the node finished, a transition went to Root): the first node from the top whose
    // conditions pass is entered; conditions are only checked then. The running node stays active until its task
    // returns Succeeded or Failed (a failed node is not selected for a while), or until a delegate of one of its
    // OnDelegate transitions fired (checked at the start of the next tick; the node stays when the target's
    // conditions fail). The tasks of a node run one after another: a succeeded task is exited and the next one
    // entered (ticked from the next tick on), the node succeeds with its last task; a failed task fails the node.
    // A finished node is followed by its first matching transition: the target node is entered on the next tick
    // when its conditions pass; without a matching transition (or when the target cannot be entered) the next tick
    // selects from the first node. A delegate fired while no node with a transition on it runs is remembered until
    // one does (cleared when the tree starts). Bindings are copied into a task's context before its OnEnter and
    // every OnTick.
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
    //             then the tree is finished. Also when the bot leaves the state (combat starts, death,
    //             resurrection): every tree on the stack is unwound from the top tree down to the initial tree.
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

        // adds this tree and the trees stacked on its running node (bottom to top)
        void AppendDebugTreeStack(std::vector<StateNodeDebugTree>& stack) const;
        const char* GetActiveNodeName() const;

        // an addon switch of the tree named tree (this one or a subtree running on it, the top one first): a task,
        // the node or a condition of node (StateTreeSwitch)
        bool SetDebugSwitch(StateTreeSwitch what, const std::string& tree, int node, int index, int value);

    private:
        // a <Binding> of a task: the values in the bot's contexts
        struct RuntimeBinding
        {
            void* local = nullptr;
            void* external = nullptr;
            const STAI_PropertyDef* property = nullptr;
        };

        // a task of the tree with the bot's context of it (in contextStorage)
        struct TaskSlot
        {
            const STAI_TaskDef* def = nullptr;
            STAI_TaskContext* context = nullptr;
            std::vector<RuntimeBinding> bindings;
        };

        // the bot's resolved conditions of a node or a transition
        struct RuntimeConditions
        {
            const STAI_ConditionsDef* def = nullptr;
            std::vector<const void*> boundValues;                 // per condition, the bound property
        };

        struct RuntimeNode
        {
            const STAI_NodeDef* def = nullptr;
            std::vector<TaskSlot> tasks;                          // per task of the node, in order
            int current = -1;                                     // task running while the node is active
            RuntimeConditions conditions;
            std::vector<StateNodeConditionResult> results;        // per condition, last update
            std::vector<uint8> conditionOverrides;                // per condition: 0 checked, 1 forced to pass, 2 forced to fail (addon)
            bool disabled = false;                                // switched off in the addon: never selected
            bool failed = false;
            uint32 failedAtMs = 0;
            std::vector<RuntimeConditions> transitionConditions;      // per transition
            std::vector<std::pair<size_t, size_t>> delegateTransitions;  // index in delegates, index of the transition
        };

        void CreateContexts();
        bool CanEnter(RuntimeNode& node, uint32 now, bool ignoreFailedDelay = false);
        void EnterNode(int index);
        void FinishNode(RuntimeNode& node, EActionRunStatus status);
        void EnterNodeTask(RuntimeNode& node, int index);
        // the first task from index on that is not disabled, -1 when none
        static int NextEnabledTask(const RuntimeNode& node, int index);
        void ExitActive();
        void EnterGlobals();
        void ExitGlobals();
        void ResolveMembers();
        void ResolveConditions(RuntimeConditions& conditions, const STAI_ConditionsDef& def, const std::string& nodeName);
        // overrides: per condition 0 checked, 1 passes, 2 fails (a node's addon switches), nullptr checks all
        static bool CheckConditions(const RuntimeConditions& conditions, std::vector<StateNodeConditionResult>* results = nullptr,
            const std::vector<uint8>* overrides = nullptr);
        STAI_TaskContext* GetContext(const STAI_MemberRef& ref) const;
        static void ApplyBindings(const TaskSlot& slot);
        static void EnterTask(const TaskSlot& slot);
        static EActionRunStatus TickTask(const TaskSlot& slot, uint32 elapsed, bool minimal);
        // the running node takes its transition of a fired delegate: true when the node was left
        bool TakeDelegateTransition(uint32 now);

        STAI_TaskContext context;
        std::shared_ptr<const STAI_TreeAsset> asset;
        std::vector<RuntimeNode> nodes;
        std::vector<TaskSlot> globals;
        bool running = false;                                    // between OnStart and OnFinish, the global tasks are entered
        std::unique_ptr<std::max_align_t[]> contextStorage;      // the contexts of all nodes, one after another
        int active = -1;
        int pendingTransition = -1;                              // node to enter on the next tick
        std::vector<FSTAI_Delegate*> delegates;                  // sources of the OnDelegate transitions
        std::vector<uint8> firedDelegates;                       // per delegate: fired, not taken yet
    };
}
