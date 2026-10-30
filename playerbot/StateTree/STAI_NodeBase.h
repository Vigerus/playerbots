#pragma once

#include "playerbot/StateTree/HLS.h"
#include "playerbot/StateTree/STAI_Types.h"
#include "delegates/Delegates.h"
#include "World/ProfilingSubsystem.h"

#include <boost/property_tree/ptree.hpp>

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
    class STAI_TreeInstance;
    struct STAI_TaskContext;

    // <Task>, <Condition> or <Consideration> element of a STAI tree, children are the node's own settings
    using STAI_TaskConfig = boost::property_tree::ptree;

    // delegate of a task context that a <Transition trigger="OnDelegate" source="..."> waits for (GetDelegates)
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

    // A value of a task context that other nodes of the tree can read by path (Root|<TaskClass>::<name> for a
    // global task, Root|<state name>|<TaskClass>::<name> for a state's task):
    //   <Binding local="needs" external="Root|BuffProvider_GlobalTask::needs"/>   copied into the task's own property
    //   <Condition class="Condition_Compare" binding="Root|BuffProvider_GlobalTask::needs"/>   read by a condition
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

    // A delegate of a task context, by path like properties: <Transition trigger="OnDelegate" source="Root|<TaskClass>::<name>" .../>
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

    // A property or delegate of a task of the tree, by path: Root|<TaskClass>::<name> for a global task,
    // Root|<state name>|<TaskClass>::<name> for a task of a state (the first task of that class);
    // <TaskClass>[<task name>] picks the task of that class with name="<task name>" (several goals of a tree)
    struct STAI_MemberRef
    {
        std::string path;
        int state = -1;         // -1: a global task
        int task = -1;          // index in the global tasks / the state's tasks
        int node = -1;          // the task in STAI_TreeAsset::nodes (its context per bot)
        std::string member;
    };

    // A property of a task that a node reads by path, without a copy (RedStateTree's FStateTreePropertyRef):
    // Condition_Compare's binding. Resolved when the tree is loaded, the value per bot is STAI_TaskContext::refs.
    struct STAI_PropertyRef
    {
        STAI_MemberRef ref;
        const STAI_PropertyDef* property = nullptr;
    };

    // Runtime data of a node (task, condition or consideration), one per bot and node of the tree (RedStateTree's
    // instance data); the node itself holds only its configuration. Nodes with runtime state derive their own
    // context from this one and expose it with GetContextType; the tree constructs the contexts when it creates the
    // bot's instance and passes them to every call. The base is also what a node is created for: the bot and the
    // HLS state whose tree contains it.
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
        // the task's History entry of the current run (STAI_TaskBase::StartLog / LogStatus / EndLog)
        uint64 activityId = 0;
        uint32 activityStartMs = 0;
        bool activityOpen = false;
        // switched off in the BotStateTree addon (runtime only, the bot's contexts live as long as the bot): the
        // tree skips the task, a state whose tasks are all disabled is not selected
        bool bDisabled = false;
        // set by a task (STAI_FinishTreeTask): once it succeeded, the tree of its state finishes
        bool bFinishTree = false;
        // a global task's <Conditions> passed in this update (always true without conditions, and for state tasks)
        bool bActive = true;
        // the task runs: entered and not finished (RedStateTree's bIsActive)
        bool bTaskActive = false;
        // the file stem of the tree that runs the node (STAI_TreeAsset::name), for tasks that report where they come from
        const std::string* tree = nullptr;
        // the bot's instance of that tree: events, active states (STAI_TreeInstance)
        STAI_TreeInstance* instance = nullptr;
        // the state the node belongs to (its task, enter condition, consideration or transition), -1 global
        int ownerState = -1;
        // per STAI_NodeBase::GetPropertyRefs: the referenced value in the bot's context
        std::vector<void*> refs;
    };

    // {spell:<id>} in a task's debug info: the addon shows the spell's icon and name
    inline std::string STAI_SpellMarkup(uint32 spellId) { return "{spell:" + std::to_string(spellId) + "}"; }

    // context class of a node: the tree reserves size/alignment and constructs it in place
    struct STAI_TaskContextType
    {
        size_t size;
        size_t alignment;
        STAI_TaskContext* (*construct)(void* memory, const STAI_TaskContext& base);

        template<class T>
        static const STAI_TaskContextType& Of()
        {
            static_assert(std::is_base_of_v<STAI_TaskContext, T>, "node contexts derive from STAI_TaskContext");
            static const STAI_TaskContextType type{ sizeof(T), alignof(T), [](void* memory, const STAI_TaskContext& base) -> STAI_TaskContext*
            {
                T* context = new (memory) T();
                static_cast<STAI_TaskContext&>(*context) = base;
                return context;
            } };
            return type;
        }
    };

    // Base of every node of a STAI tree (RedStateTree's FStateTreeNodeBase): tasks (STAI_TaskBase), conditions
    // (STAI_ConditionBase) and considerations (STAI_ConsiderationBase). Created once per tree file when it is loaded
    // and shared by every bot running that tree: nodes hold only their configuration, everything per bot lives in
    // the context the tree constructs for the node (GetContextType).
    class STAI_NodeBase
    {
    public:
        virtual ~STAI_NodeBase() = default;

        // the class attribute of the node's element
        virtual const char* GetNodeClass() const = 0;
        // short name shown by the debug addon and the activity log
        virtual std::string GetName() const = 0;

        // the context class the tree constructs for this node, override for nodes with their own context
        virtual const STAI_TaskContextType& GetContextType() const { return STAI_TaskContextType::Of<STAI_TaskContext>(); }

        // named values and delegates of the node's context, for bindings, conditions and OnDelegate transitions
        // (checked when the tree is loaded)
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

        // what the node does, from its configuration only (RedStateTree's GetDescription): shown in the debug view;
        // {spell:<id>} is shown as the spell's icon and name (STAI_SpellMarkup)
        virtual std::string GetDebugInfo() const { return ""; }

        // developer comment, the node's <Comment>
        const std::string& GetComment() const { return comment; }
        void SetComment(const std::string& text) { comment = text; }

        // properties of tasks the node reads by path (STAI_PropertyRef), resolved by the tree when it is loaded
        std::vector<STAI_PropertyRef>& GetPropertyRefs() { return propertyRefs; }
        const std::vector<STAI_PropertyRef>& GetPropertyRefs() const { return propertyRefs; }
        // a resolved reference the node cannot read (its type): throw std::runtime_error with the reason
        virtual void ValidatePropertyRef(size_t index, const STAI_PropertyDef& property) const {}

    protected:
        // view of the context as the node's own context class (the one GetContextType names)
        template<class T>
        static T& ContextAs(STAI_TaskContext& context) { return static_cast<T&>(context); }
        template<class T>
        static const T& ContextAs(const STAI_TaskContext& context) { return static_cast<const T&>(context); }

        // a property read by path, its index in GetPropertyRefs
        size_t AddPropertyRef(const std::string& path)
        {
            propertyRefs.emplace_back();
            propertyRefs.back().ref.path = path;
            return propertyRefs.size() - 1;
        }
        // the bot's value of the reference index
        static const void* GetRefValue(const STAI_TaskContext& context, size_t index) { return context.refs[index]; }

        std::string comment;
        std::vector<STAI_PropertyRef> propertyRefs;
    };

    // a node's context under the name of what it is for every node kind
    using STAI_NodeContext = STAI_TaskContext;
}
