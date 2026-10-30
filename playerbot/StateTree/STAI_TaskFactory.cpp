#include "STAI_TaskFactory.h"

#include "playerbot/PlayerbotAIConfig.h"

using namespace ai;

namespace
{
    template<class Map>
    const typename Map::mapped_type* FindClass(const Map& classes, const std::string& name)
    {
        auto it = classes.find(name);
        return it == classes.end() ? nullptr : &it->second;
    }
}

STAI_NodeFactory::STAI_NodeFactory()
{
    RegisterBuiltinStateTreeTasks(*this);
}

STAI_NodeFactory& STAI_NodeFactory::instance()
{
    static STAI_NodeFactory factory;
    return factory;
}

const STAI_NodeFactory::TaskClass* STAI_NodeFactory::FindTask(const std::string& name) const
{
    return FindClass(tasks, name);
}

const STAI_NodeFactory::ConditionClass* STAI_NodeFactory::FindCondition(const std::string& name) const
{
    return FindClass(conditions, name);
}

const STAI_NodeFactory::ConsiderationClass* STAI_NodeFactory::FindConsideration(const std::string& name) const
{
    return FindClass(considerations, name);
}

std::filesystem::path STAI_NodeFactory::ResolvePath(const std::string& file)
{
    std::filesystem::path path(file);
    if (path.is_relative())
        path = std::filesystem::path(_D_AIPLAYERBOT_CONFIG).parent_path() / path;
    return path.lexically_normal();
}

std::string STAI_NodeFactory::GetBindingSource(const STAI_TaskConfig& config, const std::string& local)
{
    for (const auto& child : config)
    {
        if (child.first != "Binding" || child.second.get<std::string>("<xmlattr>.local", "") != local)
            continue;

        std::string external = child.second.get<std::string>("<xmlattr>.external", "");
        if (external.rfind("Root|", 0) == 0)
            external = external.substr(5);
        return external.empty() ? local : external;
    }
    return local;
}

std::optional<std::string> STAI_NodeFactory::GetSetting(const STAI_TaskConfig& config, const std::string& name, const std::string& attribute)
{
    auto setting = config.get_child_optional(boost::property_tree::ptree::path_type(name, '\0'));
    if (!setting)
        return std::nullopt;

    if (auto value = setting->get_optional<std::string>("<xmlattr>." + attribute))
        return *value;

    if (!setting->data().empty())
        return setting->data();

    return std::nullopt;
}
