#pragma once

#include "HLS.h"
#include "STAI_ActivityLog.h"

#include "Log/LogHelper.h"

#include <map>
#include <memory>

namespace ai
{
    // The bot's high level states. One main state is active at a time and switched with TransitionTo: NonCombat
    // (where it starts), Combat or Dead. Reaction is additive: it is not a state the bot is in but runs next to the
    // active main state on every update, for what the bot does independent of its movement and actions (talking,
    // rolling for loot, accepting a resurrection, ...).
    class StateMachine
    {
        IStateNode* active = nullptr;
        IStateNode* initial = nullptr;
        IStateNode* reaction = nullptr;

        std::map<HLSStateType, std::unique_ptr<IStateNode>> nodes;
    public:
        StateMachine();

        // one node per state type; the NonCombat node is the initial state, the Reaction node the additive one
        void RegisterNode(std::unique_ptr<IStateNode> node);

        // switches the main state (NonCombat, Combat, Dead); Reaction is never transitioned to
        void TransitionTo(HLSStateType type);

        void Update(uint32 elapsed, bool minimal);

        // leaves every state, the next Update enters the initial state (and Reaction) again
        void Reset();

        IStateNode* GetActive() const { return active; }
        // the main state the bot is in
        bool GetStateType(HLSStateType& type) const;

        // serialized tree state for the client-side StateTree visualizer addon (see BotStateTree addon)
        std::string BuildDebugSnapshot() const;
        // serialized recent activity (history tab of the addon)
        std::string BuildDebugHistory();
        // the addon's switches: state type name, tree name, node and task / condition index of the snapshot's top tree
        bool SetDebugSwitch(const std::string& state, StateTreeSwitch what, const std::string& tree, int node, int index, int value);

        STAI_ActivityLog& GetActivityLog() { return activityLog; }

    private:
        log4cxx::LoggerPtr m_logger;

        bool bInitialized = false;
        uint32 activeSinceMs = 0;
        STAI_ActivityLog activityLog;

        static bool IsMainState(HLSStateType type) { return type != HLSStateType::Reaction; }
    };

}
