#pragma once

#include "playerbot/BotState.h"
#include "playerbot/StateTree/HLS.h"
#include "playerbot/StateTree/Tasks/StateTreeTask.h"

#include <string>

class Player;
class PlayerbotAI;

namespace ai
{
    class Engine;
    class IReactionChecker;
    class STAI_ActivityLog;

    // what a task is created for: the bot and the HLS state whose tree contains it
    struct STAI_TaskContext
    {
        PlayerbotAI* ai = nullptr;
        Player* bot = nullptr;
        HLSStateType state = HLSStateType::NonCombat;
        STAI_ActivityLog* activity = nullptr;   // the bot's recent activity, tasks may add entries
    };

    // Task of a STAI state tree node (<Task class="...">). Created once per bot when its tree is built.
    // Lifecycle while the node is selected: OnEnter, Update every bot update until it returns Succeeded or
    // Failed (Running keeps the node active), OnExit (also called when an earlier node interrupts it).
    class STAI_Task
    {
    public:
        virtual ~STAI_Task() = default;

        virtual const char* GetTaskClass() const = 0;

        virtual void OnEnter() {}
        virtual EActionRunStatus Update(uint32 elapsed, bool minimal) = 0;
        virtual void OnExit() {}

        // temporary, for compatibility with the legacy engines (forwarded by the state node to every task)
        virtual void UpdateStrategies(const std::string& name, BotState type) {}
        virtual void ResetStrategies() {}
        virtual void ReInit() {}

        // reaction tasks let the Reaction state preempt other states
        virtual IReactionChecker* GetReactionChecker() { return nullptr; }

        // the tree logs OnEnter..OnExit of the node; tasks that never finish (legacy engines) log their own entries
        virtual bool LogsNodeActivity() const { return true; }

        // debug visualization
        virtual Engine* GetDebugEngine() const { return nullptr; }
        virtual std::string GetDebugInfo() const { return ""; }
    };
}
