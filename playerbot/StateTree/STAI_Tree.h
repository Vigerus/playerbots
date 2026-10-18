#pragma once

#include "playerbot/StateTree/STAI_TaskFactory.h"

#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
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

    // error in a tree file that stops the server (other load errors only leave the file out):
    // two nodes of one tree with the same name, a transition that is invalid or names an unknown node
    class STAI_TreeFatalError : public std::runtime_error
    {
    public:
        using std::runtime_error::runtime_error;
    };

    // <Task class="..."> task settings </Task>
    struct STAI_TaskDef
    {
        std::string taskClass;
        STAI_TaskConfig taskConfig;
        std::unique_ptr<STAI_Task> task;             // shared by every bot running the tree
    };

    // <Transition type="OnSuccess|OnFailure|OnFinish" node="Root|<node name>"/>: where the tree goes when the node
    // finished (OnSuccess: its last task succeeded, OnFailure: one of its tasks failed, OnFinish: either one)
    struct STAI_TransitionDef
    {
        enum class Type : uint8 { OnSuccess, OnFailure, OnFinish };

        Type type = Type::OnFinish;
        std::string path;   // Root|<node name>
        int target = -1;    // index of the node in the tree

        bool Matches(EActionRunStatus status) const
        {
            return type == Type::OnFinish || (type == Type::OnSuccess) == (status == EActionRunStatus::Succeeded);
        }
        std::string GetText() const;
    };

    // Node of a tree, its name is unique in the tree. Its tasks run one after another: the first is entered with the node; when one succeeds it
    // is exited and the next one entered; the node succeeds with its last task and fails with any of them.
    struct STAI_NodeDef
    {
        std::string name;
        std::vector<STAI_ConditionDef> conditions;   // all must pass, empty = always
        std::vector<STAI_TaskDef> tasks;             // in order, at least one
        // checked in order when the node finished, the first matching one is taken; none = the tree starts again
        // at its first node
        std::vector<STAI_TransitionDef> transitions;
    };

    // A state tree loaded from a STAI_*.xml file:
    //   <Root>
    //     <Global>                              optional, only directly under <Root>
    //       <Task class="..."> task settings </Task>
    //       ...
    //     </Global>
    //     <Node name="...">
    //       <Conditions> <Condition trigger="..."/> ... </Conditions>
    //       <Task class="..."> task settings </Task>
    //       ...                                   more tasks, run one after another
    //       <Transitions>                         optional
    //         <Transition type="OnSuccess" node="Root|<node name>"/>
    //         ...
    //       </Transitions>
    //     </Node>
    //     ...
    //   </Root>
    // Loaded once and shared by every bot: per bot only the contexts of the nodes differ (STAI_TreeInstance).
    class STAI_TreeAsset
    {
    public:
        std::string name;   // file stem, e.g. STAI_Combat_Mage
        std::string file;
        std::vector<STAI_NodeDef> nodes;
        // scopeless tasks: not part of any node, entered when the tree starts, ticked on every tick and exited
        // when it finishes (their OnTick result is ignored)
        std::vector<STAI_TaskDef> globalTasks;

        // Throws std::runtime_error with a readable message on parse/validation errors, STAI_TreeFatalError when
        // two nodes have the same name or a transition is invalid.
        static std::shared_ptr<const STAI_TreeAsset> Load(const std::string& file);

        // tree with one unconditional node running the given task class without settings
        static std::shared_ptr<const STAI_TreeAsset> MakeSingleTask(const std::string& name, const std::string& taskClass);
    };

    // Every STAI_*.xml of the statetree directory, preloaded and kept by file, plus the trees configured in
    // aiplayerbot.conf: AiPlayerbot.StateTree.<State>.<Class|Default> = <STAI_*.xml>
    class STAI_TreeLibrary
    {
    public:
        static STAI_TreeLibrary& instance()
        {
            static STAI_TreeLibrary library;
            return library;
        }

        // Loads the statetree directory and every configured file, once at server start (a config reload keeps
        // the trees); errors are logged and the file is left out (configured slots using it fall back to the
        // built-in tree), a STAI_TreeFatalError stops the server.
        void LoadFromConfig();
        bool IsLoaded() const { return GetSnapshot() != nullptr; }

        // tree for the class (or Default), nullptr when none is configured
        std::shared_ptr<const STAI_TreeAsset> Get(HLSStateType state, uint8 classId) const;
        // loaded tree file (path relative to the config directory, e.g. statetree/STAI_NonCombat.xml), nullptr when not loaded
        std::shared_ptr<const STAI_TreeAsset> Find(const std::string& file) const;
        // one legacy engine node, used when no tree is configured for the state
        static std::shared_ptr<const STAI_TreeAsset> GetBuiltin(HLSStateType state);

    private:
        using TreeMap = std::map<std::pair<HLSStateType, uint8>, std::shared_ptr<const STAI_TreeAsset>>; // class 0 = Default
        using FileMap = std::map<std::string, std::shared_ptr<const STAI_TreeAsset>>;                    // by GetFileKey

        struct Snapshot
        {
            TreeMap trees;
            FileMap files;
        };

        static std::string GetFileKey(const std::string& file);
        static void RemoveBrokenSubtrees(FileMap& files, uint32& errors);
        std::shared_ptr<const Snapshot> GetSnapshot() const;

        mutable std::mutex mutex;
        std::shared_ptr<const Snapshot> snapshot;
    };
}

#define sSTAITreeLibrary ai::STAI_TreeLibrary::instance()
