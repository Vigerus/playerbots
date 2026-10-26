#pragma once

#include "playerbot/StateTree/HLS.h"
#include "delegates/Delegates.h"
#include "World/ProfilingSubsystem.h"

#include <functional>
#include <memory>
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
    class STAI_TreeAsset;
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
        // std::map and the like with number keys and number (or bool) values: read by key in conditions
        template <class T, class = void>
        struct IsNumberMap : std::false_type {};
        template <class T>
        struct IsNumberMap<T, std::void_t<typename T::key_type, typename T::mapped_type>>
            : std::bool_constant<std::is_arithmetic_v<typename T::key_type> && std::is_arithmetic_v<typename T::mapped_type>> {};
    }

    // A value of a task context that other tasks of the tree can read by path (Root|<TaskClass>::<name> for a
    // global task, Root|<node name>|<TaskClass>::<name> for a node's task):
    //   <Binding local="needs" external="Root|BuffProvider_GlobalTask::needs"/>   copied into the task's own property
    //   <Condition binding="Root|BuffProvider_GlobalTask::needs"/>                passes while the value is set
    //   <Condition binding="Root|ManaProvider_GlobalTask::manaPct" compare="lesser" value="90"/>   compares a number
    //   <Condition binding="Root|CooldownProvider_GlobalTask::ready" key="45438"/>   an entry of a number map
    //   <Condition binding="Root|SpecProvider_GlobalTask::specName" compare="equal" value="frost mage"/>   a text
    struct STAI_PropertyDef
    {
        std::string name;
        std::type_index type;
        std::function<void*(STAI_TaskContext& context)> get;    // the value in the bot's context
        std::function<void(void* to, const void* from)> assign;
        std::function<bool(const void* value)> isSet;           // containers: not empty, guids: not empty, else bool(value); empty when untestable
        std::function<double(const void* value)> toNumber;      // numbers (and bool) for compared conditions; empty otherwise
        // maps of numbers: the entry of the key as a number, false when the map has no such entry; empty otherwise
        std::function<bool(const void* value, double key, double& entry)> lookup;
        // strings for conditions comparing text (compare="equal" value="frost mage"); empty otherwise
        std::function<const std::string&(const void* value)> toText;

        template <class Context, class T>
        static STAI_PropertyDef Of(const std::string& name, T Context::* member)
        {
            STAI_PropertyDef def{ name, std::type_index(typeid(T)), {}, {}, {}, {}, {}, {} };
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
            if constexpr (std::is_same_v<T, std::string>)
                def.toText = [](const void* value) -> const std::string& { return *static_cast<const std::string*>(value); };
            if constexpr (STAI_Detail::IsNumberMap<T>::value)
                def.lookup = [](const void* value, double key, double& entry)
                {
                    const T& map = *static_cast<const T*>(value);
                    const auto found = map.find(static_cast<typename T::key_type>(key));
                    if (found == map.end())
                        return false;
                    entry = static_cast<double>(found->second);
                    return true;
                };
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
        // the task's History entry of the current run (STAI_Task::StartLog / LogStatus / EndLog)
        uint64 activityId = 0;
        uint32 activityStartMs = 0;
        bool activityOpen = false;
        // switched off in the BotStateTree addon (runtime only, the bot's contexts live as long as the bot): the
        // tree skips the task, a node whose tasks are all disabled is not selected
        bool bDisabled = false;
        // set by a task (STAI_FinishTreeTask): once it succeeded, the tree of its node finishes
        bool bFinishTree = false;
        // a global task's <Conditions> passed in this update (always true without conditions, and for node tasks)
        bool bActive = true;
        // the file stem of the tree that runs the task (STAI_TreeAsset::name), for tasks that report where they come from
        const std::string* tree = nullptr;
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
    // Lifecycle when its turn in the node comes (a node's tasks run one after another): OnEnter, which already
    // finishes the task when it returns Succeeded / Failed; Running: OnTick every bot update (tickable tasks only,
    // IsTickable) until it returns Succeeded (the next task of the node is entered) or Failed (the node fails); a
    // task that is not tickable keeps running until a transition leaves the node. OnExit when the task is left
    // (finished, an OnDelegate transition, the tree finishing). Per bot setup is done by the task in OnEnter.
    // The task's <Binding>s are copied into its context before OnEnter and before every OnTick.
    class STAI_Task
    {
    public:
        virtual ~STAI_Task() = default;

        virtual const char* GetTaskClass() const = 0;

        // short name shown by the debug addon and the activity log: every task class has its own task_name
        static std::string task_name;

        virtual std::string GetName() const
        {
            return task_name;
        }

        // the context class the tree constructs for this task, override for tasks with their own context
        virtual const STAI_TaskContextType& GetContextType() const { return STAI_TaskContextType::Of<STAI_TaskContext>(); }

        virtual EActionRunStatus OnEnter(STAI_TaskContext& context) const { return EActionRunStatus::Succeeded; }
        virtual EActionRunStatus OnTick(STAI_TaskContext& context, uint32 elapsed, bool minimal) const { return EActionRunStatus::Running; }
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

        // the node runs a tree stacked on top of the node's tree (Task_RunSubtree): while it does, the node's tree
        // only ticks the node
        virtual bool HoldsActiveTree(const STAI_TaskContext& context) const { return false; }

        // tree files this task runs (relative to the config directory), checked when the trees are loaded
        virtual std::vector<std::string> GetReferencedTrees() const { return {}; }
        // links the loaded trees of GetReferencedTrees into the task, once at server start when every tree is loaded
        // (find has each of them: trees with a missing subtree are dropped before)
        virtual void LinkReferencedTrees(const std::function<std::shared_ptr<const STAI_TreeAsset>(const std::string& file)>& find) {}
        // the linked tree the task runs (Task_RunSubtree), nullptr when none: the debug views show its nodes
        virtual const STAI_TreeAsset* GetLinkedTree() const { return nullptr; }

        // the task's runs get History entries; <Task class="..." show_log="false"> switches them off (e.g. an idle
        // Task_Wait that only waits for transitions), global tasks and subtrees have none by default
        bool LogsActivity() const { return bShowLog; }
        void SetShowLog(bool show) { bShowLog = show; }

        // a run right after a run of the same task (same info, same node) is merged into its History entry (one
        // entry counting the runs, e.g. Task_Wait); false gives every run its own entry (each cast of Task_CastSpell)
        virtual bool AreConsecutiveLogsStacked() const { return true; }

        // debug visualization
        // what the task does, from its configuration only (it never changes while the bot runs): shown in the debug
        // view and as its History entries; {spell:<id>} is shown as the spell's icon and name (STAI_SpellMarkup)
        virtual std::string GetDebugInfo() const { return ""; }
        // the name of a run's History entry: GetDebugInfo, or what the run does (Task_CastSpell: the rank it casts)
        virtual std::string GetActivityInfo(const STAI_TaskContext& context) const { return GetDebugInfo(); }
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

        // History entries (activity log): tasks log their runs by themselves, one entry per run named by its
        // GetActivityInfo (else GetName), its source is the task's name. StartLog in OnEnter remembers when the run
        // started; LogStatus with a status of the run: Running gives the run its entry (from its start), Succeeded /
        // Failed end it (a run that succeeds at once leaves none, a failed one always gets one); EndLog ends it when
        // the run is left while it runs, with whether it went well. Nothing is logged when LogsActivity is off.
        void StartLog(STAI_TaskContext& context) const;
        EActionRunStatus LogStatus(STAI_TaskContext& context, EActionRunStatus status) const;
        void EndLog(STAI_TaskContext& context, bool successful) const;

        bool bShowLog = true;
    };
}
