#include "STAI_Tree.h"

#include "playerbot/PlayerbotAIConfig.h"

#include <boost/property_tree/xml_parser.hpp>

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
    for (const auto& [key, node] : *root)
    {
        if (key == "<xmlattr>" || key == "<xmlcomment>")
            continue;
        if (key != "Node")
            throw std::runtime_error(file + ": unexpected <" + key + "> in <Root> (expected <Node>)");

        ++index;
        const std::string where = file + " node #" + std::to_string(index);

        STAI_NodeDef def;

        if (auto conditions = node.get_child_optional("Conditions"))
        {
            for (const auto& [conditionKey, condition] : *conditions)
            {
                if (conditionKey == "<xmlattr>" || conditionKey == "<xmlcomment>")
                    continue;
                if (conditionKey != "Condition")
                    throw std::runtime_error(where + ": unexpected <" + conditionKey + "> in <Conditions> (expected <Condition trigger=\"...\"/>)");
                def.conditions.push_back(ParseCondition(condition, where));
            }
        }

        auto task = node.get_child_optional("Task");
        if (!task)
            throw std::runtime_error(where + ": missing <Task class=\"...\">");

        def.taskClass = Attribute(*task, "class");
        const STAI_TaskFactory::TaskClass* taskClass = sSTAITaskFactory.FindTask(def.taskClass);
        if (!taskClass)
            throw std::runtime_error(where + ": unknown task class '" + def.taskClass + "'");

        def.taskConfig = *task;
        try
        {
            taskClass->validate(def.taskConfig);
        }
        catch (const std::exception& error)
        {
            throw std::runtime_error(where + " (" + def.taskClass + "): " + error.what());
        }

        def.name = Attribute(node, "name");
        if (def.name.empty())
            def.name = def.taskClass;

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

    STAI_NodeDef def;
    def.name = taskClass;
    def.taskClass = taskClass;
    def.taskConfig.put("<xmlattr>.class", taskClass);
    asset->nodes.push_back(std::move(def));

    return asset;
}

void STAI_TreeLibrary::LoadFromConfig()
{
    auto loaded = std::make_shared<TreeMap>();
    std::map<std::string, std::shared_ptr<const STAI_TreeAsset>> files;
    uint32 errors = 0;

    auto loadSlot = [&](HLSStateType state, const char* stateName, uint8 classId, const char* slotName)
    {
        const std::string key = std::string("AiPlayerbot.StateTree.") + stateName + "." + slotName;
        const std::string file = sPlayerbotAIConfig.GetConfigString(key, "");
        if (file.empty())
            return;

        auto cached = files.find(file);
        if (cached == files.end())
        {
            try
            {
                cached = files.emplace(file, STAI_TreeAsset::Load(file)).first;
            }
            catch (const std::exception& error)
            {
                sLog.outError("StateTree: %s = %s not loaded, using the built-in tree: %s", key.c_str(), file.c_str(), error.what());
                ++errors;
                return;
            }
        }

        (*loaded)[{ state, classId }] = cached->second;
    };

    for (const auto& [state, stateName] : stateNames)
    {
        loadSlot(state, stateName, 0, "Default");
        for (const auto& [classId, className] : classNames)
            loadSlot(state, stateName, classId, className);
    }

    sLog.outString("StateTree: %u tree slots configured from %u files (%u errors)", uint32(loaded->size()), uint32(files.size()), errors);

    std::lock_guard<std::mutex> lock(mutex);
    trees = std::move(loaded);
}

std::shared_ptr<const STAI_TreeAsset> STAI_TreeLibrary::Get(HLSStateType state, uint8 classId) const
{
    std::shared_ptr<const TreeMap> current;
    {
        std::lock_guard<std::mutex> lock(mutex);
        current = trees;
    }

    if (!current)
        return nullptr;

    auto it = current->find({ state, classId });
    if (it == current->end())
        it = current->find({ state, uint8(0) });
    return it == current->end() ? nullptr : it->second;
}
