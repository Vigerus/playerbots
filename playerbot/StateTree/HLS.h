#pragma once

#define USE_HLS 1

#include "Platform/Define.h"

#include <string>
#include <vector>

namespace ai
{

// result of a node condition in the last update, as shown by the StateTree debug view
enum class StateNodeConditionResult : uint8
{
    NotChecked,
    Passed,
    Failed
};

// what an addon switch (BotStateTree, "statetree toggle ...") changes in a node of a tree
enum class StateTreeSwitch : uint8
{
    Task,           // a task of the node: value 1 switched off (skipped), 0 runs
    Node,           // the node: value 1 switched off (never selected, left when running), 0 normal
    Condition       // a condition of the node: value 0 checked normally, 1 forced to pass, 2 forced to fail
};

struct StateNodeDebugTree;

// child node of a state, as shown by the StateTree debug view
struct StateNodeDebugChild
{
    std::string name;
    std::string taskClass;                          // the running task (the first one while the node is inactive)
    std::vector<std::string> tasks;                 // task classes of the node, run one after another
    std::vector<bool> tasksDisabled;                // per task: switched off (STAI_TaskContext::bDisabled)
    std::vector<std::string> taskInfos;             // per task: its static debug info (STAI_Task::GetDebugInfo)
    int currentTask = -1;                           // index into tasks while the node is active
    std::vector<std::string> transitions;           // "<type> Root|<node>", in the order they are checked
    std::vector<std::pair<std::string, StateNodeConditionResult>> conditions;
    std::vector<uint8> conditionOverrides;          // per condition: 0 checked normally, 1 forced to pass, 2 forced to fail
    bool disabled = false;                          // the node is switched off (never selected)
    bool conditionsAny = false;                     // <Conditions match="any">: one of them has to pass
    std::string info;
    std::vector<uint32_t> spells;                   // spells of the running task (the first one while inactive)
    bool active = false;
    // the trees tasks of the node run (Task_RunSubtree) while they do not run: the loaded tree file's nodes, not
    // running, down to the leaves (StateNodeDebugTree::task names the task)
    std::vector<StateNodeDebugTree> subtrees;
};

// tree on the tree stack of a state, as shown by the StateTree debug view
struct StateNodeDebugTree
{
    std::string name;
    bool running = false;
    std::vector<std::string> globalTasks;           // names of the scopeless <Global> tasks
    std::vector<std::vector<uint32_t>> globalSpells; // per global task, the spells it works with
    std::vector<StateNodeDebugChild> nodes;
    int task = -1;                                  // a subtree of a node (StateNodeDebugChild::subtrees): the node's task running it
};

// NonCombat, Combat and Dead are the main states, one of them is active (StateMachine::TransitionTo);
// Reaction is additive: it ticks on every update next to the active main state
enum class HLSStateType : uint8_t
{
    NonCombat,
    Combat,
    Reaction,
    Dead
};

class IStateNode
{
public:
    virtual ~IStateNode() = default;

    virtual void OnEnter() {}
    virtual void OnExit() {}
    virtual void Update(uint32 elapsed, bool minimal) {}
    virtual HLSStateType GetStateType() const = 0;
    virtual const char* GetName() const = 0;

    // debug visualization
    // trees the state runs, bottom (its initial tree) to top (the tree selecting nodes)
    virtual std::vector<StateNodeDebugTree> GetDebugTreeStack() const { return {}; }
    // an addon switch of the tree named tree on its tree stack (BotStateTree addon): a task (index), the node or
    // a condition (index) of node; false when there is no such tree, node, task or condition
    virtual bool SetDebugSwitch(StateTreeSwitch what, const std::string& tree, int node, int index, int value) { return false; }
};


}