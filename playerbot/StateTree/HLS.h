#pragma once

#define USE_HLS 1

#include "playerbot/BotState.h"

#include <string>
#include <vector>

namespace ai
{

class Engine;

// result of a node condition in the last update, as shown by the StateTree debug view
enum class StateNodeConditionResult : uint8
{
    NotChecked,
    Passed,
    Failed
};

// child node of a state, as shown by the StateTree debug view
struct StateNodeDebugChild
{
    std::string name;
    std::string taskClass;                          // the running task (the first one while the node is inactive)
    std::vector<std::string> tasks;                 // task classes of the node, run one after another
    int currentTask = -1;                           // index into tasks while the node is active
    std::vector<std::string> transitions;           // "<type> Root|<node>", in the order they are checked
    std::vector<std::pair<std::string, StateNodeConditionResult>> conditions;
    bool conditionsAny = false;                     // <Conditions match="any">: one of them has to pass
    std::string info;
    bool active = false;
};

// tree on the tree stack of a state, as shown by the StateTree debug view
struct StateNodeDebugTree
{
    std::string name;
    bool running = false;
    std::vector<std::string> globalTasks;           // names of the scopeless <Global> tasks
    std::vector<StateNodeDebugChild> nodes;
};

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

    // temporary, for compatibility
    virtual void ReInit() {}
    virtual void UpdateStrategies(const std::string& name, BotState type) {};
    virtual void ResetStrategies() {};

    virtual void OnEnter() {}
    virtual void OnExit() {}
    virtual void Update(uint32 elapsed, bool minimal) {}
    virtual HLSStateType GetStateType() const = 0;
    virtual const char* GetName() const = 0;

    // debug visualization: engine whose strategies / last action are shown for this node
    virtual Engine* GetDebugEngine() const { return nullptr; }
    // trees the state runs, bottom (its initial tree) to top (the tree selecting nodes)
    virtual std::vector<StateNodeDebugTree> GetDebugTreeStack() const { return {}; }
};


}