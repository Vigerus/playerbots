#pragma once

#include "playerbot/StateTree/HLS.h"
#include "delegates/Delegates.h"
#include "World/ProfilingSubsystem.h"

#include <functional>
#include <new>
#include <string>
#include <type_traits>
#include <typeindex>
#include <utility>
#include <vector>

class Player;
class PlayerbotAI;

namespace ai
{
    class STAI_ActivityLog;
    struct STAI_TaskContext;

    // result of a task's update: Running keeps it running, Succeeded / Failed finish it
    enum class EActionRunStatus : uint8_t
    {
        Running = 0,
        Succeeded = 1,
        Failed = 2
    };

    // delegate of a task context that a node's <Transition type="OnDelegate" source="..."> waits for (GetDelegates)
    DECLARE_MULTICAST_DELEGATE(FSTAI_Delegate);

    namespace STAI_Detail
    {
        template <class T, class = void>
        struct HasEmpty : std::false_type {};
        template <class T>
        struct HasEmpty<T, std::void_t<decltype(std::declval<const T&>().empty())>> : std::true_type {};
        // ObjectGuid and the like
        template <class T, class = void>
        struct HasIsEmpty : std::false_type {};
        template <class T>
        struct HasIsEmpty<T, std::void_t<decltype(std::declval<const T&>().IsEmpty())>> : std::true_type {};
    }

    // A value of a task context that other tasks of the tree can read by path (Root|<TaskClass>::<name> for a
    // global task, Root|<node name>|<TaskClass>::<name> for a node's task):
    //   <Binding local="needs" external="Root|BuffProvider_GlobalTask::needs"/>   copied into the task's own property
    //   <Condition binding="Root|BuffProvider_GlobalTask::needs"/>                passes while the value is set
    //   <Condition binding="Root|ManaProvider_GlobalTask::manaPct" compare="lesser" value="90"/>   compares a number
    struct STAI_PropertyDef
    {
        std::string name;
        std::type_index type;
        std::function<void*(STAI_TaskContext& context)> get;    // the value in the bot's context
        std::function<void(void* to, const void* from)> assign;
        std::function<bool(const void* value)> isSet;           // containers: not empty, guids: not empty, else bool(value); empty when untestable
        std::function<double(const void* value)> toNumber;      // numbers (and bool) for compared conditions; empty otherwise

        template <class Context, class T>
        static STAI_PropertyDef Of(const std::string& name, T Context::* member)
        {
            STAI_PropertyDef def{ name, std::type_index(typeid(T)), {}, {}, {} };
            def.get = [member](STAI_TaskContext& context) -> void* { return &(static_cast<Context&>(context).*member); };
            def.assign = [](void* to, const void* from) { *static_cast<T*>(to) = *static_cast<const T*>(from); };
            if constexpr (STAI_Detail::HasEmpty<T>::value)
                def.isSet = [](const void* value) { return !static_cast<const T*>(value)->empty(); };
            else if constexpr (STAI_Detail::HasIsEmpty<T>::value)
                def.isSet = [](const void* value) { return !static_cast<const T*>(value)->IsEmpty(); };
            else if constexpr (std::is_convertible_v<T, bool>)
                def.isSet = [](const void* value) { return static_cast<bool>(*static_cast<const T*>(value)); };
            if constexpr (std::is_arithmetic_v<T>)
                def.toNumber = [](const void* value) { return static_cast<double>(*static_cast<const T*>(value)); };
            return def;
        }
    };

    // A delegate of a task context, by path like properties: <Transition type="OnDelegate" source="Root|<TaskClass>::<name>" .../>
    struct STAI_DelegateDef
    {
        std::string name;
        std::function<FSTAI_Delegate*(STAI_TaskContext& context)> get;

        template <class Context>
        static STAI_DelegateDef Of(const std::string& name, FSTAI_Delegate Context::* member)
        {
            return { name, [member](STAI_TaskContext& context) { return &(static_cast<Context&>(context).*member); } };
        }
    };

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
        // the node the task belongs to (nullptr for global tasks): the source of its History entries
        const std::string* nodeName = nullptr;
        // the task's History entry of the current run (STAI_Task::Enter / Tick / Exit)
        uint64 activityId = 0;
        uint32 activityStartMs = 0;
        bool activityOpen = false;
        // switched off in the BotStateTree addon (runtime only, the bot's contexts live as long as the bot): the
        // tree skips the task, a node whose tasks are all disabled is not selected
        bool bDisabled = false;
    };

    // {spell:<id>} in a task's debug info: the addon shows the spell's icon and name
    inline std::string STAI_SpellMarkup(uint32 spellId) { return "{spell:" + std::to_string(spellId) + "}"; }

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
    // (also called when an OnDelegate transition leaves the node or the tree finishes). Tasks that are not tickable
    // (IsTickable) never get OnTick: in a node they succeed on their first update. The task's <Binding>s are copied
    // into its context before OnEnter and before every OnTick.
    class STAI_Task
    {
    public:
        virtual ~STAI_Task() = default;

        virtual const char* GetTaskClass() const = 0;
        // short name shown by the debug addon and the activity log: the class without its Task_ / StateNode_ / STAI_
        // prefix and _GlobalTask suffix (Task_CastBuff -> CastBuff, BuffProvider_GlobalTask -> BuffProvider)
        virtual std::string GetName() const
        {
            std::string name = GetTaskClass();
            for (const char* prefix : { "Task_", "StateNode_", "STAI_" })
                if (name.rfind(prefix, 0) == 0 && name.size() > std::char_traits<char>::length(prefix))
                    name.erase(0, std::char_traits<char>::length(prefix));
            const std::string suffix = "_GlobalTask";
            if (name.size() > suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0)
                name.erase(name.size() - suffix.size());
            return name;
        }

        // the context class the tree constructs for this task, override for tasks with their own context
        virtual const STAI_TaskContextType& GetContextType() const { return STAI_TaskContextType::Of<STAI_TaskContext>(); }
        // called once per bot after its context was constructed (bot setup the task needs before it runs)
        virtual void InitContext(STAI_TaskContext& context) const {}

        virtual void OnEnter(STAI_TaskContext& context) const {}
        virtual EActionRunStatus OnTick(STAI_TaskContext& context, uint32 elapsed, bool minimal) const = 0;
        virtual void OnExit(STAI_TaskContext& context) const {}

        // OnTick is called only for tickable tasks
        virtual bool IsTickable() const { return false; }

        // named values and delegates of the task's context, for bindings, bound conditions and OnDelegate
        // transitions (checked when the tree is loaded)
        virtual const std::vector<STAI_PropertyDef>& GetProperties() const { static const std::vector<STAI_PropertyDef> none; return none; }
        virtual const std::vector<STAI_DelegateDef>& GetDelegates() const { static const std::vector<STAI_DelegateDef> none; return none; }

        const STAI_PropertyDef* FindProperty(const std::string& name) const
        {
            for (const STAI_PropertyDef& property : GetProperties())
                if (property.name == name)
                    return &property;
            return nullptr;
        }
        const STAI_DelegateDef* FindDelegate(const std::string& name) const
        {
            for (const STAI_DelegateDef& delegate : GetDelegates())
                if (delegate.name == name)
                    return &delegate;
            return nullptr;
        }

        // the tree of the node finishes (OnFinish) once this task succeeded, see STAI_FinishTreeTask
        virtual bool FinishesTree() const { return false; }
        // the node runs a tree stacked on top of the node's tree (Task_RunSubtree): while it does, the node's tree
        // only ticks the node
        virtual bool HoldsActiveTree(const STAI_TaskContext& context) const { return false; }

        // tree files this task runs (relative to the config directory), checked when the trees are loaded
        virtual std::vector<std::string> GetReferencedTrees() const { return {}; }

        // What the tree calls: OnEnter / OnTick / OnExit, and the task's History entry (activity log): one per run,
        // its GetDebugInfo and whether it succeeded, made once the task keeps running after its first update or
        // fails (a task that succeeds at once leaves none). A task left while it runs (a transition, the tree
        // finishing, the bot leaving the state) ends its entry in Exit with what IsSuccessful says.
        void Enter(STAI_TaskContext& context) const;
        EActionRunStatus Tick(STAI_TaskContext& context, uint32 elapsed, bool minimal) const;
        void Exit(STAI_TaskContext& context) const;

        // whether the run counts as successful in the History: when it finished (status Succeeded / Failed), or
        // when it is left while running (status Running: interrupted, unsuccessful unless the task knows better,
        // e.g. a spell that already went off)
        virtual bool IsSuccessful(const STAI_TaskContext& context, EActionRunStatus status) const { return status == EActionRunStatus::Succeeded; }
        // tasks that log their own entries or none (subtrees, global tasks) return false
        virtual bool LogsActivity() const { return true; }

        // debug visualization
        // what the task does, from its configuration only (it never changes while the bot runs): shown in the debug
        // view and as its History entries; {spell:<id>} is shown as the spell's icon and name (STAI_SpellMarkup)
        virtual std::string GetDebugInfo() const { return ""; }
        // adds the trees this task stacked on its node's tree (a running subtree)
        virtual void AppendDebugTreeStack(const STAI_TaskContext& context, std::vector<StateNodeDebugTree>& stack) const {}
        // the tree the task runs on top of this one (Task_RunSubtree), nullptr when none
        virtual class STAI_TreeInstance* GetRunningSubtree(STAI_TaskContext& context) const { return nullptr; }

    protected:
        // view of the context as the task's own context class (the one GetContextType names)
        template<class T>
        static T& ContextAs(STAI_TaskContext& context) { return static_cast<T&>(context); }
        template<class T>
        static const T& ContextAs(const STAI_TaskContext& context) { return static_cast<const T&>(context); }
    };
}
