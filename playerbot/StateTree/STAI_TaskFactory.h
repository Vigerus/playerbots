#pragma once

#include "playerbot/StateTree/Tasks/STAI_Task.h"
#include "playerbot/StateTree/Conditions/STAI_ConditionBase.h"
#include "playerbot/StateTree/Considerations/STAI_ConsiderationBase.h"

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>

namespace ai
{
    // Node classes usable in STAI_*.xml: tasks (<Task class="...">), conditions (<Condition class="...">) and
    // considerations (<Consideration class="...">). Each class validates its config when the tree file is loaded
    // (throw std::runtime_error to reject the file) and creates the node, shared by every bot.
    class STAI_NodeFactory
    {
    public:
        template<class Node>
        struct NodeClass
        {
            std::function<void(const STAI_TaskConfig& config)> validate;
            std::function<std::unique_ptr<Node>(const STAI_TaskConfig& config)> create;
        };
        using TaskClass = NodeClass<STAI_TaskBase>;
        using ConditionClass = NodeClass<STAI_ConditionBase>;
        using ConsiderationClass = NodeClass<STAI_ConsiderationBase>;

        static STAI_NodeFactory& instance();

        void RegisterTask(const std::string& name, TaskClass taskClass) { tasks[name] = std::move(taskClass); }
        void RegisterCondition(const std::string& name, ConditionClass conditionClass) { conditions[name] = std::move(conditionClass); }
        void RegisterConsideration(const std::string& name, ConsiderationClass considerationClass) { considerations[name] = std::move(considerationClass); }
        const TaskClass* FindTask(const std::string& name) const;
        const ConditionClass* FindCondition(const std::string& name) const;
        const ConsiderationClass* FindConsideration(const std::string& name) const;

        // files named in tree/task configs are relative to the directory of aiplayerbot.conf
        static std::filesystem::path ResolvePath(const std::string& file);

        // string value of a task setting: <Name value="..."/> (or <Name>...</Name>), nullopt when missing
        static std::optional<std::string> GetSetting(const STAI_TaskConfig& config, const std::string& name, const std::string& attribute = "value");
        // the external path of the task's <Binding local="..."> without "Root|" (what a property is bound to, for
        // the debug info), the local name when it is not bound
        static std::string GetBindingSource(const STAI_TaskConfig& config, const std::string& local);

    private:
        STAI_NodeFactory();

        std::map<std::string, TaskClass> tasks;
        std::map<std::string, ConditionClass> conditions;
        std::map<std::string, ConsiderationClass> considerations;
    };

    // the factory under the name the tasks use
    using STAI_TaskFactory = STAI_NodeFactory;

    // registers the built-in task, condition and consideration classes (Tasks/STAI_BuiltinTasks.cpp)
    void RegisterBuiltinStateTreeTasks(STAI_NodeFactory& factory);
}

#define sSTAITaskFactory ai::STAI_NodeFactory::instance()
