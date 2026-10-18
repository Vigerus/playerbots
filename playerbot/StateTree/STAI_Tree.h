#pragma once

#include "playerbot/StateTree/STAI_TaskFactory.h"

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace ai
{
    // <Condition trigger="distance from master::greater::10" negate="false"/>: passes while the bot's trigger of
    // that name (same names and "::" qualifiers as in strategies and custom strategies) is active, or inactive
    // with negate="true"
    struct STAI_ConditionDef
    {
        std::string trigger;
        bool negate = false;

        std::string GetText() const { return negate ? "NOT " + trigger : trigger; }
    };

    struct STAI_NodeDef
    {
        std::string name;
        std::vector<STAI_ConditionDef> conditions;   // all must pass, empty = always
        std::string taskClass;
        STAI_TaskConfig taskConfig;
    };

    // A state tree loaded from a STAI_*.xml file:
    //   <Root>
    //     <Node name="...">
    //       <Conditions> <Condition trigger="..."/> ... </Conditions>
    //       <Task class="..."> task settings </Task>
    //     </Node>
    //     ...
    //   </Root>
    class STAI_TreeAsset
    {
    public:
        std::string name;   // file stem, e.g. STAI_Combat_Mage
        std::string file;
        std::vector<STAI_NodeDef> nodes;

        // Throws std::runtime_error with a readable message on parse/validation errors.
        static std::shared_ptr<const STAI_TreeAsset> Load(const std::string& file);

        // tree with one unconditional node running the given task class without settings
        static std::shared_ptr<const STAI_TreeAsset> MakeSingleTask(const std::string& name, const std::string& taskClass);
    };

    // Trees configured in aiplayerbot.conf: AiPlayerbot.StateTree.<State>.<Class|Default> = <STAI_*.xml>
    class STAI_TreeLibrary
    {
    public:
        static STAI_TreeLibrary& instance()
        {
            static STAI_TreeLibrary library;
            return library;
        }

        // (Re)loads every configured file; errors are logged and the slot stays unconfigured.
        void LoadFromConfig();

        // tree for the class (or Default), nullptr when none is configured
        std::shared_ptr<const STAI_TreeAsset> Get(HLSStateType state, uint8 classId) const;

    private:
        using TreeMap = std::map<std::pair<HLSStateType, uint8>, std::shared_ptr<const STAI_TreeAsset>>; // class 0 = Default

        mutable std::mutex mutex;
        std::shared_ptr<const TreeMap> trees;
    };
}

#define sSTAITreeLibrary ai::STAI_TreeLibrary::instance()
