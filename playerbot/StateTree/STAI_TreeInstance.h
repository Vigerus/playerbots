#pragma once

#include "playerbot/StateTree/STAI_Tree.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ai
{
    // One bot running a shared STAI tree (RedStateTree's execution context and instance data): the bot's contexts
    // of the nodes (constructed in one buffer, one after another like a stack frame) and its resolved bindings,
    // property references and delegates; the tree and its nodes stay shared.
    // There is always a single active path from a top level state down to a leaf state. The path is selected only
    // while none is active (the tree started, the states completed without a transition, a transition went to
    // Root): the first top level state that can be entered (enabled, not failed within its retry delay, its enter
    // conditions pass) is entered, then one of its children by its selection behaviour, down to a leaf; enter
    // conditions are only checked then. Every task of the active states runs at the same time, a parent state's
    // tasks keep running while its children run. A task finishing completes the active states according to its
    // completion (ESTAI_TaskCompletion); a failed completion marks the active states failed (their retry delay).
    // Transitions are checked from the leaf state up to the top level state, the highest priority first:
    //   at the start of every update  OnDelegate (a delegate fired since the last update), OnEvent (an event sent),
    //                                 OnTick (while its conditions pass), and a delayed transition whose time came;
    //                                 the target must be enterable now, else the next transition is tried
    //   when the states completed     OnStateCompleted / OnStateSucceeded / OnStateFailed; the target is entered
    //                                 ignoring retry delays; none: the selection starts from Root
    // Entering a target keeps the active states above it that its path shares with the current one (their tasks
    // keep running), the target and everything below it are entered anew. A delegate fired while no active state
    // has a transition on it is remembered until one does (cleared when the tree starts).
    // A tree runs until a transition to Succeeded / Failed outside a linked subtree, a task that set bFinishTree in
    // its context (STAI_FinishTreeTask) or a global task completing finishes it.
    // Trees form a stack (Task_RunSubtree): only the top tree selects states, the trees below it just tick their
    // active states, the Task_RunSubtree leading up to the top tree.
    // Global tasks (<GlobalTasks>) belong to no state: they run the whole time the tree runs.
    //   OnStart   the tree starts running: enters the global tasks, no state is active yet
    //   OnTick    ticks the global tasks, then the transitions and the active states: Succeeded / Failed when the
    //             tree finished, otherwise Running
    //   OnFinish  the tree stops, opposite to the activation order: the active states are exited from the leaf up
    //             (a running subtree of them is finished the same way first), then the global tasks (last entered
    //             first). Also when the bot leaves the state (combat starts, death, resurrection): every tree on the
    //             stack is unwound from the top tree down to the initial tree.
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

        // an event for the OnEvent transitions of the active states, seen in the next update of the tree
        void SendEvent(const std::string& tag);

        // the state is on the active path
        bool IsStateActive(int state) const { return state >= 0 && state < int(states.size()) && states[state].active; }
        // the deepest active state, -1 none
        int GetActiveLeaf() const { return active.empty() ? -1 : active.back(); }
        // ms the state has been active, 0 when it is not
        uint32 GetTimeInState(int state, uint32 now) const;

        // adds this tree and the trees stacked on its active states (bottom to top)
        void AppendDebugTreeStack(std::vector<StateNodeDebugTree>& stack) const;
        // the active leaf state, nullptr none
        const char* GetActiveNodeName() const;

        // an addon switch of the tree named tree (this one or a subtree running on it, the top one first): a task,
        // the state or a condition of state node (StateTreeSwitch); node is the state's index (depth first)
        bool SetDebugSwitch(StateTreeSwitch what, const std::string& tree, int node, int index, int value);

    private:
        // a <Binding> of a task: the values in the bot's contexts
        struct RuntimeBinding
        {
            void* local = nullptr;
            void* external = nullptr;
            const STAI_PropertyDef* property = nullptr;
        };

        // a task of the tree with the bot's context of it
        struct TaskSlot
        {
            const STAI_TaskDef* def = nullptr;
            STAI_TaskContext* context = nullptr;
            std::vector<RuntimeBinding> bindings;
            bool entered = false;                                       // entered with its state, not exited yet
            EActionRunStatus enterStatus = EActionRunStatus::Running;   // its enter's completion, handled in the update
            uint32 processedUpdate = 0;                                 // the update that ticked it last
        };

        struct RuntimeState
        {
            const STAI_StateDef* def = nullptr;
            std::vector<TaskSlot> tasks;
            std::vector<StateNodeConditionResult> results;              // per enter condition, last selection
            std::vector<uint8> conditionOverrides;                      // per enter condition: 0 checked, 1 forced to pass, 2 forced to fail (addon)
            std::vector<int> transitionDelegates;                       // per transition: index in delegates, -1 none
            bool active = false;
            bool disabled = false;                                      // switched off in the addon: never selected
            bool failed = false;
            uint32 failedAtMs = 0;
            uint32 enteredAtMs = 0;
        };

        // a transition of a state of the tree
        struct TransitionRef
        {
            int state = -1;
            int transition = -1;

            bool operator==(const TransitionRef& other) const { return state == other.state && transition == other.transition; }
        };

        void CreateContexts();
        void ResolveMembers();

        bool TestExpr(const STAI_ConditionExpr& expr) const;
        // overrides: per entry 0 checked, 1 passes, 2 fails (a state's addon switches), nullptr checks all
        bool TestConditions(const STAI_ConditionsDef& conditions, std::vector<StateNodeConditionResult>* results = nullptr,
            const std::vector<uint8>* overrides = nullptr) const;
        void ForEachCondition(const STAI_ConditionsDef& conditions, const std::function<void(const STAI_ConditionBase&, STAI_TaskContext&)>& call) const;
        // weight times the scores of the state's considerations
        float GetUtility(int state) const;

        // selection: the path from state down to a leaf appended to path, false (path unchanged) when it cannot be entered
        bool CanSelect(int state, uint32 now, bool ignoreRetryDelay);
        bool SelectState(int state, std::vector<int>& path, uint32 now, bool ignoreRetryDelay);
        bool SelectChildren(int state, std::vector<int>& path, uint32 now);
        bool SelectFromRoot(std::vector<int>& path, uint32 now);
        // the path to target through its parents (the active ones kept), then down from it
        bool SelectTarget(int target, std::vector<int>& path, uint32 now, bool ignoreRetryDelay);

        // makes path the active path: the states it shares with the current one before reenterFrom stay, the rest
        // is exited (leaf first) and entered (top first)
        void ChangeActive(const std::vector<int>& path, size_t reenterFrom, uint32 now);
        void EnterStateAt(int state, uint32 now);
        void ExitStateAt(int state);
        void ExitActive();

        // the best transition of the active states from depth down to the top level one, state -1 none
        TransitionRef FindTransition(int depth, const std::function<bool(const STAI_TransitionDef&)>& matches) const;
        // the transitions triggered by delegates, events and ticks at the start of an update: true when one was taken
        bool TakeTriggeredTransition(uint32 now);
        // a transition triggered: scheduled when it has a delay, else applied
        bool TakeTransition(const TransitionRef& transition, uint32 now, bool ignoreRetryDelay);
        // false when its target cannot be entered
        bool ApplyTransition(const TransitionRef& transition, uint32 now, bool ignoreRetryDelay);
        // a transition to Succeeded / Failed: the Linked state above it completes, else the tree finishes
        void CompleteFrom(int state, EActionRunStatus status, uint32 now);
        // the active states completed with status
        void HandleCompletion(EActionRunStatus status, uint32 now);

        void EnterGlobals();
        void ExitGlobals();
        static void ApplyBindings(const TaskSlot& slot);
        static EActionRunStatus EnterTask(TaskSlot& slot);
        static EActionRunStatus TickTask(TaskSlot& slot, uint32 elapsed, bool minimal);
        static void ExitTask(TaskSlot& slot);
        // a global task's conditions into its context's bActive, before it is entered / ticked
        void CheckGlobalConditions(const TaskSlot& slot) const;

        STAI_TaskContext context;
        std::shared_ptr<const STAI_TreeAsset> asset;
        std::unique_ptr<std::max_align_t[]> contextStorage;     // the contexts of all nodes, one after another
        std::vector<STAI_TaskContext*> contexts;                // per STAI_TreeAsset::nodes
        std::vector<TaskSlot> globals;
        std::vector<RuntimeState> states;
        std::vector<int> active;                                // the active path, top level state first
        bool running = false;                                   // between OnStart and OnFinish, the global tasks are entered
        uint32 updateId = 0;
        // how the tree finishes (a global task completed, a transition to Succeeded / Failed), Running none
        EActionRunStatus finishRequest = EActionRunStatus::Running;
        TransitionRef delayed;                                  // a transition waiting for its delay
        uint32 delayedSinceMs = 0;
        std::vector<FSTAI_Delegate*> delegates;                 // sources of the OnDelegate transitions
        std::vector<uint8> firedDelegates;                      // per delegate: fired, not used up yet
        std::vector<std::string> events;                        // the events of this update
        std::vector<std::string> incomingEvents;                // sent since the last update
    };
}
