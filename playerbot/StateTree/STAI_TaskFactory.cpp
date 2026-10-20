#include "STAI_TaskFactory.h"

#include "playerbot/PlayerbotAIConfig.h"

using namespace ai;

STAI_TaskFactory::STAI_TaskFactory()
{
    RegisterBuiltinStateTreeTasks(*this);
}

STAI_TaskFactory& STAI_TaskFactory::instance()
{
    static STAI_TaskFactory factory;
    return factory;
}

const STAI_TaskFactory::TaskClass* STAI_TaskFactory::FindTask(const std::string& name) const
{
    auto it = tasks.find(name);
    return it == tasks.end() ? nullptr : &it->second;
}

std::filesystem::path STAI_TaskFactory::ResolvePath(const std::string& file)
{
    std::filesystem::path path(file);
    if (path.is_relative())
        path = std::filesystem::path(_D_AIPLAYERBOT_CONFIG).parent_path() / path;
    return path.lexically_normal();
}

std::string STAI_TaskFactory::GetBindingSource(const STAI_TaskConfig& config, const std::string& local)
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

std::optional<std::string> STAI_TaskFactory::GetSetting(const STAI_TaskConfig& config, const std::string& name, const std::string& attribute)
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
