#pragma once

#define USE_HLS 1

namespace ai
{

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
};


}