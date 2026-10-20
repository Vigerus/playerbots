#pragma once

#include "playerbot/StateTree/Tasks/STAI_Task.h"

#include <boost/property_tree/ptree.hpp>

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>

namespace ai
{
    // <Task class="..."> element of a STAI tree, children are the task's own settings
    using STAI_TaskConfig = boost::property_tree::ptree;

    // Task classes usable in STAI_*.xml. Each class validates its config when the tree file is loaded
    // (throw std::runtime_error to reject the file) and creates the node's task, shared by every bot.
    class STAI_TaskFactory
    {
    public:
        struct TaskClass
        {
            std::function<void(const STAI_TaskConfig& config)> validate;
            std::function<std::unique_ptr<STAI_Task>(const STAI_TaskConfig& config)> create;
        };

        static STAI_TaskFactory& instance();

        void RegisterTask(const std::string& name, TaskClass taskClass) { tasks[name] = std::move(taskClass); }
        const TaskClass* FindTask(const std::string& name) const;

        // files named in tree/task configs are relative to the directory of aiplayerbot.conf
        static std::filesystem::path ResolvePath(const std::string& file);

        // string value of a task setting: <Name value="..."/> (or <Name>...</Name>), nullopt when missing
        static std::optional<std::string> GetSetting(const STAI_TaskConfig& config, const std::string& name, const std::string& attribute = "value");
        // the external path of the task's <Binding local="..."> without "Root|" (what a property is bound to, for
        // the debug info), the local name when it is not bound
        static std::string GetBindingSource(const STAI_TaskConfig& config, const std::string& local);

    private:
        STAI_TaskFactory();

        std::map<std::string, TaskClass> tasks;
    };

    // registers the built-in task classes (Tasks/STAI_BuiltinTasks.cpp)
    void RegisterBuiltinStateTreeTasks(STAI_TaskFactory& factory);
}

#define sSTAITaskFactory ai::STAI_TaskFactory::instance()
