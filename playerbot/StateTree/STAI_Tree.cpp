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

    std::string Attribute(const ptree& node, const std::string& name)
    {
        return node.get<std::string>("<xmlattr>." + name, "");
    }

    STAI_ConditionDef ParseCondition(const ptree& condition, const std::string& where)
    {
        STAI_ConditionDef def;
        def.trigger = Attribute(condition, "trigger");
        def.binding.path = Attribute(condition, "binding");
        if (def.trigger.empty() == def.binding.path.empty())
            throw std::runtime_error(where + ": <Condition> needs either a trigger=\"...\" (a trigger name, e.g. \"critical health\" or \"has aura::spellid::23341::greater or equal::5\") or a binding=\"Root|...\" (a task property)");

        const std::string negate = Attribute(condition, "negate");
        if (negate == "true" || negate == "1")
            def.negate = true;
        else if (!negate.empty() && negate != "false" && negate != "0")
            throw std::runtime_error(where + ": negate must be true or false, got '" + negate + "'");

        def.compareText = Attribute(condition, "compare");
        if (!def.compareText.empty())
        {
            using Compare = STAI_ConditionDef::Compare;
            static const std::pair<const char*, Compare> compares[] =
            {
                { "equal", Compare::Equal }, { "lesser", Compare::Lesser }, { "lesser or equal", Compare::LesserOrEqual },
                { "greater", Compare::Greater }, { "greater or equal", Compare::GreaterOrEqual },
            };
            for (const auto& [name, compare] : compares)
                if (def.compareText == name)
                    def.compare = compare;
            if (def.compare == Compare::None)
                throw std::runtime_error(where + ": compare must be equal, lesser, lesser or equal, greater or greater or equal, got '" + def.compareText + "'");
            if (!def.trigger.empty())
                throw std::runtime_error(where + ": compare is only allowed on a binding condition");

            const std::string value = Attribute(condition, "value");
            char* end = nullptr;
            def.value = std::strtod(value.c_str(), &end);
            if (value.empty() || *end)
                throw std::runtime_error(where + ": compare needs a number in value=\"...\", got '" + value + "'");
        }

        return def;
    }

    // <Conditions match="all|any"> <Condition .../> ... </Conditions>
    STAI_ConditionsDef ParseConditions(const ptree& conditions, const std::string& where)
    {
        STAI_ConditionsDef def;

        const std::string match = Attribute(conditions, "match");
        if (match == "any")
            def.any = true;
        else if (!match.empty() && match != "all")
            throw std::runtime_error(where + ": <Conditions> match must be all or any, got '" + match + "'");

        for (const auto& [conditionKey, condition] : conditions)
        {
            if (conditionKey == "<xmlattr>" || conditionKey == "<xmlcomment>")
                continue;
            if (conditionKey != "Condition")
                throw std::runtime_error(where + ": unexpected <" + conditionKey + "> in <Conditions> (expected <Condition trigger=\"...\"/>)");
            def.list.push_back(ParseCondition(condition, where));
        }
        return def;
    }

    const char* const rootPathPrefix = "Root|";

    // <Transition type="..." node="Root|..."/>, the target node and the source delegate are resolved once the whole tree is read
    STAI_TransitionDef ParseTransition(const ptree& transition, const std::string& where)
    {
        STAI_TransitionDef def;

        const std::string type = Attribute(transition, "type");
        if (type == "OnSuccess")
            def.type = STAI_TransitionDef::Type::OnSuccess;
        else if (type == "OnFailure")
            def.type = STAI_TransitionDef::Type::OnFailure;
        else if (type == "OnFinish")
            def.type = STAI_TransitionDef::Type::OnFinish;
        else if (type == "OnDelegate")
            def.type = STAI_TransitionDef::Type::OnDelegate;
        else
            throw STAI_TreeFatalError(where + ": <Transition> type must be OnSuccess, OnFailure, OnFinish or OnDelegate, got '" + type + "'");

        def.source.path = Attribute(transition, "source");
        if ((def.type == STAI_TransitionDef::Type::OnDelegate) == def.source.path.empty())
            throw STAI_TreeFatalError(where + ": <Transition> source=\"Root|<TaskClass>::<delegate>\" is needed for OnDelegate and only allowed there");

        def.path = Attribute(transition, "node");
        if (def.path != "Root" && (def.path.rfind(rootPathPrefix, 0) != 0 || def.path.size() == strlen(rootPathPrefix)))
            throw STAI_TreeFatalError(where + ": <Transition> node must be Root or a path starting with Root, e.g. \"Root|Legacy\", got '" + def.path + "'");

        for (const auto& [key, child] : transition)
        {
            if (key == "<xmlattr>" || key == "<xmlcomment>")
                continue;
            if (key != "Conditions")
                throw STAI_TreeFatalError(where + ": unexpected <" + key + "> in <Transition> (expected <Conditions>)");
            def.conditions = ParseConditions(child, where);
        }

        return def;
    }

    // <Task class="..."> settings </Task>: validated and created (shared by every bot)
    void ParseTask(const ptree& task, const std::string& where, STAI_TaskDef& def)
    {
        def.taskClass = Attribute(task, "class");
        const STAI_TaskFactory::TaskClass* taskClass = sSTAITaskFactory.FindTask(def.taskClass);
        if (!taskClass)
            throw std::runtime_error(where + ": unknown task class '" + def.taskClass + "'");

        // <Binding>s are handled by the tree, the task only sees its own settings
        def.taskConfig = task;
        def.taskConfig.erase("Binding");
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
    }
}

std::shared_ptr<const STAI_TreeAsset> STAI_TreeAsset::Load(const std::string& file)
{
    const std::filesystem::path path = STAI_TaskFactory::ResolvePath(file);

    ptree document;
    try
    {
        boost::property_tree::read_xml(path.string(), document, boost::property_tree::xml_parser::trim_whitespace | boost::property_tree::xml_parser::no_comments);
    }
    catch (const boost::property_tree::xml_parser_error& error)
    {
        throw std::runtime_error(file + ":" + std::to_string(error.line()) + ": " + error.message());
    }

    auto root = document.get_child_optional("Root");
    if (!root)
        throw std::runtime_error(file + ": missing <Root> element");

    auto asset = std::make_shared<STAI_TreeAsset>();
    asset->file = file;
    asset->name = path.stem().string();

    int index = 0;
    bool hasGlobal = false;
    std::map<std::string, int> nodeIndexByName; // node names are unique in a tree
    for (const auto& [key, node] : *root)
    {
        if (key == "<xmlattr>" || key == "<xmlcomment>")
            continue;

        if (key == "Global")
        {
            if (hasGlobal)
                throw std::runtime_error(file + ": more than one <Global> in <Root>");
            hasGlobal = true;

            int globalIndex = 0;
            for (const auto& [taskKey, task] : node)
            {
                if (taskKey == "<xmlattr>" || taskKey == "<xmlcomment>")
                    continue;
                if (taskKey != "Task")
                    throw std::runtime_error(file + ": unexpected <" + taskKey + "> in <Global> (expected <Task class=\"...\">)");

                ++globalIndex;
                STAI_TaskDef def;
                ParseTask(task, file + " global task #" + std::to_string(globalIndex), def);
                asset->globalTasks.push_back(std::move(def));
            }
            continue;
        }

        if (key != "Node")
            throw std::runtime_error(file + ": unexpected <" + key + "> in <Root> (expected <Node> or <Global>)");

        ++index;
        const std::string where = file + " node #" + std::to_string(index);

        STAI_NodeDef def;

        for (const auto& [childKey, child] : node)
        {
            if (childKey == "<xmlattr>" || childKey == "<xmlcomment>")
                continue;

            if (childKey == "Conditions")
                def.conditions = ParseConditions(child, where);
            else if (childKey == "Task")
            {
                // tasks of a node run one after another, in file order
                STAI_TaskDef task;
                ParseTask(child, where + " task #" + std::to_string(def.tasks.size() + 1), task);
                def.tasks.push_back(std::move(task));
            }
            else if (childKey == "Transitions")
            {
                for (const auto& [transitionKey, transition] : child)
                {
                    if (transitionKey == "<xmlattr>" || transitionKey == "<xmlcomment>")
                        continue;
                    if (transitionKey != "Transition")
                        throw STAI_TreeFatalError(where + ": unexpected <" + transitionKey + "> in <Transitions> (expected <Transition type=\"...\" node=\"Root|...\"/>)");
                    def.transitions.push_back(ParseTransition(transition, where));
                }
            }
            else if (childKey == "Global")
                throw std::runtime_error(where + ": <Global> is only allowed directly under <Root>");
            else
                throw std::runtime_error(where + ": unexpected <" + childKey + "> in <Node> (expected <Conditions>, <Task> or <Transitions>)");
        }

        if (def.tasks.empty())
            throw std::runtime_error(where + ": missing <Task class=\"...\">");

        def.name = Attribute(node, "name");
        const bool named = !def.name.empty();
        if (!named)
            def.name = def.tasks.front().taskClass;

        auto [used, inserted] = nodeIndexByName.emplace(def.name, index);
        if (!inserted)
            throw STAI_TreeFatalError(where + ": node name '" + def.name + "' is already used by node #" + std::to_string(used->second)
                + (named ? "" : " (a node without name=\"...\" is named after its first task class)") + ", node names must be unique in a tree");

        asset->nodes.push_back(std::move(def));
    }

    if (asset->nodes.empty())
        throw std::runtime_error(file + ": no <Node> in <Root>");

    // properties and delegates: Root|<TaskClass>::<name> (global task) or Root|<node name>|<TaskClass>::<name>
    auto resolve = [&](STAI_MemberRef& ref, const std::string& where) -> const STAI_Task&
    {
        auto fail = [&](const std::string& why) { return STAI_TreeFatalError(where + ": '" + ref.path + "': " + why); };

        if (ref.path.rfind(rootPathPrefix, 0) != 0)
            throw fail("expected Root|<TaskClass>::<name> or Root|<node name>|<TaskClass>::<name>");
        std::string rest = ref.path.substr(strlen(rootPathPrefix));

        const std::vector<STAI_TaskDef>* tasks = &asset->globalTasks;
        ref.node = -1;
        const size_t separator = rest.find('|');
        if (separator != std::string::npos)
        {
            auto node = nodeIndexByName.find(rest.substr(0, separator));
            if (node == nodeIndexByName.end())
                throw fail("unknown node '" + rest.substr(0, separator) + "'");
            ref.node = node->second - 1;
            tasks = &asset->nodes[ref.node].tasks;
            rest = rest.substr(separator + 1);
        }

        const size_t member = rest.find("::");
        if (member == std::string::npos || member == 0 || member + 2 == rest.size())
            throw fail("expected <TaskClass>::<name>");
        const std::string taskClass = rest.substr(0, member);
        ref.member = rest.substr(member + 2);

        for (size_t i = 0; i < tasks->size(); ++i)
        {
            if ((*tasks)[i].taskClass != taskClass)
                continue;
            ref.task = int(i);
            return *(*tasks)[i].task;
        }
        throw fail("no task of class '" + taskClass + "' in " + (ref.node < 0 ? "<Global>" : "the node"));
    };

    auto resolveBindings = [&](STAI_TaskDef& task, const std::string& where)
    {
        for (STAI_BindingDef& binding : task.bindings)
        {
            binding.localProperty = task.task->FindProperty(binding.local);
            if (!binding.localProperty)
                throw STAI_TreeFatalError(where + " (" + task.taskClass + "): <Binding> local '" + binding.local + "' is not a property of the task");

            binding.externalProperty = resolve(binding.external, where).FindProperty(binding.external.member);
            if (!binding.externalProperty)
                throw STAI_TreeFatalError(where + ": <Binding> external '" + binding.external.path + "' is not a property of that task");
            if (binding.externalProperty->type != binding.localProperty->type)
                throw STAI_TreeFatalError(where + ": <Binding> '" + binding.local + "' and '" + binding.external.path + "' have different types");
        }
    };

    for (size_t i = 0; i < asset->globalTasks.size(); ++i)
        resolveBindings(asset->globalTasks[i], file + " global task #" + std::to_string(i + 1));

    for (size_t i = 0; i < asset->nodes.size(); ++i)
    {
        const std::string where = file + " node #" + std::to_string(i + 1);
        for (STAI_TaskDef& task : asset->nodes[i].tasks)
            resolveBindings(task, where);

        auto resolveConditions = [&](STAI_ConditionsDef& conditions)
        {
            for (STAI_ConditionDef& condition : conditions.list)
            {
                if (!condition.trigger.empty())
                    continue;
                condition.property = resolve(condition.binding, where).FindProperty(condition.binding.member);
                if (!condition.property)
                    throw STAI_TreeFatalError(where + ": <Condition> binding '" + condition.binding.path + "' is not a property of that task");
                if (condition.compare == STAI_ConditionDef::Compare::None && !condition.property->isSet)
                    throw STAI_TreeFatalError(where + ": <Condition> binding '" + condition.binding.path + "' has a type that cannot be tested (not a container, not bool)");
                if (condition.compare != STAI_ConditionDef::Compare::None && !condition.property->toNumber)
                    throw STAI_TreeFatalError(where + ": <Condition> binding '" + condition.binding.path + "' is not a number, it cannot be compared");
            }
        };

        resolveConditions(asset->nodes[i].conditions);

        for (STAI_TransitionDef& transition : asset->nodes[i].transitions)
        {
            resolveConditions(transition.conditions);
            if (transition.type != STAI_TransitionDef::Type::OnDelegate)
                continue;
            transition.delegate = resolve(transition.source, where).FindDelegate(transition.source.member);
            if (!transition.delegate)
                throw STAI_TreeFatalError(where + ": <Transition> source '" + transition.source.path + "' is not a delegate of that task");
        }
    }

    // transition targets: Root|<node name>, nodes are direct children of Root
    for (size_t i = 0; i < asset->nodes.size(); ++i)
    {
        for (STAI_TransitionDef& transition : asset->nodes[i].transitions)
        {
            if (transition.path == "Root")
            {
                transition.target = -1;
                continue;
            }

            const std::string name = transition.path.substr(strlen(rootPathPrefix));
            auto target = nodeIndexByName.find(name);
            if (target == nodeIndexByName.end())
                throw STAI_TreeFatalError(file + " node #" + std::to_string(i + 1) + ": <Transition> to unknown node '" + transition.path + "'");
            transition.target = target->second - 1;
        }
    }

    return asset;
}

bool STAI_ConditionDef::Passes(const void* boundValue) const
{
    bool passed;
    if (compare == Compare::None)
        passed = property->isSet(boundValue);
    else
    {
        const double number = property->toNumber(boundValue);
        switch (compare)
        {
            case Compare::Equal: passed = number == value; break;
            case Compare::Lesser: passed = number < value; break;
            case Compare::LesserOrEqual: passed = number <= value; break;
            case Compare::Greater: passed = number > value; break;
            default: passed = number >= value; break;
        }
    }
    return passed != negate;
}

std::string STAI_ConditionDef::GetText() const
{
    std::string text = trigger.empty() ? binding.path : trigger;
    if (compare != Compare::None)
    {
        std::ostringstream out;
        out << " " << compareText << " " << value;
        text += out.str();
    }
    return negate ? "NOT " + text : text;
}

std::string STAI_TransitionDef::GetText() const
{
    std::string text;
    if (type == Type::OnDelegate)
        text = "OnDelegate " + source.path + " " + path;
    else
        text = std::string(type == Type::OnSuccess ? "OnSuccess" : type == Type::OnFailure ? "OnFailure" : "OnFinish") + " " + path;

    // the transition's own conditions: "... IF a AND b" / "... IF a OR b"
    for (size_t i = 0; i < conditions.list.size(); ++i)
        text += (i ? (conditions.any ? " OR " : " AND ") : " IF ") + conditions.list[i].GetText();
    return text;
}

std::shared_ptr<const STAI_TreeAsset> STAI_TreeAsset::MakeSingleTask(const std::string& name, const std::string& taskClass)
{
    auto asset = std::make_shared<STAI_TreeAsset>();
    asset->name = name;

    STAI_TaskDef task;
    task.taskClass = taskClass;
    task.taskConfig.put("<xmlattr>.class", taskClass);
    if (const STAI_TaskFactory::TaskClass* factoryClass = sSTAITaskFactory.FindTask(taskClass))
        task.task = factoryClass->create(task.taskConfig);
    if (!task.task)
        throw std::runtime_error("unknown task class '" + taskClass + "'");

    STAI_NodeDef def;
    def.name = taskClass;
    def.tasks.push_back(std::move(task));
    asset->nodes.push_back(std::move(def));

    return asset;
}

std::string STAI_TreeLibrary::GetFileKey(const std::string& file)
{
    std::string key = STAI_TaskFactory::ResolvePath(file).generic_string();
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
        for (const STAI_NodeDef& node : asset.nodes)
            for (const STAI_TaskDef& task : node.tasks)
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

    // every tree of the statetree directory, so subtrees need no config entry
    const std::filesystem::path directory = STAI_TaskFactory::ResolvePath("statetree");
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(directory, error))
    {
        const std::filesystem::path& path = entry.path();
        if (entry.is_regular_file() && path.extension() == ".xml" && path.stem().string().rfind("STAI_", 0) == 0)
            loadFile("statetree/" + path.filename().string());
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
        STAI_TreeAsset::MakeSingleTask("Legacy_NonCombat", "StateNode_Legacy"),
        STAI_TreeAsset::MakeSingleTask("Legacy_Combat", "StateNode_Legacy"),
        STAI_TreeAsset::MakeSingleTask("Legacy_Reaction", "StateNode_Reaction"),
        STAI_TreeAsset::MakeSingleTask("Legacy_Dead", "StateNode_Legacy"),
    };
    return builtin[(uint8)state];
}
