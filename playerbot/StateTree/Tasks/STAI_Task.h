#pragma once

#include "playerbot/BotState.h"
#include "playerbot/StateTree/HLS.h"
#include "playerbot/StateTree/Tasks/StateTreeTask.h"

#include <new>
#include <string>
#include <type_traits>
#include <vector>

class Player;
class PlayerbotAI;

namespace ai
{
    class Engine;
    class IReactionChecker;
    class STAI_ActivityLog;

    // Runtime data of a task, one per bot and tree node; the task itself holds only its configuration.
    // Tasks with runtime state derive their own context from this one and expose it with GetContextType;
    // the tree constructs the contexts when it creates the tasks and passes them to every lifecycle call.
    // The base is also what a task is created for: the bot and the HLS state whose tree contains it.
    struct STAI_TaskContext
    {
        STAI_TaskContext() = default;
        STAI_TaskContext(PlayerbotAI* ai, Player* bot, HLSStateType state, STAI_ActivityLog* activity)
            : ai(ai), bot(bot), state(state), activity(activity) {}
        STAI_TaskContext(const STAI_TaskContext&) = default;
        STAI_TaskContext& operator=(const STAI_TaskContext&) = default;
        virtual ~STAI_TaskContext() = default;

        PlayerbotAI* ai = nullptr;
        Player* bot = nullptr;
        HLSStateType state = HLSStateType::NonCombat;
        STAI_ActivityLog* activity = nullptr;   // the bot's recent activity, tasks may add entries
    };

    // context class of a task: the tree reserves size/alignment and constructs it in place
    struct STAI_TaskContextType
    {
        size_t size;
        size_t alignment;
        STAI_TaskContext* (*construct)(void* memory, const STAI_TaskContext& base);

        template<class T>
        static const STAI_TaskContextType& Of()
        {
            static_assert(std::is_base_of_v<STAI_TaskContext, T>, "task contexts derive from STAI_TaskContext");
            static const STAI_TaskContextType type{ sizeof(T), alignof(T), [](void* memory, const STAI_TaskContext& base) -> STAI_TaskContext*
            {
                T* context = new (memory) T();
                static_cast<STAI_TaskContext&>(*context) = base;
                return context;
            } };
            return type;
        }
    };

    // Task of a STAI state tree node (<Task class="...">). Created once per tree file when it is loaded and
    // shared by every bot running that tree: tasks hold only their configuration, everything per bot lives in
    // the context the tree constructs for the node (see GetContextType) and passes to every call.
    // Lifecycle when its turn in the node comes (a node's tasks run one after another): OnEnter, OnTick every bot
    // update until it returns Succeeded (the next task of the node is entered) or Failed (the node fails), OnExit
    // (also called when an earlier node interrupts the node or the tree finishes). Tasks that are not tickable
    // (IsTickable) never get OnTick: in a node they succeed on their first update.
    class STAI_Task
    {
    public:
        virtual ~STAI_Task() = default;

        virtual const char* GetTaskClass() const = 0;

        // the context class the tree constructs for this task, override for tasks with their own context
        virtual const STAI_TaskContextType& GetContextType() const { return STAI_TaskContextType::Of<STAI_TaskContext>(); }
        // called once per bot after its context was constructed (bot setup the task needs before it runs)
        virtual void InitContext(STAI_TaskContext& context) const {}

        virtual void OnEnter(STAI_TaskContext& context) const {}
        virtual EActionRunStatus OnTick(STAI_TaskContext& context, uint32 elapsed, bool minimal) const = 0;
        virtual void OnExit(STAI_TaskContext& context) const {}

        // OnTick is called only for tickable tasks
        virtual bool IsTickable() const { return false; }

        // the tree of the node finishes (OnFinish) once this task succeeded, see STAI_FinishTreeTask
        virtual bool FinishesTree() const { return false; }
        // the node runs a tree stacked on top of the node's tree (Task_RunSubtree): while it does, the node's tree
        // only ticks the node, no other node is selected
        virtual bool HoldsActiveTree(const STAI_TaskContext& context) const { return false; }

        // tree files this task runs (relative to the config directory), checked when the trees are loaded
        virtual std::vector<std::string> GetReferencedTrees() const { return {}; }

        // temporary, for compatibility with the legacy engines (forwarded by the tree to every task)
        virtual void UpdateStrategies(STAI_TaskContext& context, const std::string& name, BotState type) const {}
        virtual void ResetStrategies(STAI_TaskContext& context) const {}
        virtual void ReInit(STAI_TaskContext& context) const {}

        // reaction tasks let the Reaction state preempt other states
        virtual IReactionChecker* GetReactionChecker(STAI_TaskContext& context) const { return nullptr; }

        // the tree logs OnEnter..OnExit of the node; tasks that never finish (legacy engines) log their own entries
        virtual bool LogsNodeActivity() const { return true; }

        // debug visualization
        virtual Engine* GetDebugEngine(const STAI_TaskContext& context) const { return nullptr; }
        virtual std::string GetDebugInfo(const STAI_TaskContext& context) const { return ""; }
        // adds the trees this task stacked on its node's tree (a running subtree)
        virtual void AppendDebugTreeStack(const STAI_TaskContext& context, std::vector<StateNodeDebugTree>& stack) const {}

    protected:
        // view of the context as the task's own context class (the one GetContextType names)
        template<class T>
        static T& ContextAs(STAI_TaskContext& context) { return static_cast<T&>(context); }
        template<class T>
        static const T& ContextAs(const STAI_TaskContext& context) { return static_cast<const T&>(context); }
    };
}
