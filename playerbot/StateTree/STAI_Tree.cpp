#include "STAI_Tree.h"

#include "playerbot/PlayerbotAIConfig.h"

#include <boost/property_tree/xml_parser.hpp>

#include <algorithm>
#include <cctype>
#include <functional>
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
        if (def.trigger.empty())
            throw std::runtime_error(where + ": <Condition> needs a trigger=\"...\" (a trigger name, e.g. \"critical health\" or \"has aura::spellid::23341::greater or equal::5\")");

        const std::string negate = Attribute(condition, "negate");
        if (negate == "true" || negate == "1")
            def.negate = true;
        else if (!negate.empty() && negate != "false" && negate != "0")
            throw std::runtime_error(where + ": negate must be true or false, got '" + negate + "'");

        return def;
    }

    // <Task class="..."> settings </Task>: validated and created (shared by every bot)
    void ParseTask(const ptree& task, const std::string& where, STAI_TaskDef& def)
    {
        def.taskClass = Attribute(task, "class");
        const STAI_TaskFactory::TaskClass* taskClass = sSTAITaskFactory.FindTask(def.taskClass);
        if (!taskClass)
            throw std::runtime_error(where + ": unknown task class '" + def.taskClass + "'");

        def.taskConfig = task;
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
            {
                for (const auto& [conditionKey, condition] : child)
                {
                    if (conditionKey == "<xmlattr>" || conditionKey == "<xmlcomment>")
                        continue;
                    if (conditionKey != "Condition")
                        throw std::runtime_error(where + ": unexpected <" + conditionKey + "> in <Conditions> (expected <Condition trigger=\"...\"/>)");
                    def.conditions.push_back(ParseCondition(condition, where));
                }
            }
            else if (childKey == "Task")
            {
                // tasks of a node run one after another, in file order
                STAI_TaskDef task;
                ParseTask(child, where + " task #" + std::to_string(def.tasks.size() + 1), task);
                def.tasks.push_back(std::move(task));
            }
            else if (childKey == "Global")
                throw std::runtime_error(where + ": <Global> is only allowed directly under <Root>");
            else
                throw std::runtime_error(where + ": unexpected <" + childKey + "> in <Node> (expected <Conditions> or <Task>)");
        }

        if (def.tasks.empty())
            throw std::runtime_error(where + ": missing <Task class=\"...\">");

        def.name = Attribute(node, "name");
        if (def.name.empty())
            def.name = def.tasks.front().taskClass;

        asset->nodes.push_back(std::move(def));
    }

    if (asset->nodes.empty())
        throw std::runtime_error(file + ": no <Node> in <Root>");

    return asset;
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
    auto loaded = std::make_shared<Snapshot>();
    uint32 errors = 0;

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
