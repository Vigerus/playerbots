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
    // <Binding local="needs" external="Root|BuffProvider_GlobalTask::needs"/> in a <Task>: the external property
    // is copied into the task's local one before the task's enter and every tick (same type)
    struct STAI_BindingDef
    {
        std::string local;
        STAI_MemberRef external;
        const STAI_PropertyDef* localProperty = nullptr;
        const STAI_PropertyDef* externalProperty = nullptr;
    };

    // error in a tree file that stops the server (other load errors only leave the file out): two states of one
    // tree with the same name, a path, transition or linked subtree that is invalid or names an unknown state
    class STAI_TreeFatalError : public std::runtime_error
    {
    public:
        using std::runtime_error::runtime_error;
    };

    // an entry of a condition list: a <Condition class="..."> (Leaf) or an <All> / <Any> group of entries
    struct STAI_ConditionExpr
    {
        enum class Op : uint8 { Leaf, All, Any };

        Op op = Op::Leaf;
        std::unique_ptr<STAI_ConditionBase> condition;  // Leaf
        int node = -1;                                  // Leaf: index in STAI_TreeAsset::nodes (its context per bot)
        std::vector<STAI_ConditionExpr> children;       // All / Any

        // "(a OR b)", "(a AND b)", the condition's text
        std::string GetText() const;
    };

    // <EnterConditions match="all|any"> of a state, <Conditions match="all|any"> of a transition or a global task:
    // all entries must pass (default), or one of them; empty always passes
    struct STAI_ConditionsDef
    {
        bool any = false;
        std::vector<STAI_ConditionExpr> list;

        bool empty() const { return list.empty(); }
        size_t size() const { return list.size(); }
    };

    // <Consideration class="..."> of a state
    struct STAI_ConsiderationDef
    {
        std::unique_ptr<STAI_ConsiderationBase> consideration;
        int node = -1;
    };

    // <Task class="..." name="..." completion="..."> task settings </Task>; a global task may have <Conditions>:
    // the tree checks them before every tick of the task and sets its context's bActive (MovementGoal_GlobalTask)
    struct STAI_TaskDef
    {
        std::string taskClass;
        std::string name;                            // name="...": Root|<TaskClass>[<name>]::<member> picks it
        STAI_ConditionsDef conditions;               // global tasks only
        STAI_TaskConfig taskConfig;                  // without the <Binding>, <Conditions> and <Comment> elements
        std::vector<STAI_BindingDef> bindings;
        std::unique_ptr<STAI_TaskBase> task;         // shared by every bot running the tree
        int node = -1;                               // index in STAI_TreeAsset::nodes
    };

    // <Transition trigger="..." target="..." source="..." event="..." priority="..." delay="...">: see
    // statetree/STATETREE_FORMAT.md
    struct STAI_TransitionDef
    {
        enum class Target : uint8 { State, Root, Succeeded, Failed };

        ESTAI_TransitionTrigger trigger = ESTAI_TransitionTrigger::OnStateCompleted;
        ESTAI_TransitionPriority priority = ESTAI_TransitionPriority::Normal;
        uint32 delayMs = 0;
        STAI_MemberRef source;                          // OnDelegate
        const STAI_DelegateDef* delegate = nullptr;     // OnDelegate, resolved when the tree is loaded
        std::string event;                              // OnEvent
        std::string path;                               // the target as written: Root, Root|<state>, NextState, Succeeded, Failed
        Target target = Target::Root;
        int state = -1;                                 // Target::State: index of the state in the tree (NextState resolved)
        STAI_ConditionsDef conditions;                  // all / any must pass for the transition to be taken

        // a completion trigger matching how the state completed
        bool Matches(EActionRunStatus status) const;
        std::string GetText() const;
    };

    // <State name="..." type="..." selection="..." weight="..." retryDelay="...">, its name is unique in the tree.
    // States are kept depth first: a state comes before its children, its subtree ends at subtreeEnd.
    struct STAI_StateDef
    {
        std::string name;
        std::string path;                               // Root|<parent>|...|<name>
        std::string comment;
        int parent = -1;                                // -1: a top level state (a child of Root)
        int depth = 0;
        std::vector<int> children;
        int subtreeEnd = 0;                             // index after the last state below this one
        ESTAI_StateType type = ESTAI_StateType::State;
        std::string linkedSubtree;                      // Linked: the name of the Subtree state it runs
        int linked = -1;
        ESTAI_StateSelection selection = ESTAI_StateSelection::TrySelectChildrenInOrder;
        float weight = 1.0f;
        uint32 retryDelayMs = 0;                        // a failed state is not selected for this long
        bool enabled = true;
        STAI_ConditionsDef enterConditions;
        std::vector<STAI_ConsiderationDef> considerations;
        std::vector<STAI_TaskDef> tasks;                // run at the same time
        std::vector<STAI_TransitionDef> transitions;    // checked in order
    };

    // a node of the tree with the state it belongs to (-1 global): the bot's instance creates a context per node
    struct STAI_NodeEntry
    {
        const STAI_NodeBase* node = nullptr;
        int state = -1;
    };

    // A state tree loaded from a STAI_*.xml file (format: statetree/STATETREE_FORMAT.md):
    //   <StateTree version="2">
    //     <GlobalTasks> <Task class="..."> ... </Task> ... </GlobalTasks>
    //     <State name="...">
    //       <EnterConditions> <Condition class="..." .../> <Any> ... </Any> </EnterConditions>
    //       <Considerations> <Consideration class="..." .../> </Considerations>
    //       <Tasks> <Task class="..."> settings, <Binding local="..." external="Root|..."/> </Task> </Tasks>
    //       <Transitions> <Transition trigger="..." target="Root|..."/> </Transitions>
    //       <State name="..."> ... </State>        child states
    //     </State>
    //   </StateTree>
    // Loaded once and shared by every bot: per bot only the contexts of the nodes differ (STAI_TreeInstance).
    class STAI_TreeAsset
    {
    public:
        std::string name;   // file stem, e.g. STAI_Combat_Mage
        std::string file;
        std::string comment;
        std::vector<STAI_StateDef> states;              // depth first
        std::vector<int> topStates;                     // the children of Root
        // global tasks: not part of any state, entered when the tree starts, ticked on every tick and exited when it
        // finishes (their result is ignored unless their completion says otherwise)
        std::vector<STAI_TaskDef> globalTasks;
        std::vector<STAI_NodeEntry> nodes;              // every task, condition and consideration of the tree

        // Throws std::runtime_error with a readable message on parse/validation errors, STAI_TreeFatalError when
        // two states have the same name or a path, transition or linked subtree is invalid.
        static std::shared_ptr<const STAI_TreeAsset> Load(const std::string& file);

        // tree with one unconditional "Idle" state waiting forever (Task_Wait, Delay 0)
        static std::shared_ptr<const STAI_TreeAsset> MakeIdle(const std::string& name);

        // the tree as the debug views show a tree that does not run (no bot's contexts): its states inactive,
        // conditions not checked, with the trees its tasks run down to the leaves
        StateNodeDebugTree GetDebugTree() const;

        // index of the state with that name, -1 none
        int FindState(const std::string& stateName) const;
    };

    // a state of the tree as the debug views show it without a bot's data: name, nesting, conditions (not checked),
    // transitions, the first task
    void STAI_DescribeState(const STAI_TreeAsset& asset, int index, StateNodeDebugChild& child);

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
        // one idle state (MakeIdle), used when no tree is configured for the state
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
