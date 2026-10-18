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
    std::string taskClass;
    std::vector<std::pair<std::string, StateNodeConditionResult>> conditions;
    std::string info;
    bool active = false;
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
    virtual std::vector<StateNodeDebugChild> GetDebugChildren() const { return {}; }
};


}