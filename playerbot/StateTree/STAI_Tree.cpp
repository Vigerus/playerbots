#include "STAI_Tree.h"

#include "playerbot/PlayerbotAIConfig.h"

#include <boost/property_tree/xml_parser.hpp>

#include <algorithm>
#include <cstring>
#include <cctype>
#include <cstdlib>
#include <functional>
#include <sstream>
#include <stdexcept>

using namespace ai;
using boost::property_tree::ptree;

namespace
{
    const std::pair<HLSStateType, const char*> stateNames[] =
    {
        { HLSStateType::NonCombat, "NonCombat" },
        { HLSStateType::Combat, "Combat" },
        { HLSStateType::Reaction, "Reaction" },
        { HLSStateType::Dead, "Dead" },
    };

    const std::pair<uint8, const char*> classNames[] =
    {
        { CLASS_WARRIOR, "Warrior" },
        { CLASS_PALADIN, "Paladin" },
        { CLASS_HUNTER, "Hunter" },
        { CLASS_ROGUE, "Rogue" },
        { CLASS_PRIEST, "Priest" },
        { CLASS_SHAMAN, "Shaman" },
        { CLASS_MAGE, "Mage" },
        { CLASS_WARLOCK, "Warlock" },
        { CLASS_DRUID, "Druid" },
#ifdef MANGOSBOT_TWO
        { CLASS_DEATH_KNIGHT, "DeathKnight" },
#endif
    };

    const char* const rootPathPrefix = "Root|";

    std::string Attribute(const ptree& node, const std::string& name)
    {
        return node.get<std::string>("<xmlattr>." + name, "");
    }

    // children every element may have that are no part of its content
    bool IsMeta(const std::string& key)
    {
        return key == "<xmlattr>" || key == "<xmlcomment>" || key == "Comment";
    }

    // <Comment> of an element, its lines trimmed
    std::string CommentOf(const ptree& node)
    {
        std::istringstream in(node.get<std::string>("Comment", ""));
        std::string comment;
        std::string line;
        while (std::getline(in, line))
        {
            line.erase(0, line.find_first_not_of(" \t\r"));
            line.erase(line.find_last_not_of(" \t\r") + 1);
            if (!line.empty())
                comment += (comment.empty() ? "" : "\n") + line;
        }
        return comment;
    }

    bool ParseBool(const ptree& node, const std::string& name, bool fallback, const std::string& where)
    {
        const std::string value = Attribute(node, name);
        if (value.empty())
            return fallback;
        if (value == "true" || value == "1")
            return true;
        if (value == "false" || value == "0")
            return false;
        throw std::runtime_error(where + ": " + name + " must be true or false, got '" + value + "'");
    }

    double ParseNumber(const ptree& node, const std::string& name, double fallback, double min, const std::string& where)
    {
        const std::string value = Attribute(node, name);
        if (value.empty())
            return fallback;
        char* end = nullptr;
        const double number = std::strtod(value.c_str(), &end);
        if (*end || number < min)
            throw std::runtime_error(where + ": " + name + " needs a number of at least " + std::to_string(int(min)) + ", got '" + value + "'");
        return number;
    }

    template<class Enum>
    Enum ParseEnum(const ptree& node, const std::string& name, Enum fallback, const std::string& where)
    {
        const std::string value = Attribute(node, name);
        if (value.empty())
            return fallback;
        Enum parsed = fallback;
        if (!Parse(value, parsed))
            throw std::runtime_error(where + ": unknown " + name + " '" + value + "'");
        return parsed;
    }

    std::vector<std::string> Split(const std::string& text, char separator)
    {
        std::vector<std::string> parts;
        size_t start = 0;
        for (size_t at = text.find(separator); at != std::string::npos; at = text.find(separator, start))
        {
            parts.push_back(text.substr(start, at - start));
            start = at + 1;
        }
        parts.push_back(text.substr(start));
        return parts;
    }

    // builds a STAI_TreeAsset from the <StateTree> element: parses, then resolves paths, targets and linked subtrees
    class Loader
    {
    public:
        Loader(STAI_TreeAsset& asset) : asset(asset) {}

        void ParseTree(const ptree& root);
        void Resolve();

    private:
        int AddNode(const STAI_NodeBase* node, int state)
        {
            asset.nodes.push_back({ node, state });
            return int(asset.nodes.size()) - 1;
        }

        STAI_ConditionExpr ParseConditionEntry(const std::string& key, const ptree& element, const std::string& where, int state);
        STAI_ConditionsDef ParseConditions(const ptree& element, const std::string& where, int state);
        void ParseConsiderations(const ptree& element, const std::string& where, int state);
        STAI_TaskDef ParseTask(const ptree& task, const std::string& where, bool global, int state);
        STAI_TransitionDef ParseTransition(const ptree& transition, const std::string& where, int state);
        int ParseState(const ptree& element, int parent, const std::string& where);

        int ResolveStatePath(const std::vector<std::string>& segments, const std::string& path, const std::string& where) const;
        const STAI_TaskBase& ResolveMember(STAI_MemberRef& ref, const std::string& where) const;
        void ResolveLinks();

        STAI_TreeAsset& asset;
        std::map<std::string, int> stateIndexByName;
        // nodes reading properties by path, resolved once the whole tree is read
        std::vector<std::pair<STAI_NodeBase*, std::string>> refNodes;
    };

    STAI_ConditionExpr Loader::ParseConditionEntry(const std::string& key, const ptree& element, const std::string& where, int state)
    {
        STAI_ConditionExpr expr;
        if (key == "Any" || key == "All")
        {
            expr.op = key == "Any" ? STAI_ConditionExpr::Op::Any : STAI_ConditionExpr::Op::All;
            for (const auto& [childKey, child] : element)
            {
                if (IsMeta(childKey))
                    continue;
                expr.children.push_back(ParseConditionEntry(childKey, child, where, state));
            }
            if (expr.children.empty())
                throw std::runtime_error(where + ": <" + key + "> without a <Condition>");
            return expr;
        }

        if (key != "Condition")
            throw std::runtime_error(where + ": unexpected <" + key + "> in a condition list (expected <Condition class=\"...\"/>, <Any> or <All>)");

        // binding="..." without a class: a Condition_Compare
        std::string conditionClass = Attribute(element, "class");
        if (conditionClass.empty())
            conditionClass = "Condition_Compare";
        const STAI_NodeFactory::ConditionClass* factoryClass = sSTAITaskFactory.FindCondition(conditionClass);
        if (!factoryClass)
            throw std::runtime_error(where + ": unknown condition class '" + conditionClass + "'");

        ptree config = element;
        config.erase("Comment");
        try
        {
            factoryClass->validate(config);
        }
        catch (const std::exception& error)
        {
            throw std::runtime_error(where + " (" + conditionClass + "): " + error.what());
        }

        expr.condition = factoryClass->create(config);
        if (!expr.condition)
            throw std::runtime_error(where + " (" + conditionClass + "): the condition could not be created");
        expr.condition->SetInverted(ParseBool(element, "inverted", false, where));
        expr.condition->SetComment(CommentOf(element));
        expr.node = AddNode(expr.condition.get(), state);
        if (!expr.condition->GetPropertyRefs().empty())
            refNodes.emplace_back(expr.condition.get(), where);
        return expr;
    }

    STAI_ConditionsDef Loader::ParseConditions(const ptree& element, const std::string& where, int state)
    {
        STAI_ConditionsDef def;

        const std::string match = Attribute(element, "match");
        if (match == "any")
            def.any = true;
        else if (!match.empty() && match != "all")
            throw std::runtime_error(where + ": match must be all or any, got '" + match + "'");

        for (const auto& [key, child] : element)
        {
            if (IsMeta(key))
                continue;
            def.list.push_back(ParseConditionEntry(key, child, where, state));
        }
        return def;
    }

    void Loader::ParseConsiderations(const ptree& element, const std::string& where, int state)
    {
        for (const auto& [key, child] : element)
        {
            if (IsMeta(key))
                continue;
            if (key != "Consideration")
                throw std::runtime_error(where + ": unexpected <" + key + "> in <Considerations> (expected <Consideration class=\"...\"/>)");

            const std::string considerationClass = Attribute(child, "class");
            const STAI_NodeFactory::ConsiderationClass* factoryClass = sSTAITaskFactory.FindConsideration(considerationClass);
            if (!factoryClass)
                throw std::runtime_error(where + ": unknown consideration class '" + considerationClass + "'");

            ptree config = child;
            config.erase("Comment");
            try
            {
                factoryClass->validate(config);
            }
            catch (const std::exception& error)
            {
                throw std::runtime_error(where + " (" + considerationClass + "): " + error.what());
            }

            STAI_ConsiderationDef def;
            def.consideration = factoryClass->create(config);
            if (!def.consideration)
                throw std::runtime_error(where + " (" + considerationClass + "): the consideration could not be created");
            def.consideration->SetComment(CommentOf(child));
            def.node = AddNode(def.consideration.get(), state);
            if (!def.consideration->GetPropertyRefs().empty())
                refNodes.emplace_back(def.consideration.get(), where);
            asset.states[state].considerations.push_back(std::move(def));
        }
    }

    // <Task class="..."> settings </Task>: validated and created (shared by every bot); <Conditions> only for global
    // tasks
    STAI_TaskDef Loader::ParseTask(const ptree& task, const std::string& where, bool global, int state)
    {
        STAI_TaskDef def;
        def.taskClass = Attribute(task, "class");
        def.name = Attribute(task, "name");
        const STAI_NodeFactory::TaskClass* taskClass = sSTAITaskFactory.FindTask(def.taskClass);
        if (!taskClass)
            throw std::runtime_error(where + ": unknown task class '" + def.taskClass + "'");

        // <Binding>s, <Conditions> and <Comment> are handled by the tree, the task only sees its own settings
        def.taskConfig = task;
        def.taskConfig.erase("Binding");
        def.taskConfig.erase("Conditions");
        def.taskConfig.erase("Comment");
        if (const auto conditions = task.get_child_optional("Conditions"))
        {
            if (!global)
                throw std::runtime_error(where + " (" + def.taskClass + "): <Conditions> in a <Task> only in <GlobalTasks> (a state has <EnterConditions>)");
            def.conditions = ParseConditions(*conditions, where + " (" + def.taskClass + ")", state);
        }
        for (const auto& [key, binding] : task)
        {
            if (key != "Binding")
                continue;

            STAI_BindingDef bindingDef;
            bindingDef.local = Attribute(binding, "local");
            bindingDef.external.path = Attribute(binding, "external");
            if (bindingDef.local.empty() || bindingDef.external.path.empty())
                throw std::runtime_error(where + " (" + def.taskClass + "): <Binding> needs local=\"<property>\" and external=\"Root|...\"");
            def.bindings.push_back(std::move(bindingDef));
        }

        try
        {
            taskClass->validate(def.taskConfig);
        }
        catch (const std::exception& error)
        {
            throw std::runtime_error(where + " (" + def.taskClass + "): " + error.what());
        }

        def.task = taskClass->create(def.taskConfig);
        if (!def.task)
            throw std::runtime_error(where + " (" + def.taskClass + "): the task could not be created");
        def.task->SetComment(CommentOf(task));
        def.node = AddNode(def.task.get(), state);
        if (!def.task->GetPropertyRefs().empty())
            refNodes.emplace_back(def.task.get(), where + " (" + def.taskClass + ")");

        // show_log="false": no History entries for the task's runs; "true" for a task that has none by default
        const std::string showLog = Attribute(task, "show_log");
        if (!showLog.empty())
            def.task->SetShowLog(ParseBool(task, "show_log", true, where + " (" + def.taskClass + ")"));

        const std::string completion = Attribute(task, "completion");
        if (!completion.empty())
            def.task->SetCompletion(ParseEnum(task, "completion", ESTAI_TaskCompletion::AlwaysFinish, where + " (" + def.taskClass + ")"));

        return def;
    }

    // <Transition trigger="..." target="..."/>, the target state and the source delegate are resolved once the
    // whole tree is read
    STAI_TransitionDef Loader::ParseTransition(const ptree& transition, const std::string& where, int state)
    {
        STAI_TransitionDef def;

        const std::string trigger = Attribute(transition, "trigger");
        if (!ai::Parse(trigger, def.trigger))
            throw STAI_TreeFatalError(where + ": <Transition> trigger must be OnStateCompleted, OnStateSucceeded, OnStateFailed, OnTick, OnEvent or OnDelegate, got '" + trigger + "'");

        def.source.path = Attribute(transition, "source");
        if ((def.trigger == ESTAI_TransitionTrigger::OnDelegate) == def.source.path.empty())
            throw STAI_TreeFatalError(where + ": <Transition> source=\"Root|<TaskClass>::<delegate>\" is needed for OnDelegate and only allowed there");

        def.event = Attribute(transition, "event");
        if ((def.trigger == ESTAI_TransitionTrigger::OnEvent) == def.event.empty())
            throw STAI_TreeFatalError(where + ": <Transition> event=\"...\" is needed for OnEvent and only allowed there");

        def.path = Attribute(transition, "target");
        if (def.path.empty())
            throw STAI_TreeFatalError(where + ": <Transition> needs a target (Root, Root|<state>, NextState, Succeeded or Failed)");

        try
        {
            def.priority = ParseEnum(transition, "priority", ESTAI_TransitionPriority::Normal, where);
            def.delayMs = uint32(ParseNumber(transition, "delay", 0.0, 0.0, where));
        }
        catch (const std::runtime_error& error)
        {
            throw STAI_TreeFatalError(error.what());
        }

        for (const auto& [key, child] : transition)
        {
            if (IsMeta(key))
                continue;
            if (key != "Conditions")
                throw STAI_TreeFatalError(where + ": unexpected <" + key + "> in <Transition> (expected <Conditions>)");
            def.conditions = ParseConditions(child, where, state);
        }

        return def;
    }

    int Loader::ParseState(const ptree& element, int parent, const std::string& where)
    {
        const int index = int(asset.states.size());
        asset.states.emplace_back();

        {
            STAI_StateDef& def = asset.states[index];
            def.name = Attribute(element, "name");
            if (def.name.empty())
                throw STAI_TreeFatalError(where + ": <State> needs a name");
            if (def.name.find('|') != std::string::npos || def.name.find("::") != std::string::npos)
                throw STAI_TreeFatalError(where + ": state name '" + def.name + "' must not contain | or ::");

            auto [used, inserted] = stateIndexByName.emplace(def.name, index);
            if (!inserted)
                throw STAI_TreeFatalError(where + ": state name '" + def.name + "' is already used by state '" + asset.states[used->second].path + "', state names must be unique in a tree");

            def.parent = parent;
            def.depth = parent < 0 ? 0 : asset.states[parent].depth + 1;
            def.path = (parent < 0 ? std::string("Root") : asset.states[parent].path) + "|" + def.name;
            def.comment = CommentOf(element);
            def.type = ParseEnum(element, "type", ESTAI_StateType::State, where);
            def.linkedSubtree = Attribute(element, "linkedSubtree");
            if ((def.type == ESTAI_StateType::Linked) == def.linkedSubtree.empty())
                throw STAI_TreeFatalError(where + ": linkedSubtree=\"<state>\" is needed for type=\"Linked\" and only allowed there");
            def.selection = ParseEnum(element, "selection", ESTAI_StateSelection::TrySelectChildrenInOrder, where);
            def.weight = float(ParseNumber(element, "weight", 1.0, 0.0, where));
            def.enabled = ParseBool(element, "enabled", true, where);
        }

        const std::string stateWhere = where + " '" + asset.states[index].name + "'";
        for (const auto& [key, child] : element)
        {
            if (IsMeta(key))
                continue;

            if (key == "EnterConditions")
                asset.states[index].enterConditions = ParseConditions(child, stateWhere, index);
            else if (key == "Considerations")
                ParseConsiderations(child, stateWhere, index);
            else if (key == "Tasks")
            {
                for (const auto& [taskKey, task] : child)
                {
                    if (IsMeta(taskKey))
                        continue;
                    if (taskKey != "Task")
                        throw std::runtime_error(stateWhere + ": unexpected <" + taskKey + "> in <Tasks> (expected <Task class=\"...\">)");
                    if (!ParseBool(task, "enabled", true, stateWhere))
                        continue;
                    const std::string taskWhere = stateWhere + " task #" + std::to_string(asset.states[index].tasks.size() + 1);
                    STAI_TaskDef def = ParseTask(task, taskWhere, false, index);
                    asset.states[index].tasks.push_back(std::move(def));
                }
            }
            else if (key == "Transitions")
            {
                for (const auto& [transitionKey, transition] : child)
                {
                    if (IsMeta(transitionKey))
                        continue;
                    if (transitionKey != "Transition")
                        throw STAI_TreeFatalError(stateWhere + ": unexpected <" + transitionKey + "> in <Transitions> (expected <Transition trigger=\"...\" target=\"...\"/>)");
                    if (!ParseBool(transition, "enabled", true, stateWhere))
                        continue;
                    STAI_TransitionDef def = ParseTransition(transition, stateWhere, index);
                    asset.states[index].transitions.push_back(std::move(def));
                }
            }
            else if (key == "State")
            {
                const int childIndex = ParseState(child, index, where);
                asset.states[index].children.push_back(childIndex);
            }
            else
                throw std::runtime_error(stateWhere + ": unexpected <" + key + "> in <State> (expected <EnterConditions>, <Considerations>, <Tasks>, <Transitions> or <State>)");
        }

        STAI_StateDef& def = asset.states[index];
        def.subtreeEnd = int(asset.states.size());
        def.retryDelayMs = uint32(ParseNumber(element, "retryDelay", def.children.empty() ? 3000.0 : 0.0, 0.0, stateWhere));

        if (def.type == ESTAI_StateType::Linked && !def.children.empty())
            throw STAI_TreeFatalError(stateWhere + ": a Linked state runs its subtree's children, it cannot have child states of its own");
        if (def.type != ESTAI_StateType::Linked && def.children.empty() && def.tasks.empty())
            throw std::runtime_error(stateWhere + ": a state without child states needs a <Task class=\"...\">");

        return index;
    }

    void Loader::ParseTree(const ptree& root)
    {
        asset.comment = CommentOf(root);

        const std::string version = Attribute(root, "version");
        if (version != "2")
            throw std::runtime_error(asset.file + ": <StateTree version=\"2\"> expected, got version '" + version + "' (convert v1 files with tools/StateTreeEditor/convert_v1_to_v2.py)");

        bool hasGlobal = false;
        int index = 0;
        for (const auto& [key, child] : root)
        {
            if (IsMeta(key))
                continue;

            if (key == "GlobalTasks")
            {
                if (hasGlobal)
                    throw std::runtime_error(asset.file + ": more than one <GlobalTasks> in <StateTree>");
                hasGlobal = true;

                for (const auto& [taskKey, task] : child)
                {
                    if (IsMeta(taskKey))
                        continue;
                    if (taskKey != "Task")
                        throw std::runtime_error(asset.file + ": unexpected <" + taskKey + "> in <GlobalTasks> (expected <Task class=\"...\">)");
                    if (!ParseBool(task, "enabled", true, asset.file))
                        continue;
                    STAI_TaskDef def = ParseTask(task, asset.file + " global task #" + std::to_string(asset.globalTasks.size() + 1), true, -1);
                    asset.globalTasks.push_back(std::move(def));
                }
                continue;
            }

            if (key != "State")
                throw std::runtime_error(asset.file + ": unexpected <" + key + "> in <StateTree> (expected <GlobalTasks> or <State>)");

            ++index;
            asset.topStates.push_back(ParseState(child, -1, asset.file + " state"));
        }

        if (asset.topStates.empty())
            throw std::runtime_error(asset.file + ": no <State> in <StateTree>");
    }

    // Root|<state> or Root|<top state>|...|<state>: the state, a path through its parents must be its real one
    int Loader::ResolveStatePath(const std::vector<std::string>& segments, const std::string& path, const std::string& where) const
    {
        auto found = stateIndexByName.find(segments.back());
        if (found == stateIndexByName.end())
            throw STAI_TreeFatalError(where + ": '" + path + "': unknown state '" + segments.back() + "'");

        const int state = found->second;
        if (segments.size() == 1)
            return state;

        std::string full = "Root";
        for (const std::string& segment : segments)
            full += "|" + segment;
        if (asset.states[state].path != full)
            throw STAI_TreeFatalError(where + ": '" + path + "' is not the path of state '" + segments.back() + "' (" + asset.states[state].path + ")");
        return state;
    }

    // properties and delegates: Root|<TaskClass>::<name> (global task) or Root|<state>|<TaskClass>::<name>
    const STAI_TaskBase& Loader::ResolveMember(STAI_MemberRef& ref, const std::string& where) const
    {
        auto fail = [&](const std::string& why) { return STAI_TreeFatalError(where + ": '" + ref.path + "': " + why); };

        if (ref.path.rfind(rootPathPrefix, 0) != 0)
            throw fail("expected Root|<TaskClass>::<name> or Root|<state>|<TaskClass>::<name>");
        const std::string rest = ref.path.substr(strlen(rootPathPrefix));

        const size_t member = rest.find("::");
        if (member == std::string::npos || member == 0 || member + 2 == rest.size())
            throw fail("expected <TaskClass>::<name>");
        ref.member = rest.substr(member + 2);

        std::vector<std::string> segments = Split(rest.substr(0, member), '|');
        std::string taskClass = segments.back();
        segments.pop_back();

        const std::vector<STAI_TaskDef>* tasks = &asset.globalTasks;
        ref.state = -1;
        if (!segments.empty())
        {
            ref.state = ResolveStatePath(segments, ref.path, where);
            tasks = &asset.states[ref.state].tasks;
        }

        // <TaskClass>[<task name>]
        std::string taskName;
        const size_t open = taskClass.find('[');
        if (open != std::string::npos)
        {
            if (taskClass.back() != ']')
                throw fail("expected <TaskClass>[<task name>]::<name>");
            taskName = taskClass.substr(open + 1, taskClass.size() - open - 2);
            taskClass = taskClass.substr(0, open);
        }

        for (size_t i = 0; i < tasks->size(); ++i)
        {
            if ((*tasks)[i].taskClass != taskClass || (open != std::string::npos && (*tasks)[i].name != taskName))
                continue;
            ref.task = int(i);
            ref.node = (*tasks)[i].node;
            return *(*tasks)[i].task;
        }
        throw fail("no task of class '" + taskClass + "'" + (open != std::string::npos ? " named '" + taskName + "'" : "") + " in " +
            (ref.state < 0 ? "<GlobalTasks>" : "the state"));
    }

    // Linked states run a Subtree state of the tree; a subtree running itself through its linked states is an error
    void Loader::ResolveLinks()
    {
        for (STAI_StateDef& state : asset.states)
        {
            if (state.type != ESTAI_StateType::Linked)
                continue;
            auto found = stateIndexByName.find(state.linkedSubtree);
            if (found == stateIndexByName.end() || asset.states[found->second].type != ESTAI_StateType::Subtree)
                throw STAI_TreeFatalError(asset.file + " state '" + state.name + "': linkedSubtree '" + state.linkedSubtree + "' is not a state of type Subtree");
            state.linked = found->second;
        }

        // depth first from every subtree through the Linked states inside it
        std::map<int, int> visit; // 1 = on the path, 2 = done
        std::function<void(int)> walk = [&](int subtree)
        {
            int& mark = visit[subtree];
            if (mark == 1)
                throw STAI_TreeFatalError(asset.file + ": subtree '" + asset.states[subtree].name + "' runs itself through its linked states");
            if (mark == 2)
                return;
            mark = 1;
            for (int i = subtree; i < asset.states[subtree].subtreeEnd; ++i)
                if (asset.states[i].linked >= 0)
                    walk(asset.states[i].linked);
            visit[subtree] = 2;
        };
        for (const STAI_StateDef& state : asset.states)
            if (state.linked >= 0)
                walk(state.linked);
    }

    void Loader::Resolve()
    {
        ResolveLinks();

        auto resolveBindings = [&](STAI_TaskDef& task, const std::string& where)
        {
            for (STAI_BindingDef& binding : task.bindings)
            {
                binding.localProperty = task.task->FindProperty(binding.local);
                if (!binding.localProperty)
                    throw STAI_TreeFatalError(where + " (" + task.taskClass + "): <Binding> local '" + binding.local + "' is not a property of the task");

                binding.externalProperty = ResolveMember(binding.external, where).FindProperty(binding.external.member);
                if (!binding.externalProperty)
                    throw STAI_TreeFatalError(where + ": <Binding> external '" + binding.external.path + "' is not a property of that task");
                if (binding.externalProperty->type != binding.localProperty->type)
                    throw STAI_TreeFatalError(where + ": <Binding> '" + binding.local + "' and '" + binding.external.path + "' have different types");
            }
        };

        for (size_t i = 0; i < asset.globalTasks.size(); ++i)
            resolveBindings(asset.globalTasks[i], asset.file + " global task #" + std::to_string(i + 1));

        for (size_t s = 0; s < asset.states.size(); ++s)
        {
            STAI_StateDef& state = asset.states[s];
            const std::string where = asset.file + " state '" + state.name + "'";
            for (STAI_TaskDef& task : state.tasks)
                resolveBindings(task, where);

            // siblings for NextState: the parent's children, or the top level states
            const std::vector<int>& siblings = state.parent < 0 ? asset.topStates : asset.states[state.parent].children;

            for (STAI_TransitionDef& transition : state.transitions)
            {
                if (transition.trigger == ESTAI_TransitionTrigger::OnDelegate)
                {
                    transition.delegate = ResolveMember(transition.source, where).FindDelegate(transition.source.member);
                    if (!transition.delegate)
                        throw STAI_TreeFatalError(where + ": <Transition> source '" + transition.source.path + "' is not a delegate of that task");
                }

                if (transition.path == "Root")
                    transition.target = STAI_TransitionDef::Target::Root;
                else if (transition.path == "Succeeded")
                    transition.target = STAI_TransitionDef::Target::Succeeded;
                else if (transition.path == "Failed")
                    transition.target = STAI_TransitionDef::Target::Failed;
                else if (transition.path == "NextState")
                {
                    auto self = std::find(siblings.begin(), siblings.end(), int(s));
                    if (self == siblings.end() || self + 1 == siblings.end())
                        throw STAI_TreeFatalError(where + ": <Transition> target NextState, but the state is the last of its siblings");
                    transition.target = STAI_TransitionDef::Target::State;
                    transition.state = *(self + 1);
                }
                else if (transition.path.rfind(rootPathPrefix, 0) == 0 && transition.path.size() > strlen(rootPathPrefix))
                {
                    transition.target = STAI_TransitionDef::Target::State;
                    transition.state = ResolveStatePath(Split(transition.path.substr(strlen(rootPathPrefix)), '|'), transition.path, where);
                }
                else
                    throw STAI_TreeFatalError(where + ": <Transition> target must be Root, Root|<state>, NextState, Succeeded or Failed, got '" + transition.path + "'");
            }
        }

        // properties read by path (Condition_Compare's binding, Consideration_Number's): their type checked by the node
        for (auto& [node, where] : refNodes)
        {
            for (size_t i = 0; i < node->GetPropertyRefs().size(); ++i)
            {
                STAI_PropertyRef& ref = node->GetPropertyRefs()[i];
                ref.property = ResolveMember(ref.ref, where).FindProperty(ref.ref.member);
                if (!ref.property)
                    throw STAI_TreeFatalError(where + ": '" + ref.ref.path + "' is not a property of that task");
                try
                {
                    node->ValidatePropertyRef(i, *ref.property);
                }
                catch (const std::runtime_error& error)
                {
                    throw STAI_TreeFatalError(where + " (" + node->GetNodeClass() + "): " + error.what());
                }
            }
        }
    }

    // the state's static description for the debug views (no bot's data: inactive, conditions not checked)
    void DescribeState(const STAI_TreeAsset& asset, int index, StateNodeDebugChild& child)
    {
        const STAI_StateDef& state = asset.states[index];
        child.name = state.name;
        child.depth = state.depth;
        child.parent = state.parent;
        if (!state.tasks.empty())
        {
            child.taskClass = state.tasks.front().task->GetName();
            child.info = state.tasks.front().task->GetDebugInfo();
        }
        else if (state.type == ESTAI_StateType::Linked)
            child.info = "Linked " + state.linkedSubtree;
        else
            child.info = ToString(state.selection);
        for (const STAI_TransitionDef& transition : state.transitions)
            child.transitions.push_back(transition.GetText());
        for (const STAI_ConditionExpr& condition : state.enterConditions.list)
            child.conditions.emplace_back(condition.GetText(), StateNodeConditionResult::NotChecked);
        child.conditionsAny = state.enterConditions.any;
        child.conditionOverrides.assign(state.enterConditions.size(), 0);
        child.disabled = !state.enabled;
    }
}

bool STAI_TransitionDef::Matches(EActionRunStatus status) const
{
    switch (trigger)
    {
        case ESTAI_TransitionTrigger::OnStateCompleted: return status != EActionRunStatus::Running;
        case ESTAI_TransitionTrigger::OnStateSucceeded: return status == EActionRunStatus::Succeeded;
        case ESTAI_TransitionTrigger::OnStateFailed: return status == EActionRunStatus::Failed;
        default: return false;
    }
}

std::string STAI_TransitionDef::GetText() const
{
    std::string text = ToString(trigger);
    if (trigger == ESTAI_TransitionTrigger::OnDelegate)
        text += " " + source.path;
    else if (trigger == ESTAI_TransitionTrigger::OnEvent)
        text += " '" + event + "'";
    text += " " + path;
    if (priority != ESTAI_TransitionPriority::Normal)
        text += std::string(" (") + ToString(priority) + ")";
    if (delayMs)
        text += " after " + std::to_string(delayMs) + " ms";

    // the transition's own conditions: "... IF a AND b" / "... IF a OR b"
    for (size_t i = 0; i < conditions.list.size(); ++i)
        text += (i ? (conditions.any ? " OR " : " AND ") : " IF ") + conditions.list[i].GetText();
    return text;
}

std::string STAI_ConditionExpr::GetText() const
{
    if (op == Op::Leaf)
        return condition->GetText();

    // "(a OR b)" / "(a AND b)"
    std::string text = "(";
    for (size_t i = 0; i < children.size(); ++i)
        text += (i ? (op == Op::Any ? " OR " : " AND ") : "") + children[i].GetText();
    return text + ")";
}

std::shared_ptr<const STAI_TreeAsset> STAI_TreeAsset::Load(const std::string& file)
{
    const std::filesystem::path path = STAI_NodeFactory::ResolvePath(file);

    ptree document;
    try
    {
        boost::property_tree::read_xml(path.string(), document, boost::property_tree::xml_parser::trim_whitespace | boost::property_tree::xml_parser::no_comments);
    }
    catch (const boost::property_tree::xml_parser_error& error)
    {
        throw std::runtime_error(file + ":" + std::to_string(error.line()) + ": " + error.message());
    }

    auto root = document.get_child_optional("StateTree");
    if (!root)
        throw std::runtime_error(file + ": missing <StateTree version=\"2\"> element" + (document.get_child_optional("Root") ?
            " (a v1 file: convert it with tools/StateTreeEditor/convert_v1_to_v2.py)" : ""));

    auto asset = std::make_shared<STAI_TreeAsset>();
    asset->file = file;
    asset->name = path.stem().string();

    Loader loader(*asset);
    loader.ParseTree(*root);
    loader.Resolve();
    return asset;
}

int STAI_TreeAsset::FindState(const std::string& stateName) const
{
    for (size_t i = 0; i < states.size(); ++i)
        if (states[i].name == stateName)
            return int(i);
    return -1;
}

std::shared_ptr<const STAI_TreeAsset> STAI_TreeAsset::MakeIdle(const std::string& name)
{
    auto asset = std::make_shared<STAI_TreeAsset>();
    asset->name = name;

    const std::string taskClass = "Task_Wait";
    STAI_TaskDef task;
    task.taskClass = taskClass;
    task.taskConfig.put("<xmlattr>.class", taskClass);
    task.taskConfig.put("Delay.<xmlattr>.value", "0");
    if (const STAI_NodeFactory::TaskClass* factoryClass = sSTAITaskFactory.FindTask(taskClass))
        task.task = factoryClass->create(task.taskConfig);
    if (!task.task)
        throw std::runtime_error("unknown task class '" + taskClass + "'");

    STAI_StateDef state;
    state.name = "Idle";
    state.path = "Root|Idle";
    state.subtreeEnd = 1;
    state.retryDelayMs = 3000;
    asset->states.push_back(std::move(state));
    asset->topStates.push_back(0);

    asset->nodes.push_back({ task.task.get(), 0 });
    task.node = 0;
    asset->states[0].tasks.push_back(std::move(task));

    return asset;
}

StateNodeDebugTree STAI_TreeAsset::GetDebugTree() const
{
    StateNodeDebugTree tree;
    tree.name = name;
    for (const STAI_TaskDef& global : globalTasks)
    {
        tree.globalTasks.push_back(global.task->GetName());
        tree.globalSpells.emplace_back();
    }

    for (size_t i = 0; i < states.size(); ++i)
    {
        StateNodeDebugChild child;
        DescribeState(*this, int(i), child);
        for (size_t t = 0; t < states[i].tasks.size(); ++t)
        {
            const STAI_TaskBase& task = *states[i].tasks[t].task;
            child.tasks.push_back(task.GetName());
            child.tasksDisabled.push_back(false);
            child.taskInfos.push_back(task.GetDebugInfo());
            if (const STAI_TreeAsset* subtree = task.GetLinkedTree())
            {
                child.subtrees.push_back(subtree->GetDebugTree());
                child.subtrees.back().task = int(t);
            }
        }
        tree.nodes.push_back(std::move(child));
    }
    return tree;
}

namespace ai
{
    // the static part of a state of the bot's tree for the debug views (STAI_TreeInstance)
    void STAI_DescribeState(const STAI_TreeAsset& asset, int index, StateNodeDebugChild& child)
    {
        DescribeState(asset, index, child);
    }
}

std::string STAI_TreeLibrary::GetFileKey(const std::string& file)
{
    std::string key = STAI_NodeFactory::ResolvePath(file).generic_string();
#ifdef _WIN32
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return char(std::tolower(c)); });
#endif
    return key;
}

// drops trees running a subtree that is not loaded (failed or missing) or running themselves through subtrees
void STAI_TreeLibrary::RemoveBrokenSubtrees(FileMap& files, uint32& errors)
{
    auto referencedTrees = [](const STAI_TreeAsset& asset)
    {
        std::vector<std::string> references;
        auto add = [&](const STAI_TaskDef& task)
        {
            for (const std::string& file : task.task->GetReferencedTrees())
                references.push_back(file);
        };
        for (const STAI_TaskDef& task : asset.globalTasks)
            add(task);
        for (const STAI_StateDef& state : asset.states)
            for (const STAI_TaskDef& task : state.tasks)
                add(task);
        return references;
    };

    for (bool removed = true; removed; )
    {
        removed = false;

        // missing subtrees
        for (auto it = files.begin(); it != files.end(); )
        {
            std::string missing;
            for (const std::string& file : referencedTrees(*it->second))
                if (!files.count(GetFileKey(file)))
                    missing = file;

            if (missing.empty())
            {
                ++it;
                continue;
            }

            sLog.outError("StateTree: %s not loaded: subtree %s is not loaded", it->second->file.c_str(), missing.c_str());
            ++errors;
            it = files.erase(it);
            removed = true;
        }

        // cycles: depth-first walk from every tree, a tree reached again while on the path closes a cycle
        std::map<std::string, int> visit; // 1 = on the path, 2 = done
        std::string cycle;
        std::function<bool(const std::string&)> walk = [&](const std::string& key)
        {
            const int state = visit[key];
            if (state == 1)
            {
                cycle = key;
                return true;
            }
            if (state == 2)
                return false;

            visit[key] = 1;
            for (const std::string& file : referencedTrees(*files.at(key)))
                if (walk(GetFileKey(file)))
                    return true;
            visit[key] = 2;
            return false;
        };

        for (const auto& entry : files)
            if (walk(entry.first))
                break;

        if (!cycle.empty())
        {
            sLog.outError("StateTree: %s not loaded: it runs itself through its subtrees", files.at(cycle)->file.c_str());
            ++errors;
            files.erase(cycle);
            removed = true;
        }
    }
}

void STAI_TreeLibrary::LoadFromConfig()
{
    if (IsLoaded())
    {
        sLog.outString("StateTree: tree files are loaded only at server start, not reloaded");
        return;
    }

    auto loaded = std::make_shared<Snapshot>();
    uint32 errors = 0;
    std::vector<std::string> fatalErrors;

    auto loadFile = [&](const std::string& file) -> bool
    {
        const std::string key = GetFileKey(file);
        if (loaded->files.count(key))
            return true;

        try
        {
            loaded->files.emplace(key, STAI_TreeAsset::Load(file));
            return true;
        }
        catch (const STAI_TreeFatalError& error)
        {
            sLog.outError("StateTree: %s not loaded: %s", file.c_str(), error.what());
            fatalErrors.push_back(error.what());
            ++errors;
            return false;
        }
        catch (const std::exception& error)
        {
            sLog.outError("StateTree: %s not loaded: %s", file.c_str(), error.what());
            ++errors;
            return false;
        }
    };

    // every tree of the statetree directory and its class folders, so subtrees need no config entry
    const std::filesystem::path directory = STAI_NodeFactory::ResolvePath("statetree");
    std::error_code error;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(directory, error))
    {
        const std::filesystem::path& path = entry.path();
        if (entry.is_regular_file() && path.extension() == ".xml" && path.stem().string().rfind("STAI_", 0) == 0)
            loadFile("statetree/" + path.lexically_relative(directory).generic_string());
    }
    if (error)
        sLog.outError("StateTree: %s not readable: %s", directory.string().c_str(), error.message().c_str());

    // configured trees (also the ones outside the directory)
    struct Slot
    {
        HLSStateType state;
        uint8 classId;
        std::string key;
        std::string file;
    };
    std::vector<Slot> slots;
    for (const auto& [state, stateName] : stateNames)
    {
        auto addSlot = [&](uint8 classId, const char* slotName)
        {
            const std::string key = std::string("AiPlayerbot.StateTree.") + stateName + "." + slotName;
            const std::string file = sPlayerbotAIConfig.GetConfigString(key, "");
            if (!file.empty() && loadFile(file))
                slots.push_back({ state, classId, key, file });
        };

        addSlot(0, "Default");
        for (const auto& [classId, className] : classNames)
            addSlot(classId, className);
    }

    RemoveBrokenSubtrees(loaded->files, errors);

    // every subtree a task runs is linked into it now: a bot entering the task only creates its contexts
    auto find = [&loaded](const std::string& file) -> std::shared_ptr<const STAI_TreeAsset>
    {
        auto it = loaded->files.find(GetFileKey(file));
        return it == loaded->files.end() ? nullptr : it->second;
    };
    for (const auto& [key, asset] : loaded->files)
    {
        for (const STAI_TaskDef& task : asset->globalTasks)
            task.task->LinkReferencedTrees(find);
        for (const STAI_StateDef& state : asset->states)
            for (const STAI_TaskDef& task : state.tasks)
                task.task->LinkReferencedTrees(find);
    }

    for (const Slot& slot : slots)
    {
        auto it = loaded->files.find(GetFileKey(slot.file));
        if (it == loaded->files.end())
        {
            sLog.outError("StateTree: %s = %s not loaded, using the built-in tree", slot.key.c_str(), slot.file.c_str());
            continue;
        }
        loaded->trees[{ slot.state, slot.classId }] = it->second;
    }

    sLog.outString("StateTree: %u tree files loaded, %u tree slots configured (%u errors)", uint32(loaded->files.size()), uint32(loaded->trees.size()), errors);

    if (!fatalErrors.empty())
    {
        sLog.outError("StateTree: %u tree file(s) with fatal errors (see above), the server does not start", uint32(fatalErrors.size()));
        Log::WaitBeforeContinueIfNeed();
        exit(1);
    }

    std::lock_guard<std::mutex> lock(mutex);
    snapshot = std::move(loaded);
}

std::shared_ptr<const STAI_TreeLibrary::Snapshot> STAI_TreeLibrary::GetSnapshot() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return snapshot;
}

std::shared_ptr<const STAI_TreeAsset> STAI_TreeLibrary::Get(HLSStateType state, uint8 classId) const
{
    std::shared_ptr<const Snapshot> current = GetSnapshot();
    if (!current)
        return nullptr;

    auto it = current->trees.find({ state, classId });
    if (it == current->trees.end())
        it = current->trees.find({ state, uint8(0) });
    return it == current->trees.end() ? nullptr : it->second;
}

std::shared_ptr<const STAI_TreeAsset> STAI_TreeLibrary::Find(const std::string& file) const
{
    std::shared_ptr<const Snapshot> current = GetSnapshot();
    if (!current)
        return nullptr;

    auto it = current->files.find(GetFileKey(file));
    return it == current->files.end() ? nullptr : it->second;
}

std::shared_ptr<const STAI_TreeAsset> STAI_TreeLibrary::GetBuiltin(HLSStateType state)
{
    static const std::shared_ptr<const STAI_TreeAsset> builtin[] =
    {
        STAI_TreeAsset::MakeIdle("Idle_NonCombat"),
        STAI_TreeAsset::MakeIdle("Idle_Combat"),
        STAI_TreeAsset::MakeIdle("Idle_Reaction"),
        STAI_TreeAsset::MakeIdle("Idle_Dead"),
    };
    return builtin[(uint8)state];
}
