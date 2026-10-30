#pragma once

#include "playerbot/StateTree/STAI_TreeInstance.h"
#include "playerbot/StateTree/Tasks/STAI_GlobalTask.h"
#include "Util/Util.h"

#include <map>
#include <memory>
#include <vector>

template<int N>
struct HostVec : public std::vector<int> {};

struct HostGuid
{
    bool value = false;
    bool IsEmpty() const { return !value; }
};
struct HostLocation : public HostGuid {};
struct HostPosition : public HostGuid {};

struct HostStats
{
    int taskEnters = 0;
    int taskExits = 0;
};
inline HostStats hostStats;

inline bool HostChance(uint32 percent) { return urand(0, 99) < percent; }

inline ai::EActionRunStatus HostEnterResult()
{
    const uint32 roll = urand(0, 99);
    return roll < 60 ? ai::EActionRunStatus::Running : roll < 85 ? ai::EActionRunStatus::Succeeded : ai::EActionRunStatus::Failed;
}

inline ai::EActionRunStatus HostTickResult()
{
    const uint32 roll = urand(0, 99);
    return roll < 75 ? ai::EActionRunStatus::Running : roll < 95 ? ai::EActionRunStatus::Succeeded : ai::EActionRunStatus::Failed;
}

void RegisterStubTasks(ai::STAI_NodeFactory& factory);
